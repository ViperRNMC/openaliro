# `aliro_reader` — the transaction

The reader's state machine: it takes a connected phone and ends with the UWB responder
listening on negotiated parameters, bound to a key both sides derived independently.

## Flow

```
phone connects (aliro_ble)
      │
      ├─ AUTH0 ─────► ephemeral ECDH, transaction id established
      ├─ AUTH1 ─────► mutual signatures; the secure channel opens here
      │                (a wrong salt transcript fails at exactly this point)
      ├─ trust gate ─ verify the device signature, then check the credential
      ├─ EXCHANGE ──► success, then the reader must announce completion
      │                or the phone stalls and disconnects
      └─ M1-M4 ─────► ranging parameters negotiated; engine starts the DW3000
```

## Files

| File | Role |
|---|---|
| `modules/woz_aliro/src/aliro_reader.c` | The state machine above, plus connection lifecycle and the console-facing provisioning entry points. |
| `modules/woz_aliro/src/aliro_apdu.c` | Pure bytes: BER-TLV, the command builders, the signed-data transcripts, the response parsers, and the L2CAP envelope. No crypto, so it is fully host-testable. |
| `modules/woz_aliro/src/aliro_prov.c` | Reader identity and credential trust store — portable logic, host-tested. |
| `modules/woz_aliro/src/aliro_ranging.c` | Post-auth M1-M4: creates a reader session bound to the derived key, emits M1, routes replies into the engine, and lets the engine start the responder. |
| `aliro_prov_nvs.c` | The NVS load/store behind `aliro_prov.c`. Target-only, so it stays here. |

Everything but the NVS backend is platform-agnostic and lives in `modules/woz_aliro`,
shared source between the targets; this component is the ESP-IDF build wiring plus
that one backend.

## Two things that are easy to get wrong

**The ranging session id is derived, not chosen.** It comes from the transaction id
established at AUTH0. A reader that picks its own gets told the key is unavailable and is
disconnected, even though the key value is correct on both sides.

**M1 is device-initiated.** The engine emits it when the phone asks to start ranging, not
eagerly.

## Threading

Everything — the transaction, the ranging lifecycle, and the engine's transmit and event
callbacks — runs synchronously on the BLE host task. Driving it from another task races
the host. That is why no locking appears in the transaction path; the only mutex guards
provisioning state shared with the console task.

Only one session exists at a time, because the DW3000 has one.

## Identity and trust

The reader loads a stable identity from NVS at start. Without one it falls back to a
fixed, non-secret dev identity and a dev-open trust policy: it accepts the presented
credential and logs a warning. That is a bench seam where real issuer-chain validation
belongs, not a security control. A per-boot random key is specifically what this replaced,
because it changed the reader's identity on every reboot and invalidated the phone's
stored credential.

Console commands `aliro-prov` and `aliro-trust` drive it by hand. Matter's
`SetAliroReaderConfig` writes the same blob.

## Depends on

`aliro_ble`, `aliro_crypto`, `woz_uwb`, `nvs_flash`.
