# P2P Polling App Handoff

This document captures the current project context and progress for another coding agent.

## Workspace

- Bootcamp root: `/home/jrazz/logos-bootcamp`
- Project root: `/home/jrazz/logos-bootcamp/p2p-polling`
- Delivery source checkout: `/home/jrazz/logos-bootcamp/logos-delivery`
- Basecamp dev install folders:
  - Core modules: `/home/jrazz/.local/share/Logos/LogosBasecampDev/modules`
  - UI plugins: `/home/jrazz/.local/share/Logos/LogosBasecampDev/plugins`
- GitHub repo: `https://github.com/JasonRandazza/p2p-polling`

## AI Context Files To Read First

Before changing code, another AI agent should read these local context files. They contain important Logos-specific assumptions that are easy to miss.

- `/home/jrazz/logos-bootcamp/skills.md`
  - High-level Logos ecosystem context: Nomos, LEZ, SPEL, wallet, and related command patterns.
  - This is broader than Basecamp, but useful for avoiding bad architecture assumptions.
- `/home/jrazz/logos-bootcamp/.gemini/skills/logos-messaging-nim/SKILL.md`
  - Messaging/Delivery/Waku/Nim context. Most relevant for future `logos-delivery` work.
- `/home/jrazz/logos-bootcamp/.gemini/skills/logos-c-bindings/SKILL.md`
  - C FFI binding guidance. Relevant because this app currently links `polling_core` directly to `liblogosdelivery`.
- `/home/jrazz/logos-bootcamp/.gemini/skills/logos-testnets/SKILL.md`
  - Testnet/node/relay context. Relevant when testing real peer-to-peer propagation.
- `/home/jrazz/logos-bootcamp/.gemini/skills/nomos-blockchain/SKILL.md`
  - Blockchain context. Less relevant to local UI behavior, but relevant if this app later anchors votes or identity on Nomos.
- `/home/jrazz/logos-bootcamp/.gemini/skills/codex-js-sdk/SKILL.md`
  - JavaScript SDK context. Not directly used in the current Qt/C++ module, but useful if a web bridge or JS tooling is added.
- `/home/jrazz/logos-bootcamp/logos-tutorial/logos-developer-guide.md`
  - Logos module packaging, IPC, `.lgx` variants, and Basecamp install behavior.
- `/home/jrazz/logos-bootcamp/logos-tutorial/tutorial-qml-ui-app.md`
  - QML UI module patterns and dev-vs-portable install notes.
- `/home/jrazz/logos-bootcamp/logos-tutorial/tutorial-wrapping-c-library.md`
  - External library packaging details. Relevant to bundling `liblogosdelivery.so`.
- `/home/jrazz/logos-bootcamp/logos-delivery/liblogosdelivery/README.md`
  - Primary source for the local Delivery C FFI.
- `/home/jrazz/logos-bootcamp/logos-delivery/liblogosdelivery/MESSAGE_EVENTS.md`
  - Delivery event shapes and callback behavior.

The `.gemini/skills/*/references`, `scripts`, and `assets` folders may also contain useful details, but do not bulk-load them. Open the specific reference needed for the task.

## Goal

Build a Logos Basecamp "P2P Polling App" with:

- A core module named `polling_core`.
- A UI module named `polling_ui`.
- Three vote options: Apples, Bananas, Oranges.
- UI votes update local counts immediately.
- Core broadcasts votes over Logos Delivery/Waku.
- Core subscribes to the same Delivery topic and applies valid remote votes.
- UI updates from core events, not from shared memory.

## Logos Architecture Notes

The Logos module model is process-isolated:

- UI and core modules run in separate processes.
- They cannot share C++ headers or memory directly for runtime behavior.
- UI talks to core through the Logos Qt Remote Objects API.
- Core methods called by UI must be exposed as `Q_INVOKABLE`.
- Core emits events through `eventResponse(...)`; UI subscribes using `logos.onModuleEvent(...)`.

The Gemini-provided pattern `logos::getModule<ILogosDelivery>("logos-delivery")` was not present in this local workspace. The local `logos-delivery` checkout currently exposes a C FFI library instead.

## Delivery Integration Chosen

This implementation links `polling_core` directly against the local `liblogosdelivery` C FFI from:

