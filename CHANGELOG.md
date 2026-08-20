# Changelog

All notable changes to minbot are recorded in this file.

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
