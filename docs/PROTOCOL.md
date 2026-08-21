# Protocol notes for minbot 0.1.5

minbot implements selected parts of Minecraft Java Edition 1.21.1 protocol
`767` directly in C++20. It does not use Mineflayer or another bot framework.

## Status flow

1. Open a TCP connection.
2. Send handshake packet `0x00` with `next state = 1`.
3. Send status request `0x00` and read the JSON response.
4. Send ping `0x01` and verify the echoed 64-bit value.

## Gameplay flow

1. Send handshake `0x00` with `next state = 2`.
2. Send Login Start with a deterministic local UUID and username.
3. Read login packets until Login Success, then acknowledge it.
4. Send client settings and process configuration packets.
5. Reply to configuration keep-alive, ping, cookie, and known-pack requests.
6. Acknowledge Finish Configuration and enter the play state.
7. Process play packets until the user enters `/quit` or the server disconnects.

The play loop currently responds to keep-alive (`0x26`), position (`0x40`), and
chunk-batch-finished (`0x0c`). It tracks map-chunk (`0x27`) and unload-chunk
(`0x21`) coordinates. It sends chat with serverbound packet `0x06` and unsigned
commands with `0x04`.

## Player position and movement

Clientbound position packet `0x40` initializes or updates the local player
state. minbot reads all five relative flags, applies them to the previous
`x/y/z/yaw/pitch` state, and acknowledges the teleport ID with packet `0x00`.

Local controls use these serverbound play packets:

- position `0x1a`: three 64-bit coordinates and `onGround`;
- look `0x1c`: 32-bit yaw and pitch plus `onGround`.

`/jump` sends two position packets: an airborne position 0.42 blocks higher,
then a landing position at the previous height. This is deliberately described
as basic movement rather than physics: it does not inspect collision shapes,
apply gravity, or negotiate anti-cheat behavior.

## Packet framing and compression

Before compression is negotiated, every packet starts with a VarInt body
length. The body begins with a VarInt packet ID followed by packet-specific
fields. Integer and floating point fields use network byte order where the
protocol requires it.

After the login Set Compression packet, minbot stores the server threshold.
Each following frame contains an outer packet length and a VarInt uncompressed
length. A zero uncompressed length means the remaining bytes are below the
threshold and are sent directly. A non-zero value means the remaining bytes are
a zlib stream. minbot validates both wire and expanded sizes.

## Scope of chunk processing

minbot reads the `x` and `z` fields from chunk packets and maintains a
map of currently loaded chunks. It stores the received packet size as a useful
debugging summary. Heightmaps, NBT, section palettes, light arrays, biomes, and
individual block states remain opaque bytes for now.

That boundary keeps this release testable: “chunk received at `(x, z)`” is a
working feature, while “the bot knows every block” is still a roadmap item.

## Offline-mode compatibility

minbot supports Set Compression but still rejects Encryption Request.
It targets offline-mode servers with:

```properties
online-mode=false
enforce-secure-profile=false
```

The normal `network-compression-threshold` value can remain enabled.
Authenticated Microsoft account sessions are future work.

The packet layouts and IDs were checked against the public
[Minecraft data for protocol 767](https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.21.1/protocol.json).

## Safety limits

The network reader rejects negative, empty, truncated, overflowing, and larger
than 16 MiB packets. Strings also have explicit limits. Malformed server data
becomes a normal error instead of an out-of-bounds read or unbounded allocation.
Movement rejects non-finite coordinates, bounds horizontal coordinates to
±30,000,000, bounds vertical coordinates to ±2,048, constrains pitch to
-90..90 degrees, and spaces movement packets by at least 50 milliseconds.