`/home/jrazz/logos-bootcamp/logos-delivery/liblogosdelivery/liblogosdelivery.h`

Important FFI functions:

```c
void *logosdelivery_create_node(const char *configJson, FFICallBack callback, void *userData);
int logosdelivery_start_node(void *ctx, FFICallBack callback, void *userData);
int logosdelivery_stop_node(void *ctx, FFICallBack callback, void *userData);
int logosdelivery_destroy(void *ctx, FFICallBack callback, void *userData);
int logosdelivery_subscribe(void *ctx, FFICallBack callback, void *userData, const char *contentTopic);
int logosdelivery_unsubscribe(void *ctx, FFICallBack callback, void *userData, const char *contentTopic);
int logosdelivery_send(void *ctx, FFICallBack callback, void *userData, const char *messageJson);
void logosdelivery_set_event_callback(void *ctx, FFICallBack callback, void *userData);
```

The polling content topic is:

```text
/logos-polling/1/votes/proto
```

**Critical note:** Waku content topics must be exactly 4 segments (`/<application>/<version>/<topic-name>/<encoding>`) or 5 segments (`/<gen>/<application>/<version>/<topic-name>/<encoding>`). An earlier version of this code used `/logos/tutorial/polling/1/vote/proto` (6 segments), which caused `logosdelivery_subscribe` to silently fail, keeping `m_networkReady = false` permanently and preventing all broadcasts.

Vote messages are JSON payloads, base64 encoded into the Delivery send envelope:

```json
{
  "contentTopic": "/logos-polling/1/votes/proto",
  "payload": "<base64 JSON vote>",
  "ephemeral": false
}
```

The decoded vote payload shape is:

```json
{
  "type": "vote",
  "id": "<uuid>",
  "sender": "<instance uuid>",
  "option": "Apples"
}
```

## Files Changed So Far

### `core/flake.nix`

- Added local `logos-delivery` flake input:

```nix
logos-delivery.url = "path:/home/jrazz/logos-bootcamp/logos-delivery";
```

- Added `externalLibInputs` for `logosdelivery`, using the `liblogosdelivery` package.

Note: this absolute path is practical for this local bootcamp workspace. For a more portable GitHub version, consider switching later to a GitHub flake URL or documenting the required sibling checkout.

### `core/metadata.json`

- Describes the module as a P2P vote-counting backend.
- Adds `logosdelivery` as an external library with `vendor_path: "lib"`.
- Adds `lib` to `extra_include_dirs`.
- Does not add `"logos-delivery"` to Logos module dependencies because Delivery is currently consumed as a linked C FFI library, not as a Basecamp module dependency.

### `core/CMakeLists.txt`

- Adds:

```cmake
EXTERNAL_LIBS
    logosdelivery
```

### `core/src/polling_core_plugin.h`

Adds Delivery state, callbacks, and helpers:

- `m_delivery`
- `m_seenVoteIds`
- `m_instanceId`
- `m_networkStatus`
- `m_networkReady`
- callback functions for create/start/subscribe/events/operations
- helper methods for initialize, subscribe, shutdown, broadcast, parse events, record votes, update status

### `core/src/polling_core_plugin.cpp`

Important behavior:

- Includes `liblogosdelivery.h`.
- Initializes Logos API correctly with `logosAPI = api;`.
- Creates a random instance ID.
- Creates a Delivery node with config:
  - `logLevel: "INFO"`
  - `mode: "Core"`
  - `preset: "logos.dev"`
  - randomized `portsShift` to reduce port collisions when running multiple local Basecamp instances.
- Starts the Delivery node asynchronously.
- Subscribes to `/logos-polling/1/votes/proto`.
- On local `submitVote(QString option)`:
  - validates option
  - records local vote immediately
  - emits `voteSubmitted`
  - broadcasts the vote if Delivery is ready
- On incoming `message_received`:
  - validates topic
  - decodes payload
  - ignores own sender ID
  - deduplicates vote IDs
  - validates option
  - records network vote
  - emits `voteSubmitted`
- Persists vote counts via `QSettings("Logos", "polling_core")` after each vote; loads persisted counts in constructor.
- Emits `networkStatusChanged` when Delivery status changes.
- Shuts down Delivery in the destructor.

### `ui/src/qml/Main.qml`

