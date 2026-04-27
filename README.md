# P2P Polling for Logos Basecamp

An experimental full-stack Logos module app for peer-to-peer poll voting.

This project is split into two isolated Logos modules:

- `core/` contains `polling_core`, a C++ backend module that tracks vote counts and connects to Logos Delivery.
- `ui/` contains `polling_ui`, a Qt6/QML frontend module that calls `polling_core` through the Logos IPC bridge.

Local votes update immediately, then the core broadcasts vote messages on a Logos Delivery topic. Other running instances subscribed to the same topic can apply valid remote vote messages and notify the UI through Logos module events.

## Current Poll

The app supports three options:

- Apples
- Bananas
- Oranges

## Delivery Topic

Votes are broadcast on:

```text
/logos/tutorial/polling/1/vote/proto
```

The current build links directly against the local `liblogosdelivery` FFI from the sibling checkout at `/home/jrazz/logos-bootcamp/logos-delivery`. That makes this practical for the bootcamp workspace, but a public clone currently needs the same local checkout path or a future switch to a GitHub flake input.

## Build

Build the core library and generated SDK headers:

```bash
cd core
nix build .#lib
```

Build dev packages for LogosBasecampDev:

```bash
cd core
nix build .#lgx --impure -o result-polling-core-lgx-dev

cd ../ui
nix build .#lgx --impure -o result-polling-ui-lgx-dev
```

Build portable packages for distributed Basecamp builds:

```bash
cd core
nix build .#lgx-portable --impure -o result-polling-core-lgx

cd ../ui
nix build .#lgx-portable --impure
```

## Architecture Notes

The UI and core are separate Logos modules and run in separate processes. The QML frontend calls backend methods through the Logos bridge:

```qml
logos.callModuleAsync("polling_core", "submitVote", [option], function(result) {
    // update QML state from result
})
```

Backend methods that are available to the UI are marked `Q_INVOKABLE`.

Core modules must assign the global `logosAPI` pointer in `initLogos`; storing the pointer only on the plugin instance prevents the Logos provider from dispatching remote method calls.

The app does not need a blockchain node for local voting. Real peer-to-peer propagation does require Delivery networking to be able to reach peers or relays, so WSL2 networking and Windows Firewall can still affect multi-instance or cross-machine tests.
