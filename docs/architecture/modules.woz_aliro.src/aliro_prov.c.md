<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_prov.c`

Aliro reader provisioning state: default dev identity, and serialization/deserialization of the
reader identity plus trusted-credential store to/from a self-describing binary blob.
Also implements the trust-store membership check and add-with-dedup operations used to decide
whether a presented credential public key is trusted.

**depends on** [`modules/woz_aliro/include/aliro_prov.h`](../modules.woz_aliro.include/aliro_prov.h.md)  ·  **discussed in** [`ports/esp32-idf/components/aliro_reader/README.md`](../../../ports/esp32-idf/components/aliro_reader/README.md)

<details><summary>Undocumented (7)</summary>

- `aliro_prov_dev_default`
- `aliro_prov_serialize`
- `aliro_prov_deserialize`
- `aliro_reader_identity`
- `aliro_prov_trust_check`
- `aliro_trust_store`
- `aliro_prov_trust_add`

</details>
