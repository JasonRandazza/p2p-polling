/// vote-bridge — stdin/stdout JSON bridge between polling_core (C++) and the Logos Zone SDK.
///
/// Protocol (one JSON object per line):
///   stdin  ← {"cmd":"publish","option":"Apples","vote_id":"<uuid>","sender":"<instanceId>"}
///   stdout → {"event":"ready"}
///   stdout → {"event":"vote","option":"Apples","vote_id":"<uuid>","sender":"<instanceId>"}
///   stdout → {"event":"error","message":"..."}
///
/// Environment variables:
///   VOTE_NODE_URL  — Logos node HTTP endpoint (default: http://localhost:8080)
///   VOTE_DATA_DIR  — directory for key + checkpoint files
///                    (default: ~/.local/share/Logos/polling_core)
use std::{
    fs,
    io::{self, BufRead, Write as _},
    path::{Path, PathBuf},
    time::Duration,
};

use futures::StreamExt as _;
use lb_core::mantle::ops::channel::ChannelId;
use lb_key_management_system_keys::keys::{ED25519_SECRET_KEY_SIZE, Ed25519Key};
use lb_zone_sdk::{
    CommonHttpClient, Slot, ZoneBlock, ZoneMessage,
    adapter::{Node as _, NodeHttpClient},
    sequencer::{SequencerCheckpoint, ZoneSequencer},
};
use reqwest::Url;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;

// Channel name — shared across all polling app instances on the network.
const CHANNEL_NAME: &str = "logos:yolo:polling";

// Only these values are accepted as vote options.
const VALID_OPTIONS: &[&str] = &["Apples", "Bananas", "Oranges"];

// How often the indexer polls for new blocks when the node is caught up.
const POLL_INTERVAL: Duration = Duration::from_secs(5);

// ── Channel ID ────────────────────────────────────────────────────────────────

fn polling_channel_id() -> ChannelId {
    let bytes = CHANNEL_NAME.as_bytes();
    assert!(bytes.len() <= 32, "channel name too long");
    let mut arr = [0u8; 32];
    arr[..bytes.len()].copy_from_slice(bytes);
    ChannelId::from(arr)
}

// ── Key & checkpoint helpers ──────────────────────────────────────────────────

fn load_or_create_key(path: &Path) -> Ed25519Key {
    if path.exists() {
        let bytes = fs::read(path).expect("failed to read key file");
        let arr: [u8; ED25519_SECRET_KEY_SIZE] = bytes
            .try_into()
            .expect("key file has wrong length — delete it to regenerate");
        Ed25519Key::from_bytes(&arr)
    } else {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).ok();
        }
        let mut bytes = [0u8; ED25519_SECRET_KEY_SIZE];
        rand::RngCore::fill_bytes(&mut rand::thread_rng(), &mut bytes);
        fs::write(path, bytes).expect("failed to write key file");
        Ed25519Key::from_bytes(&bytes)
    }
}

fn load_checkpoint(path: &Path, channel_id: ChannelId) -> Option<SequencerCheckpoint> {
    if !path.exists() {
        return None;
    }
    let sidecar = sidecar_path(path);
    if sidecar.exists() {
        let saved = fs::read(&sidecar).unwrap_or_default();
        if saved.as_slice() != channel_id.as_ref() {
            eprintln!("vote-bridge: channel changed — discarding stale checkpoint");
            let _ = fs::remove_file(path);
            let _ = fs::remove_file(&sidecar);
            return None;
        }
    } else {
        eprintln!("vote-bridge: checkpoint has no sidecar — discarding");
        let _ = fs::remove_file(path);
        return None;
    }
    let data = fs::read(path).ok()?;
    serde_json::from_slice(&data).ok()
}

fn save_checkpoint(path: &Path, checkpoint: &SequencerCheckpoint, channel_id: ChannelId) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).ok();
    }
    if let Ok(data) = serde_json::to_vec(checkpoint) {
        fs::write(path, &data).ok();
        fs::write(sidecar_path(path), channel_id.as_ref()).ok();
    }
}

fn sidecar_path(checkpoint_path: &Path) -> PathBuf {
    let mut p = checkpoint_path.to_path_buf();
    let name = p
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();
    p.set_file_name(format!("{name}.channel"));
    p
}

fn load_indexer_slot(path: &Path) -> Slot {
    let Some(contents) = fs::read_to_string(path).ok() else {
        return Slot::new(0);
    };

    contents
        .trim()
        .parse::<u64>()
        .map(Slot::new)
        .unwrap_or_else(|_| Slot::new(0))
}

fn save_indexer_slot(path: &Path, slot: Slot) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).ok();
    }
    fs::write(path, slot.into_inner().to_string()).ok();
}

// ── Protocol types ────────────────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
enum Command {
    Publish {
        option: String,
        vote_id: String,
        sender: String,
    },
}

#[derive(Debug, Serialize)]
#[serde(tag = "event", rename_all = "snake_case")]
enum Event {
    Ready,
    Vote {
        option: String,
        vote_id: String,
        sender: String,
    },
    Error {
        message: String,
    },
}

fn emit(event: &Event) {
    if let Ok(json) = serde_json::to_string(event) {
        println!("{json}");
        io::stdout().flush().ok();
    }
}

// ── Inscription processing ────────────────────────────────────────────────────