- Dark themed polling UI.
- Calls core through `logos.callModuleAsync(...)` (falls back to sync `logos.callModule`).
- Subscribes to:
  - `voteSubmitted`
  - `networkStatusChanged`
- Updates displayed counts from core snapshots.
- Shows total votes and network status in the status bar.
- **Loading state:** While a vote is in-flight, the clicked button changes to "Sending..." and all vote buttons are disabled.
- **Toast notifications:** When a remote vote arrives (`source === "network"`), a pill-shaped overlay slides in from the top reading "New vote received for [Option]!" and auto-dismisses after 3 seconds.

## Bug Fixes and Runtime Verification

Committed before Delivery work:

- Fixed `callModuleAsync` overload errors by passing a callback argument.
- Fixed core Logos API initialization so `QtProviderObject::callMethod: LogosAPI not available` no longer blocks calls.
- Fixed UI to update from core events after votes.

Critical P2P bug fixed during runtime testing:

- **Bug:** Content topic `/logos/tutorial/polling/1/vote/proto` (6 path segments) caused `logosdelivery_subscribe` to fail silently. `m_networkReady` was always `false`, so `broadcastVote` never sent anything.
- **Fix:** Changed to `/logos-polling/1/votes/proto` (4 path segments). Committed in `1a7913f Expand project handoff notes`.

Confirmed working end-to-end in Basecamp (2026-04-27):

- Two Basecamp instances launched simultaneously on the same machine.
- Voted in instance A → appeared in instance B within seconds.
- Voted in instance B → appeared in instance A.
- Deduplication confirmed: no double-counting of the same vote ID.
- Self-message filtering confirmed: own votes not double-counted from relay echo.

Commit history:

- `2ac2bb3 Create P2P polling Logos modules`
- `f34539a Fix polling UI async module calls`
- `9540338 Fix polling core LogosAPI initialization`
- `6e095e3 Update polling UI from core vote events`
- `0521618 Wire polling app to Logos Delivery`
- `1a7913f Expand project handoff notes`

## Session 2 Additions (2026-04-27)

### Persistent vote counts

Vote counts survive Basecamp restarts. The core saves counts after each `recordVote` call using `QSettings("Logos", "polling_core")` and loads them in the constructor. Storage location is the platform default (`~/.config/Logos/polling_core.conf` on Linux).

### UI loading state

Clicking a vote button sets it to "Sending..." and disables all three buttons. They re-enable when the IPC callback returns.

### Toast notifications for remote votes

When a `voteSubmitted` event arrives with `source === "network"`, a pill-shaped overlay slides in from the top of the UI reading "New vote received for [Option]!" and auto-dismisses after 3 seconds.

### Portable build note

`nix build .#lgx-portable` (without `--impure`) succeeds on this machine because the `logos-delivery` flake lock file contains a `narHash` and the content is already in the nix store from previous impure builds. On a fresh machine the first build still requires `--impure`. The portable output contains the `linux-amd64` variant (not `linux-amd64-dev`) — do NOT install the portable into `LogosBasecampDev`.

## Session 3 Additions (2026-04-27) — Blockchain Channel Inscriptions

### Architecture

The polling app now submits votes through **two parallel channels**:

1. **Waku/Logos Delivery** (existing) — low-latency P2P gossip, ~seconds
2. **Logos blockchain ChannelInscribe** (new) — permanent on-chain record, ~slot time

Both channels embed the same `vote_id` UUID in the payload. The C++ core deduplicates via `m_seenVoteIds`, so a vote delivered by both channels is only counted once. If Waku is unavailable, the blockchain delivers the vote and vice-versa.

### How ChannelInscribe works

`ChannelInscribe` is a Logos blockchain (Nomos) operation. It:

1. Creates a `MantleTx` with an `InscriptionOp` containing the vote JSON payload
2. Signs it with an Ed25519 key
3. POSTs the `SignedMantleTx` to the node's `/mempool/add/tx` endpoint

The channel is identified by a 32-byte `ChannelId`. All polling app instances use the same channel: `"logos:yolo:polling"` zero-padded to 32 bytes.

### vote-bridge sidecar (new file: `core/vote-bridge/`)

The Zone SDK (`lb_zone_sdk`) is a Rust library — it cannot be called from C++ directly. A small Rust sidecar binary (`vote-bridge`) bridges the two:

