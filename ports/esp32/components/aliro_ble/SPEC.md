# aliro_ble — Aliro BLE transport (NimBLE), clean-room reimplementation

The wire contract this component implements. For what the component is and how to
use it, see [`README.md`](README.md).

This is the BLE side of the Aliro reader: it advertises the Aliro service, lets a
phone negotiate the BLE-UWB protocol version, and carries the Aliro transaction over
an L2CAP connection-oriented channel. The transaction that rides on it ends with a
ranging key and negotiated parameters entering the UWB engine through
`woz_uwb_start_aliro()`.

## Provenance (clean-room)

Behavior and the Aliro-spec constants below were derived by *studying* the
Nordic reference (`ncs-door-lock-and-access-control`, `LicenseRef-Nordic-5-Clause`,
Nordic-device-restricted, study-only). No source was copied. The UUIDs, PSM range,
and wire formats are Aliro-protocol facts an interoperable reader must match; the
implementation is original and MIT/ISC-clean per this project's provenance
discipline. Do not paste Nordic code here.

## Wire protocol (what an interoperable reader must present)

GATT service `0xFFF2` (16-bit Aliro service UUID), two characteristics:

- **Reader SPSM + BLE-UWB protocol version** — UUID
  `D3B5A130-9E23-4B3A-8BE4-6B1EE5F980A3`, READ. Returns:

  ```
  [SPSM              : uint16 big-endian]   # L2CAP PSM to connect to (0x0080..0x00FF)
  [protoVersionsLen  : uint8]               # = 2 * number_of_versions
  [protoVersions     : uint16 big-endian * N]
  [featuresLen       : uint8]               # = sizeof(features blob)
  [features          : featuresLen bytes]
  ```

- **User-device selected BLE-UWB protocol version** — UUID
  `BD4B9502-3F54-11EC-B919-0242AC120005`, WRITE. Accepts:

  ```
  [version   : uint16 big-endian]
  [featLen   : uint8]
  [features  : featLen bytes]
  ```
  Minimum length 3. The reader validates `version` is one it advertised; on a
  match it records the version for that connection, otherwise ignores it.

`features` flags (both directions): `TimesyncProcedure0`, `TimesyncProcedure1`,
`LeCodedPhy`.

## L2CAP transaction

- Reader registers an L2CAP CoC server on a PSM in the reader range
  `0x0080..0x00FF` and publishes that value as the SPSM in the READ characteristic
  above.
- SDU MTU: >= 267 TX / >= 264 RX.
- Inbound SDUs are handed to the Aliro message handler as `(conn, data, len)`;
  replies go out via a `send(conn, data, len)`. The whole credential-auth and
  M1-M4 exchange sits on top of exactly those two calls and nothing else in this
  component.

## Seam to the engine

On a completed handshake the reader fills a `struct woz_uwb_aliro_cfg` (URSK +
channel/sync-code/slots/STS index) and the engine starts the responder. This
component knows nothing about UWB; it only moves bytes.
