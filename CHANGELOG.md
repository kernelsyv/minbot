# Changelog

All notable changes to minbot are recorded in this file.

## [0.1.5] - 2026-08-21

### Added

- Player position state with absolute and relative server teleport handling.
- Local `/pos`, `/move`, `/look`, and `/jump` terminal controls.
- Serverbound position and look packets for protocol 767.
- Unit tests for control parsing and floating-point protocol primitives.
- Integration coverage for movement packets, relative teleports, and rate
  limiting.

### Safety

- Reject non-finite and out-of-range coordinates and pitch values.
- Space outgoing movement packets by at least 50 milliseconds.

### Known limitations

- Jumping is a packet-level upward step without gravity or collision checks.
- Navigation, block collision, and pathfinding are not implemented yet.

## [0.1.4] - 2026-08-21

### Changed

- Reworked all README diagrams as restrained technical-documentation figures.
- Replaced gradients, glows, decorative cards, and icon-heavy styling with a
  neutral paper palette, direct labels, and consistent relationship lines.
- Added explicit diagram scope, source version, protocol labels, and text
  descriptions for accessibility and maintenance.

## [0.1.3] - 2026-08-21

### Added

- Minimalist system architecture diagram for the README.
- Visual connection lifecycle from TCP setup to the active play loop.
- Incoming packet pipeline diagram covering framing, compression, dispatch,
  and state updates.

### Changed

- Reorganized the README so the implementation is understandable before the
  build and usage instructions.

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
