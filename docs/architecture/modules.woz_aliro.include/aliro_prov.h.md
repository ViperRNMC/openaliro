<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_prov.h`

Persistent reader provisioning storage: identity and credential trust anchors saved to and
loaded from NVS.
Declares aliro_prov_store for committing an identity/trust pair to NVS, and struct
aliro_trust_store, the set of trusted credential public keys against which a presented
credential is authenticated.

**used by** [`modules/woz_aliro/src/aliro_prov.c`](../modules.woz_aliro.src/aliro_prov.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

## API

### `int aliro_prov_store(const struct aliro_reader_identity *id, const struct aliro_trust_store *ts)`
`modules/woz_aliro/include/aliro_prov.h:108`

Persist identity+trust to NVS. 0 on success, negative on an NVS error.

### `struct aliro_trust_store`
`modules/woz_aliro/include/aliro_prov.h:109`

Trusted credential public keys. A presented credential authenticates only if
its key is in here (or the store is empty and dev policy allows it). A raw-key
allowlist is the interim seam; real issuer-chain validation is the Phase-4
refinement that plugs in at aliro_prov_trust_check.
