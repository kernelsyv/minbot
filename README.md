# minbot

Minecraft Java Edition protocol client built from scratch in C++20. The project
explores binary protocols, networking, and autonomous bot behavior without
Mineflayer or another high-level bot framework.

**Current version: `0.1` · Target: Minecraft Java `1.21.1` (protocol `767`)**

## What works in 0.1

- Cross-platform TCP connection using system sockets
- Minecraft VarInt and packet framing
- Handshake and server status request
- Raw status JSON output
- Ping/pong latency measurement
- Defensive parsing and protocol unit tests
- Windows and Linux CI

Gameplay login, movement, chat, and pathfinding are later milestones. The
roadmap never presents planned features as finished.

## Build

You need CMake 3.20 or newer and a C++20 compiler.

```bash
cmake -S . -B build -DMINBOT_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is normally located at `build/minbot` on single-configuration
generators or `build/Release/minbot.exe` with Visual Studio.

## Usage

```text
minbot --version
minbot status <host> [port] [protocol-version]
```

Example for a local test server:

```bash
minbot status localhost 25565
```

The protocol version defaults to `767` for Minecraft Java 1.21.1. The final
argument can still override it when testing another server version.

## Project documentation

- [Protocol notes](docs/PROTOCOL.md)
- [Development roadmap](docs/ROADMAP.md)
- [Versioning policy](docs/VERSIONING.md)
- [Changelog](CHANGELOG.md)

## Responsible use

Run minbot only on private servers or servers where automated clients are
allowed. Version 0.1 only reads public server-list status data and does not log
in as a player.

## License

This project is released under the [MIT License](LICENSE).
