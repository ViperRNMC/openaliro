<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_crypto.h`

Aliro crypto public API: key derivation, AES-GCM secure channels, and wire message
seal/open framing shared by the reader and device sides of an Aliro session.

**used by** [`modules/woz_aliro/src/aliro_crypto.c`](../modules.woz_aliro.src/aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

## API

### `int aliro_msg_open(struct aliro_secchan *sc, const uint8_t *wire, size_t wire_len, uint8_t *plain, size_t plain_cap, size_t *plain_len)`
`modules/woz_aliro/include/aliro_crypto.h:150`

Inverse of aliro_msg_seal: open a wire SDU into the engine plaintext form,
verifying the tag. Returns <0 on a tag mismatch (drop the connection then).