- **Protocol**: JSON lines on stdin/stdout
- `stdout → {"event":"ready"}` — emitted once the sequencer receives its first block
- `stdout → {"event":"vote","option":"Apples","vote_id":"<uuid>","sender":"<instanceId>"}` — when a remote vote is detected on-chain
- `stdin  ← {"cmd":"publish","option":"Apples","vote_id":"<uuid>","sender":"<instanceId>"}` — from C++ to inscribe a vote

**Indexer**: polls `/cryptarchia/blocks?from=X&to=Y` every 5 seconds using `NodeHttpClient::zone_messages_in_blocks`. On first startup it backfills from genesis slot 0 to the current canonical tip. Subsequent polls scan only new slots.

**Sequencer**: uses `ZoneSequencer::init()` + `spawn()`. Tracks last message ID for chain linking. Checkpoint persisted to `VOTE_DATA_DIR/vote_bridge.checkpoint` after each successful publish.

**Key management**: Ed25519 key loaded from or generated at `VOTE_DATA_DIR/vote_bridge.key` on first run.

**Environment variables**:

- `VOTE_NODE_URL` — node HTTP endpoint (default: `http://localhost:8080`)
- `VOTE_DATA_DIR` — where key and checkpoint live (default: `~/.local/share/Logos/polling_core`)

### Rust toolchain note

The vote-bridge pins `channel = "1.93.0"` (same as zone-sdk-test) via `core/vote-bridge/rust-toolchain.toml`. Dependencies are pinned to `tag = "0.1.2"` of `logos-blockchain` GitHub — the same tag used by zone-sdk-test.

### C++ integration (`polling_core_plugin.cpp`)

`startVoteBridge()` is called from `initLogos()`. It searches for the `vote-bridge` binary in the module install directory:

- `~/.local/share/Logos/LogosBasecampDev/modules/polling_core/vote-bridge`
- `~/.local/share/Logos/modules/polling_core/vote-bridge`

If not found, blockchain inscriptions are disabled and surfaced through `chainStatus` (Waku-only fallback).

`broadcastVote()` now writes a `{"cmd":"publish",...}` JSON line to vote-bridge stdin in addition to the existing Waku send.

`processVoteBridgeOutput()` parses incoming JSON lines from vote-bridge stdout and calls `recordVote()` for `"vote"` events (with deduplication).

### Building vote-bridge

```bash
cd /home/jrazz/logos-bootcamp/p2p-polling/core/vote-bridge
cargo build --release
# Then copy to module dir (lgpm reinstall overwrites the dir, so copy after each lgpm install):
cp target/release/vote-bridge \
   ~/.local/share/Logos/LogosBasecampDev/modules/polling_core/vote-bridge
```

First build downloads all Rust crates (~10 min). Subsequent builds are fast (seconds).

### Installing after code changes

```bash
# 1. Rebuild C++ core
cd /home/jrazz/logos-bootcamp/p2p-polling/core
nix build .#lgx -L --impure -o result-polling-core-lgx-dev

# 2. Install C++ core
/home/jrazz/logos-bootcamp/logos-tutorial/pm/bin/lgpm \
  --modules-dir ~/.local/share/Logos/LogosBasecampDev/modules \
  install --file result-polling-core-lgx-dev/logos-polling_core-module-lib.lgx

# 3. Copy vote-bridge (lgpm install overwrites the module dir)
cp /home/jrazz/logos-bootcamp/p2p-polling/core/vote-bridge/target/release/vote-bridge \
   ~/.local/share/Logos/LogosBasecampDev/modules/polling_core/vote-bridge
```

### Runtime verification checklist

- Launch Basecamp with the consensus node running on port 8080
- Open polling_ui — after a few seconds the sequencer connects and logs appear
- Cast a vote — it should be submitted both via Waku and inscribed on-chain
- Wait ~5 seconds — the indexer poll will pick up the inscription and emit a `vote` event
- The C++ core receives the event; since the vote_id is already in `m_seenVoteIds` it's a no-op (expected)
- On a second Basecamp instance: their vote inscription arrives via the 5-second poll, passes deduplication, and increments the counter

## Session 4 Testing/Fixes (2026-04-27)

Latest local verification after resuming development:

