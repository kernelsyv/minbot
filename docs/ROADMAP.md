# minbot roadmap

Each milestone should produce a small working demo and tests before the next
one begins.

## 0. Project foundation

- [x] Repository structure
- [x] README, roadmap, license, and ignore rules
- [ ] Choose the implementation language and target Minecraft version
- [ ] Add a build system and continuous integration

## 1. Binary protocol basics

- [ ] Byte buffer abstraction
- [ ] VarInt encode and decode
- [ ] Length-prefixed UTF-8 strings
- [ ] Unit tests, including invalid and truncated input

## 2. Server status client

- [ ] TCP connection
- [ ] Handshake packet
- [ ] Status request and JSON response
- [ ] Ping and latency measurement

## 3. Login and sessions

- [ ] Login state packet loop
- [ ] Compression support
- [ ] Offline-mode local server login
- [ ] Keep-alive handling

## 4. Bot behavior

- [ ] Position and movement
- [ ] Chat commands
- [ ] Basic world representation
- [ ] Simple pathfinding

## Later ideas

- Microsoft account authentication
- Inventory and block interaction
- Navigation experiments and visual debugging
