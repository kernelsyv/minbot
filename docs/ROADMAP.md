# minbot roadmap

Each milestone should produce a small working demo and tests before the next
one begins.

## 0. Project foundation

- [x] Repository structure
- [x] README, roadmap, license, and ignore rules
- [x] Choose C++20 and target Minecraft Java 1.21.1 (protocol 767)
- [x] Add a CMake build and Windows/Linux continuous integration

## 1. Binary protocol basics

- [x] Byte buffer abstraction
- [x] VarInt encode and decode
- [x] Length-prefixed UTF-8 strings
- [x] Unit tests, including invalid and truncated input

## 2. Server status client

- [x] TCP connection
- [x] Handshake packet
- [x] Status request and JSON response
- [x] Ping and latency measurement

## 3. Login and sessions

- [x] Login state packet loop
- [x] Compression support
- [x] Offline-mode local server login
- [x] Configuration state and client settings
- [x] Keep-alive handling

## 4. Bot behavior

- [x] Position acknowledgement
- [ ] Movement packets and physics
- [x] Chat messages and unsigned commands
- [x] Configurable register/login command sequences
- [x] Basic loaded-chunk representation
- [ ] Chunk section and block-palette decoding
- [ ] NBT chat-component rendering
- [ ] Simple pathfinding

## Later ideas

- Microsoft account authentication
- Inventory and block interaction
- Navigation experiments and visual debugging