fn parse_zone_block(block: &ZoneBlock) -> Option<Event> {
    let json: serde_json::Value = serde_json::from_slice(&block.data).ok()?;
    let option = json["option"].as_str()?.to_string();
    if !VALID_OPTIONS.contains(&option.as_str()) {
        return None;
    }
    let vote_id = json["id"].as_str().unwrap_or("").to_string();
    let sender = json["sender"].as_str().unwrap_or("").to_string();
    if vote_id.is_empty() {
        return None;
    }
    Some(Event::Vote { option, vote_id, sender })
}

// ── Indexer task — polls node every POLL_INTERVAL for new channel messages ────
//
// Uses NodeHttpClient directly (not ZoneIndexer::follow) to avoid the
// `impl Stream + '_` lifetime constraint that is incompatible with tokio::spawn.

async fn run_indexer(
    node: NodeHttpClient,
    channel_id: ChannelId,
    vote_tx: mpsc::Sender<Event>,
    indexer_slot_path: PathBuf,
) {
    let mut last_slot = load_indexer_slot(&indexer_slot_path);
    eprintln!("vote-bridge: indexer starting from slot {last_slot:?}");

    loop {
        let current_slot = match node.consensus_info().await {
            Ok(info) => info.slot,
            Err(e) => {
                eprintln!("vote-bridge: consensus_info error: {e}");
                tokio::time::sleep(POLL_INTERVAL).await;
                continue;
            }
        };

        if current_slot.into_inner() >= last_slot.into_inner() {
            match node
                .zone_messages_in_blocks(last_slot, current_slot, channel_id)
                .await
            {
                Ok(stream) => {
                    futures::pin_mut!(stream);
                    while let Some((msg, _slot)) = stream.next().await {
                        if let ZoneMessage::Block(ref block) = msg {
                            if let Some(event) = parse_zone_block(block) {
                                vote_tx.send(event).await.ok();
                            }
                        }
                    }
                    last_slot = Slot::new(current_slot.into_inner() + 1);
                    save_indexer_slot(&indexer_slot_path, last_slot);
                }
                Err(e) => {
                    eprintln!("vote-bridge: scan error ({last_slot:?}–{current_slot:?}): {e}");
                }
            }
        }

        tokio::time::sleep(POLL_INTERVAL).await;
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let node_url: Url = std::env::var("VOTE_NODE_URL")
        .unwrap_or_else(|_| "http://localhost:8080".into())
        .parse()?;

    let data_dir = PathBuf::from(std::env::var("VOTE_DATA_DIR").unwrap_or_else(|_| {
        let home = std::env::var("HOME").unwrap_or_else(|_| ".".into());
        format!("{home}/.local/share/Logos/polling_core")
    }));

    let channel_id = polling_channel_id();
    let key = load_or_create_key(&data_dir.join("vote_bridge.key"));
    let checkpoint_path = data_dir.join("vote_bridge.checkpoint");
    let indexer_slot_path = data_dir.join("vote_bridge.indexer_slot");
    let checkpoint = load_checkpoint(&checkpoint_path, channel_id);

    let client = CommonHttpClient::new(None);
    let node = NodeHttpClient::new(client, node_url);

    // ── Sequencer (publishing) ────────────────────────────────────────────────
    let (sequencer, mut seq_handle) =
        ZoneSequencer::init(channel_id, key, node.clone(), checkpoint);
    sequencer.spawn();

    seq_handle.wait_ready().await;
    emit(&Event::Ready);

    // ── Indexer task (polling) ────────────────────────────────────────────────
    let (vote_tx, mut vote_rx) = mpsc::channel::<Event>(64);
    tokio::spawn(run_indexer(node, channel_id, vote_tx, indexer_slot_path));

    // ── Stdin command reader ──────────────────────────────────────────────────
    // Stdin is blocking, so read it on a dedicated OS thread and forward
    // parsed commands via a channel into the async main loop.
    let (cmd_tx, mut cmd_rx) = mpsc::channel::<Command>(16);
    let cmd_tx_stdin = cmd_tx.clone();
    std::thread::spawn(move || {
        let stdin = io::stdin();
        for line in stdin.lock().lines().flatten() {
            let line = line.trim().to_string();
            if line.is_empty() {
                continue;
            }
            if let Ok(cmd) = serde_json::from_str::<Command>(&line) {
                if cmd_tx_stdin.blocking_send(cmd).is_err() {
                    break;
                }
            }
        }
    });

    // ── Main loop ─────────────────────────────────────────────────────────────
    loop {
        tokio::select! {
            Some(cmd) = cmd_rx.recv() => {
                match cmd {
                    Command::Publish { option, vote_id, sender } => {
                        if !VALID_OPTIONS.contains(&option.as_str()) {
                            continue;
                        }
                        // Embed the same JSON structure as the Waku path so that
                        // vote_id can be used for cross-channel deduplication in C++.
                        let payload = serde_json::json!({
                            "type":   "vote",
                            "id":     vote_id,
                            "sender": sender,
                            "option": option,
                        });
                        match seq_handle
                            .publish_message(payload.to_string().into_bytes())
                            .await
                        {
                            Ok(result) => {
                                save_checkpoint(&checkpoint_path, &result.checkpoint, channel_id);
                            }
                            Err(e) => {
                                emit(&Event::Error { message: e.to_string() });
                            }
                        }
                    }
                }
            }
            Some(event) = vote_rx.recv() => {
                emit(&event);
            }
            else => break,
        }
    }

    Ok(())
}
