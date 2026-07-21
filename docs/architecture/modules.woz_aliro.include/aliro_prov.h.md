<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_prov.h`

Persistent reader provisioning storage: identity and credential trust anchors saved to and
loaded from NVS.
Declares aliro_prov_store for committing an identity/trust pair to NVS, and struct
aliro_trust_store, the set of trusted credential public keys against which a presented
credential is authenticated.

**used by** [`modules/woz_aliro/src/aliro_prov.c`](../modules.woz_aliro.src/aliro_prov.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)  ·  **discussed in** [`docs/porting-esp32-phase3.md`](../../porting-esp32-phase3.md)

## API

### `struct aliro_reader_identity`
`modules/woz_aliro/include/aliro_prov.h:43`

The reader's provisioned identity. reader_id rides AUTH0 and both ECDSA
transcripts (tag 0x4D); sign_priv signs the reader-usage transcript. is_dev
marks the built-in bench identity, never a real deployment.

### `struct aliro_trust_store`
`modules/woz_aliro/include/aliro_prov.h:56`

Trusted credential public keys. A presented credential authenticates only if
its key is in here (or the store is empty and dev policy allows it). A raw-key
allowlist is the interim seam; real issuer-chain validation is the Phase-4
refinement that plugs in at aliro_prov_trust_check.
