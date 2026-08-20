# Protocol notes for minbot 0.1

minbot 0.1 implements the status state of the Minecraft Java Edition protocol.
It deliberately avoids high-level bot libraries.

## Request flow

1. Resolve the server hostname and open a TCP connection.
2. Send a handshake packet with `next state = 1` (status).
3. Send the empty status request packet.
4. Read the server's length-prefixed JSON response.
5. Send an eight-byte ping payload and verify the echoed pong.

## Packet framing

Each packet starts with a VarInt byte length. The framed body begins with a
VarInt packet ID followed by packet-specific fields.

The status handshake contains:

- packet ID `0x00`;
- protocol version as a signed VarInt;
- server address as a VarInt-length-prefixed UTF-8 string;
- server port as an unsigned 16-bit big-endian integer;
- next state `0x01`.

## Safety limits

The network reader rejects negative, empty, truncated, overflowing, and larger
than 4 MiB packets. A malformed server response becomes a normal error instead
of an out-of-bounds read or unbounded allocation.

## Version compatibility

minbot 0.1 targets Minecraft Java Edition 1.21.1 and sends protocol version
`767` by default. The status flow has remained stable across many Java Edition
releases, so another protocol number can still be passed as the final CLI
argument for testing.