- `cargo test --release` in `core/vote-bridge` completed successfully. There are currently 0 Rust unit tests, but the release binary compiles.
- Initial `curl` checks from inside the default Codex sandbox could not reach `localhost`, but that was a sandbox/network-namespace artifact.
- Re-running outside the sandbox confirmed the directly-running Linux node is reachable at `http://localhost:8080/cryptarchia/info` and reports `mode: "Online"`.
- Docker is not required for this setup. The user intentionally runs Logos services directly on Linux.
- `vote-bridge` was run against `VOTE_NODE_URL=http://localhost:8080` with a timeout and emitted `{"event":"ready"}`. It was stopped before publishing any test votes.

Code fix made in this session:

- `polling_core` now separates Delivery/Waku status from blockchain sidecar status:
  - `deliveryStatus` / `deliveryReady`
  - `chainStatus` / `chainReady`
  - existing `networkStatus` / `networkReady` are kept for backward compatibility with older UI code.
- `vote-bridge` readiness no longer overwrites the Delivery status. It updates `chainStatus`.
- Missing, failed, stopped, or errored `vote-bridge` states are surfaced to the UI instead of being mostly log-only.
- The UI status panel now displays Delivery and Blockchain status on separate lines.
- The Delivery test checklist was corrected to use `/logos-polling/1/votes/proto`, not the earlier invalid six-segment topic.
- Added app-specific vote audit logs in `polling_core_plugin.cpp` so future tests can trace one vote ID across local recording, Waku broadcast/receive, blockchain publish/receive, and duplicate/self-message ignores.

Useful audit filter:

```bash
rg "broadcasting Waku vote|publishing blockchain vote|received Waku vote|received blockchain vote|recorded vote|ignored own|ignored duplicate|skipped Waku|skipped blockchain" ~/basecamp-polling.log
```

Build/install verification after these fixes:

```bash
cd /home/jrazz/logos-bootcamp/p2p-polling/core/vote-bridge
cargo test --release

cd /home/jrazz/logos-bootcamp/p2p-polling/core
nix build .#lgx -L --impure -o result-polling-core-lgx-dev

cd /home/jrazz/logos-bootcamp/p2p-polling/ui
nix build .#lgx -L --impure -o result-polling-ui-lgx-dev
```

Both `nix build` commands passed and fresh dev packages were installed into `LogosBasecampDev`.

The installed sidecar binary already matched the fresh release binary:

```bash
sha256sum core/vote-bridge/target/release/vote-bridge
sha256sum ~/.local/share/Logos/LogosBasecampDev/modules/polling_core/vote-bridge
```

Both hashes were:

```text
45c96773f0976869974cc52f268d0fb95346b999ae00cc02270f4009f941339e
```

Expected runtime behavior if no blockchain node is running:

- Local voting should still increment immediately.
- Delivery/Waku status should report separately.
- Blockchain status should remain on a connecting/error/stopped state until a valid node endpoint is reachable.
- This missing node condition should not block local voting or Waku-based P2P voting.

Expected runtime behavior on this machine now:

- The default `VOTE_NODE_URL=http://localhost:8080` is correct for the directly-running node.
- `vote-bridge` should emit `ready` shortly after Basecamp loads `polling_core`.
- The UI should show Delivery and Blockchain status separately.
- A real vote click may now publish to Waku and attempt an on-chain inscription.

## Current State (2026-04-27)

Both packages built and installed into `LogosBasecampDev`:

- Core: persistent storage, Waku P2P broadcast/receive, blockchain inscription, deduplication across both channels, separated Delivery and blockchain status events
- UI: vote cards, loading state, toast notifications, status panel with separate Delivery and Blockchain info

All checklist items through multi-instance P2P verified working (Waku). Blockchain inscription code compiled, binary installed, and status surfaced in UI, but runtime inscription still awaits a reachable consensus/node HTTP endpoint.

What was proven by builds:

- `liblogosdelivery` built successfully from the local `logos-delivery` checkout.
- `polling_core` compiled and linked successfully against `liblogosdelivery.so`.
- The fresh core package bundled `liblogosdelivery.so` with `polling_core_plugin.so`.
- The UI package generated and consumed the fresh `polling_core_api.h` from the rebuilt core.
- Both core and UI dev `.lgx` packages installed successfully into `LogosBasecampDev`.

Installed files to verify:

