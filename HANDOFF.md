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

## Current State (2026-04-27)

Both packages built and installed into `LogosBasecampDev`:

- Core: persistent storage, P2P broadcast/receive, deduplication, network status events
- UI: vote cards, loading state, toast notifications, status bar with network info

All checklist items through multi-instance P2P verified working.

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

5. Multi-instance/network test:
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
- Confirm subscription to `/logos/tutorial/polling/1/vote/proto` completes successfully.
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
