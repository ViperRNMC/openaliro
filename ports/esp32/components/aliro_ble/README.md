# `aliro_ble` — BLE transport (NimBLE)

Moves bytes between a phone and the reader. Nothing in here knows what Aliro is beyond
the shapes it has to present on the wire: it advertises the service, answers the version
negotiation, and carries the transaction over an L2CAP connection-oriented channel.

The exact wire formats an interoperable reader must present — UUIDs, the SPSM payload
layout, the version-selection write, and the CoC parameters — are in
[`SPEC.md`](SPEC.md), along with the clean-room provenance statement for this component.

## What it provides

- The `0xFFF2` GATT service with two characteristics: a READ returning the reader's SPSM
  and supported protocol versions, and a WRITE where the phone selects one.
- An L2CAP CoC server on a PSM in the range readers use, published as the SPSM in that
  READ.
- Advertising, either as a bare service UUID plus name or as full service data with a
  dynamic resolvable tag once a group resolving key is set.

## Two bring-up modes

Which one you use depends on who owns the BLE stack:

- **Standalone** — `aliro_ble_start()` brings up NimBLE itself. This is what the bench
  app in [`../../apps/reader`](../../apps/reader) uses.
- **Attached** — `aliro_ble_prepare()`, `aliro_ble_service_def()`, and
  `aliro_ble_start_attached()` let the reader register its service on a NimBLE host that
  something else already owns. The Matter app uses this, because esp-matter runs the host
  for commissioning and a second BLE stack instance crashes.

## Seam to the rest of the reader

Inbound SDUs go to a handler as `(conn, data, len)`; replies go out through
`aliro_ble_send`. `aliro_ble_post_reader_status` exists so the grant message that triggers
the Wallet unlock animation can be posted onto the BLE host task from elsewhere, keeping
the sealed-message counter monotonic.

Everything above this component — the transaction, the crypto, the ranging setup — lives
in [`../aliro_reader`](../aliro_reader) and [`../aliro_crypto`](../aliro_crypto).

## Depends on

`bt` (NimBLE), `nvs_flash`, `mbedtls` (AES, for the advertisement tag).
