<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_apdu.h`

APDU framing and parsing for the Aliro Access Protocol: builds outbound command APDUs via a
TLV writer and parses the AUTH0/AUTH1 response APDUs exchanged during the reader-device
handshake.

**used by** [`modules/woz_aliro/src/aliro_apdu.c`](aliro_apdu.c.md), [`modules/woz_aliro/src/aliro_reader.c`](aliro_reader.c.md)

## API

### `int aliro_tlv_w_finish(struct aliro_tlv_w *w, size_t *out_len)`
`modules/woz_aliro/src/aliro_apdu.h:72`

zero length

### `struct aliro_auth0_response *r);`
`modules/woz_aliro/src/aliro_apdu.h:121`

Response payload for an Aliro AUTH0 APDU exchange.

### `struct aliro_auth1_response *r);`
`modules/woz_aliro/src/aliro_apdu.h:131`

Response payload for an Aliro AUTH1 APDU exchange.
