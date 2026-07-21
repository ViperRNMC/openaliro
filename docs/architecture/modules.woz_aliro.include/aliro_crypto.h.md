<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_crypto.h`

Aliro crypto public API: key derivation, AES-GCM secure channels, and wire message
seal/open framing shared by the reader and device sides of an Aliro session.

**used by** [`modules/woz_aliro/src/aliro_crypto.c`](../modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

## API

### `struct aliro_secchan`
`modules/woz_aliro/include/aliro_crypto.h:134`

---- Secure channel (AES-256-GCM, directional per-message counters) ------
Nonce = 8-byte big-endian direction (0 outbound/seal, 1 inbound/open) followed
by a 4-byte big-endian per-direction counter. Separate seal/open counters,
start at 0, no wrap. SessionCrypto sends no AAD; the BLE channel authenticates
a 4-byte AAD (caller-supplied here).