```text
/home/jrazz/.local/share/Logos/LogosBasecampDev/modules/polling_core/polling_core_plugin.so
/home/jrazz/.local/share/Logos/LogosBasecampDev/modules/polling_core/liblogosdelivery.so
/home/jrazz/.local/share/Logos/LogosBasecampDev/modules/polling_core/variant
/home/jrazz/.local/share/Logos/LogosBasecampDev/plugins/polling_ui/polling_ui_plugin.so
/home/jrazz/.local/share/Logos/LogosBasecampDev/plugins/polling_ui/polling_ui_replica_factory.so
/home/jrazz/.local/share/Logos/LogosBasecampDev/plugins/polling_ui/qml/Main.qml
```

The installed dev variant should be:

```text
linux-amd64-dev
```

Important realization: `LogosBasecampDev` needs `.#lgx` packages, not `.#lgx-portable`. The portable package was valid, but `lgpm` rejected it for the dev install because it contained `linux-amd64` while the dev install expects `linux-amd64-dev`.

## Current Build State

The standalone Delivery library build completed successfully once:

```bash
nix build /home/jrazz/logos-bootcamp/logos-delivery#liblogosdelivery -L -o /home/jrazz/logos-bootcamp/p2p-polling/result-liblogosdelivery
```

Result symlink:

```text
/home/jrazz/logos-bootcamp/p2p-polling/result-liblogosdelivery
```

Then the core build was started:

```bash
cd /home/jrazz/logos-bootcamp/p2p-polling/core
nix build .#lib -L --impure
```

The first attempt failed because `logos-delivery.url = "path:../../logos-delivery"` was resolved incorrectly by Nix as an invalid store path. The flake input was changed to the absolute path above.

The second core build completed successfully. It rebuilt `liblogosdelivery`, compiled `polling_core_plugin.cpp`, linked `polling_core_plugin.so`, and copied `liblogosdelivery.so` into the module output.

Fresh dev packages were also built:

```bash
cd /home/jrazz/logos-bootcamp/p2p-polling/core
nix build .#lgx -L --impure -o result-polling-core-lgx-dev

cd /home/jrazz/logos-bootcamp/p2p-polling/ui
nix build .#lgx -L --impure -o result-polling-ui-lgx-dev
```

Both dev packages were installed successfully into `LogosBasecampDev`:

```bash
/home/jrazz/logos-bootcamp/logos-tutorial/pm/bin/lgpm \
  --modules-dir /home/jrazz/.local/share/Logos/LogosBasecampDev/modules \
  install --file /home/jrazz/logos-bootcamp/p2p-polling/core/result-polling-core-lgx-dev/logos-polling_core-module-lib.lgx
```

```bash
/home/jrazz/logos-bootcamp/logos-tutorial/pm/bin/lgpm \
  --ui-plugins-dir /home/jrazz/.local/share/Logos/LogosBasecampDev/plugins \
  install --file /home/jrazz/logos-bootcamp/p2p-polling/ui/result-polling-ui-lgx-dev/logos-polling_ui-module.lgx
```

Note: installing `.#lgx-portable` into `LogosBasecampDev` failed because the dev Basecamp install expects a dev variant. Use `.#lgx` for `LogosBasecampDev` and `.#lgx-portable` for portable Basecamp builds.

To rebuild the core library only:

```bash
cd /home/jrazz/logos-bootcamp/p2p-polling/core
nix build .#lib -L --impure
```

## Next Steps

1. Test in Basecamp using the already-installed dev packages.

2. If runtime loading fails:
   - Check whether `polling_core` starts and whether `liblogosdelivery.so` is present in `/home/jrazz/.local/share/Logos/LogosBasecampDev/modules/polling_core`.
   - Check Basecamp logs for Delivery node startup errors.
   - Confirm `variant` files say `linux-amd64-dev` for Dev Basecamp.

3. If future core builds fail:
   - First inspect compile errors in `polling_core_plugin.cpp`.
   - Do not immediately rewrite CMake unless the error is clearly link/include related.
   - Check that the external library include files are copied into the expected `lib` include path by the Logos module builder.

4. Locate LGX outputs:

```bash
find /home/jrazz/logos-bootcamp/p2p-polling -maxdepth 4 -name '*.lgx'
```

1. Multi-instance/network test:
   - Run two Basecamp instances if RAM allows.
   - Vote in instance A.
   - Instance B should receive `message_received` and update.

