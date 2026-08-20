# minbot

Minecraft Java Edition bot built from scratch to explore networking, binary
protocols, and autonomous agents.

> [!IMPORTANT]
> minbot is in early development. The protocol client and gameplay features are
> not implemented yet.

## Goal

Build a small, understandable Minecraft bot without high-level bot libraries.
The project will implement the important protocol pieces directly and document
the engineering decisions along the way.

## Planned features

- TCP connection and Minecraft handshake
- VarInt and packet serialization
- Server status and ping
- Login to a local `offline-mode` test server
- Keep-alive, chat, and basic movement
- World state and simple pathfinding
- Automated tests and continuous integration

## Principles

- No Mineflayer or similar high-level bot frameworks
- Small modules with clear responsibilities
- Tests for binary encoding and packet parsing
- Honest documentation: planned work is never presented as finished
- Testing only on private servers or servers where bots are allowed

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md) for the development milestones.

## Building

Build instructions will be added with the first working protocol milestone.

## License

This project is released under the [MIT License](LICENSE).
