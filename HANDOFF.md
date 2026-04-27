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
/logos/tutorial/polling/1/vote/proto
```

Vote messages are JSON payloads, base64 encoded into the Delivery send envelope:

```json
{
  "contentTopic": "/logos/tutorial/polling/1/vote/proto",
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
- Subscribes to `/logos/tutorial/polling/1/vote/proto`.
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
- Emits `networkStatusChanged` when Delivery status changes.
- Shuts down Delivery in the destructor.

### `ui/src/qml/Main.qml`

- Dark themed polling UI.
- Calls core through `logos.callModuleAsync(...)`.
- Subscribes to:
  - `voteSubmitted`
  - `networkStatusChanged`
- Updates displayed counts from core snapshots.
- Shows total votes and network status.

## Previous Bug Fixes Already Landed Before This Work

These were already committed before the Delivery work:

- Fixed `callModuleAsync` overload errors by passing a callback argument.
- Fixed core Logos API initialization so `QtProviderObject::callMethod: LogosAPI not available` no longer blocks calls.
- Fixed UI to update from core events after votes.

Existing commits at the time of this handoff:

- `2ac2bb3 Create P2P polling Logos modules`
- `f34539a Fix polling UI async module calls`
- `9540338 Fix polling core LogosAPI initialization`
- `6e095e3 Update polling UI from core vote events`

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

1. Test in Basecamp:
   - Launch Basecamp.
   - Open `polling_ui`.
   - Confirm initial counts load.
   - Click each vote button.
   - Confirm counts change immediately.
   - Confirm logs show Delivery start/subscribe status.

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