## Testing Checklist

Run these tests in order. Stop at the first failure and inspect logs before changing code.

### Single-Instance Smoke Test

- Launch the dev Basecamp app.
- Open `polling_ui` from the app launcher.
- Confirm the UI loads without QML errors.
- Confirm the UI shows the three options: Apples, Bananas, Oranges.
- Confirm initial counts load from `polling_core`.
- Click Apples once.
- Confirm Apples increments immediately.
- Click Bananas once.
- Confirm Bananas increments immediately.
- Click Oranges once.
- Confirm Oranges increments immediately.
- Confirm the total vote count equals the sum of the three option counts.

### IPC/Event Test

- Watch Basecamp logs while clicking buttons.
- Confirm `polling_core` loads as a dependency for `polling_ui`.
- Confirm `submitVote` calls reach `polling_core`.
- Confirm `voteSubmitted` events are forwarded from the core.
- Confirm the UI updates from the event payload, not only from the immediate async call result.
- Confirm no `Unable to determine callable overload` errors appear. That old bug was fixed by passing a callback to `callModuleAsync`.
- Confirm no `QtProviderObject::callMethod: LogosAPI not available` errors block method calls. That old bug was fixed by assigning the global `logosAPI` in `initLogos`.

### Delivery Startup Test

- Check logs for Delivery node initialization.
- Confirm `logosdelivery_create_node` completes successfully.
- Confirm `logosdelivery_start_node` completes successfully.
- Confirm subscription to `/logos-polling/1/votes/proto` completes successfully.
- Confirm the UI status text eventually reflects the Delivery network state.
- If Delivery fails to start, inspect the status emitted by `networkStatusChanged`.

### Local Broadcast Test

- Click a vote after Delivery is ready.
- Confirm logs show the local vote was recorded.
- Confirm logs show `logosdelivery_send` or a related send operation.
- Confirm send failures update network status without rolling back the local vote.

### Multi-Instance Test

- Start two Basecamp instances if RAM allows.
- Open `polling_ui` in both.
- Wait for both to show Delivery readiness or at least a non-crashing status.
- Vote Apples in instance A.
- Confirm instance A increments immediately.
- Watch instance B for a remote update.
- Vote Bananas in instance B.
- Confirm instance A receives the remote update.
- Confirm duplicate self-messages do not double-count. The core ignores messages where `sender == m_instanceId` and deduplicates vote IDs.

### Network Environment Test

- If multi-instance or cross-machine sync fails, verify WSL2 networking and Windows Firewall.
- Confirm any required Delivery peers/relays are running and reachable.
- Remember: a blockchain node is not required for local UI vote clicks. Real P2P propagation depends on Delivery networking, peers/relays, and firewall access.

### Packaging/Install Test

- Confirm `polling_core` install contains `liblogosdelivery.so`.
- Confirm both installed `variant` files say `linux-amd64-dev` for `LogosBasecampDev`.
- If using portable Basecamp instead, rebuild and install `.#lgx-portable` outputs into the portable Basecamp data directory, not `LogosBasecampDev`.

### Regression Tests

- Reopen Basecamp after closing it fully.
- Open `polling_ui` again and confirm it still loads.
- Click all three options.
- Confirm no stale module from an older install is being loaded. If behavior looks stale, check timestamps and installed files under `LogosBasecampDev`.

## Caveats

- The app does not need a blockchain node for local UI vote clicks.
- Actual P2P propagation does need working Delivery networking, peers/relays, and firewall access.
- WSL2 networking and Windows Firewall can block gossip traffic.
- If no peers are reachable, local voting should still work and network status should tell the user Delivery is not ready or not connected.
- Because `portsShift` is randomized, two local instances are less likely to collide, but this is still experimental.
- The current flake uses an absolute local path to `logos-delivery`; this is good for this machine and bootcamp folder, but not portable for strangers cloning only `p2p-polling`.

## Suggested Commit Message

After successful build/install verification:

```text
Wire polling app to Logos Delivery
```

Possible body:

```text
- Link polling_core against the local liblogosdelivery FFI package
- Start and subscribe a Delivery node from the core module
- Broadcast local votes and apply validated remote vote messages
- Surface network readiness/status updates in the QML UI
```
