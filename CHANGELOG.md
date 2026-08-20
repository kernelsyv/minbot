# Changelog

All notable changes to minbot are recorded in this file.

## [0.1.2] - 2026-08-20

### Added

- Minecraft packet compression negotiation and zlib framing.
- Support for compressed and below-threshold uncompressed packets.
- Official zlib 1.3.2 fallback dependency pinned by SHA-256.
- `login`, `register`, and register-then-login authentication modes.
- Custom command templates with `{username}` and `{password}` placeholders.
- Hidden password prompt and `MINBOT_AUTH_PASSWORD` for unattended tests.
- Integration coverage for compressed login, configuration, gameplay, and
  server authentication commands.

### Security

- Authentication passwords are not accepted as command-line arguments and are
  never printed by minbot.
- Compressed and uncompressed packet sizes remain limited to prevent oversized
  allocations.

## [0.1.1] - 2026-08-20

### Added

- Offline-mode login for Minecraft Java 1.21.1.
- Login, configuration, and play protocol state machine.
- Client settings, keep-alive, ping, teleport, and chunk-batch responses.
- Interactive outgoing chat and unsigned commands.
- Incoming plain player-chat events.
- Loaded/unloaded chunk coordinate tracking.
- Shared cross-platform TCP connection layer with a 16 MiB packet limit.
- End-to-end mock-server test for a complete gameplay session.

### Known limitations

- Requires `network-compression-threshold=-1`.
- Does not implement online-mode encryption or Microsoft authentication.
- Does not decode block palettes or render NBT chat components yet.

## [0.1] - 2026-08-20

### Added

- Cross-platform C++20 project and command-line interface.
- Minecraft Java Edition 1.21.1 target with protocol version 767 by default.
- Minecraft VarInt, string, integer, and packet framing primitives.
- Java Edition server handshake and status request.
- Ping/pong latency measurement.
- Defensive packet size and parsing limits.
- Protocol unit tests for valid and malformed input.
- End-to-end status test against a local mock server.
- Windows and Linux continuous integration workflow.
