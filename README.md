# P2P Polling for Logos Basecamp

An experimental full-stack Logos module app for local poll voting.

This project is split into two isolated Logos modules:

- `core/` contains `polling_core`, a C++ backend module that tracks vote counts.
- `ui/` contains `polling_ui`, a Qt6/QML frontend module that calls `polling_core` through the Logos IPC bridge.

The first milestone keeps vote state local. The next milestone is wiring vote broadcasts through Logos Delivery once the Delivery module SDK is available as a build input.

## Current Poll

The app supports three options:

- Apples
- Bananas
- Oranges

## Build

Build the core library and generated SDK headers:

```bash
cd core
nix build .#lib
```

Build the UI package:

```bash
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
