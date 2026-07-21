<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_prov.c`

Aliro reader provisioning state: default dev identity, and serialization/deserialization of the
reader identity plus trusted-credential store to/from a self-describing binary blob.
Also implements the trust-store membership check and add-with-dedup operations used to decide
whether a presented credential public key is trusted.

**depends on** [`modules/woz_aliro/include/aliro_prov.h`](../modules.woz_aliro.include/aliro_prov.h.md)  ·  **discussed in** [`docs/porting-esp32-phase3.md`](../../porting-esp32-phase3.md), [`ports/esp32/components/aliro_reader/README.md`](../../../ports/esp32/components/aliro_reader/README.md)

<details><summary>Undocumented (5)</summary>

- `aliro_prov_dev_default`
- `aliro_prov_serialize`
- `aliro_prov_deserialize`
- `aliro_prov_trust_check`
- `aliro_prov_trust_add`

</details>
