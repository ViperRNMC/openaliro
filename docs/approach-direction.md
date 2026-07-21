# Approach Direction

How the Apple Home app's "Approach Direction" control is exposed by an Aliro lock, and
every trap hit making it appear on the ESP32-S3 port. Validated on silicon 2026-07-21:
the control renders and its Left/Front/Right selection round-trips to the device.

> Convention: **VERIFIED** = confirmed on silicon or against SDK source; **INFERRED** =
> consistent with observation but not directly proven.

---

## 1. It is not a standard Matter attribute

**VERIFIED.** No attribute of the standard Door Lock cluster (`0x0101`) carries approach
direction, through spec revision 1.6 — not in the Aliro attribute block (`0x0080`–
`0x0088`), not anywhere else. Searching the standard cluster for it will always come up
empty, and that emptiness is not evidence the feature does not exist.

The control is driven by a **manufacturer-specific cluster** using a Matter MEI
(Manufacturer Extensible Identifier): vendor ID in the upper 16 bits, cluster ID in the
lower 16. Manufacturer clusters live in a separate ID space that a search of the standard
data model cannot reach.

Vendor `0x1349` is Apple (`CHIPVendorIdentifiers.hpp:46`), so the cluster is
**`0x1349FC03`**.

## 2. The descriptor

**VERIFIED** on silicon — the control renders and its selection round-trips with exactly
these values — and against the type tables in `chip-types.xml` and
`attribute-metadata.h`.

Cluster `0x1349FC03`, mask `0x40` (server), on the **door lock endpoint** — the same
endpoint as the Door Lock cluster, not endpoint 0. Three attributes, 7 bytes total:

| Attribute | Size | Type | Mask | Default |
|---|---|---|---|---|
| `0x0000` direction | 1 | `0x18` bitmap8 | `0x03` writable\|nonvolatile | 7 |
| `0xFFFC` FeatureMap | 4 | `0x1B` bitmap32 | 0 | 0 |
| `0xFFFD` ClusterRevision | 2 | `0x21` int16u | 0 | 1 |

Type codes per `chip-types.xml` lines 28/30/33; mask bits per
`attribute-metadata.h:110,112` (`MATTER_ATTRIBUTE_FLAG_WRITABLE` = `0x01`,
`..._NONVOLATILE` = `0x02`).

Two things are easy to get wrong here:

- The direction attribute is a **bitmap8**, not `int8u`. Declaring it as an unsigned
  integer is the wrong type code (`0x20` instead of `0x18`).
- The default of **7** is `0b111`, all three directions permitted, matching Home's
  "unlock when you approach from any direction". Which single bit means Left versus
  Right is **still unknown** and nothing in the port depends on it.

## 3. The expensive one: missing global attributes fail silently, then loudly

**VERIFIED, cost three pairing cycles.**

`FeatureMap` and `ClusterRevision` are mandatory on every Matter cluster.
`esp_matter`'s `cluster::create()` emits **neither** — every generated cluster in the SDK
adds them explicitly (see any file under `data_model/generated/clusters/`).

Declaring the cluster with only the direction attribute produces a cluster that cannot
answer `ClusterRevision`. The failure does not look like a failure:

1. Home runs a **complete** commissioning — PASE, attestation, CSR request, AddNOC,
   `CommissioningComplete` — on two fabrics
2. Roughly 0.5 s later it sends `RemoveFabric` for its own fabric
3. The UI says **"Unable to Add Accessory"**

Nothing in the device log errors. The read Home makes immediately before bailing
*succeeds*, so the rejection is based on a value it received, not on an error status.

**How to recognise it:** search the log for `OpCreds: Received a RemoveFabric Command`
appearing *after* a successful `Commissioning completed successfully`. That sequence
means the accessory was accepted and then rejected, which is a data-model problem, not a
connectivity or crypto problem.

**Fix:**

```c
cluster_t *c = cluster::create(endpoint, kApproachDirectionClusterId, CLUSTER_FLAG_SERVER);
attribute::create(c, 0x0000, ATTRIBUTE_FLAG_WRITABLE | ATTRIBUTE_FLAG_NONVOLATILE,
                  esp_matter_bitmap8(7));
cluster::global::attribute::create_feature_map(c, 0);
cluster::global::attribute::create_cluster_revision(c, 1);
```

`create_feature_map` / `create_cluster_revision` are declared in
`generated/esp_matter_data_model_utils.h:62`, already pulled in by `esp_matter.h:25`.

## 4. The endpoint descriptor is cached at commissioning

**VERIFIED.** A controller reads the endpoint's cluster list once, when the accessory is
commissioned, and caches it. Adding a cluster to a device that is already paired changes
nothing visible — the control will not appear no matter how many times the app is
restarted.

Any newly declared cluster requires **removing the accessory and adding it again**.

## 5. Removal and re-pairing traps

Three separate ways to end up unable to re-add a device, all seen on the bench:

**5.1 Removing while the device is unreachable.** Removing an accessory sends
`RemoveFabric` *to the device*. If the device is offline at that moment, the controller
removes it locally and the device never hears about it — it still boots with
`Fabric already commissioned. Disabling BLE advertisement` and cannot be discovered.

**5.2 Multiple fabrics.** A device commissioned into Apple Home typically holds **two**
fabrics. Removing the accessory in the app clears one; the device stays commissioned on
the other, so it still will not advertise.

For both, the fix is a device-side factory reset, which clears every fabric at once. On
the ESP32 port: `matter> factoryreset`. If the console is unresponsive, erase only the
`nvs` partition rather than the whole flash, so `esp_secure_cert` and `fctry` survive.

**5.3 Trust-store exhaustion.** Tangential but it will bite during repeated pairing
cycles. A Matter factory reset does not touch the reader's own provisioning namespace,
and nothing evicts superseded credentials, so each re-pair burns a slot. Once the store
is at `ALIRO_TRUST_MAX`, the reader verifies the phone's signature and *then* rejects the
credential:

```
device signature OK
credential key NOT trusted (not in trust store); rejecting
```

`aliro trust` reports `FAILED (trust store full or NVS error)`, which covers three
distinct causes and does not say which. Run `aliro prov` to see the true count, and
`aliro clear` to empty the store.

## 6. Red herring: cluster `0x1349FC00`

**VERIFIED as a dead end.** During commissioning, Home reads
`clusterId 0x1349FC00, attributeId 0x0001` and the device answers `err = 5c3`.

This looks like a promising lead and is not one. This port renders the Approach Direction
control while still answering `UNSUPPORTED_CLUSTER` for `0x1349FC00`, so that cluster
cannot be what gates the control. Do not spend build cycles guessing at its datatype.

## 7. Decoding the error codes in these logs

Interaction Model status codes appear in device logs as `err = 0x500 + status`:

| Log value | Status | Meaning |
|---|---|---|
| `5c3` | `0xc3` | `UNSUPPORTED_CLUSTER` |
| `586` | `0x86` | `UNSUPPORTED_ATTRIBUTE` |

Per `src/protocols/interaction_model/StatusCodeList.h`. `UNSUPPORTED_CLUSTER` on a
manufacturer cluster during commissioning is normal probing and is not by itself a
problem — the device answers the same way for optional standard clusters it does not
implement, such as ICD Management (`0x0046`).

## 8. The control is cosmetic on single-antenna hardware

**VERIFIED.** Nothing gates an unlock on the stored value. Measuring which direction a
phone approaches from requires angle of arrival, which needs a dual-antenna UWB part.
The DW3110 used on the DWM3000EVB has a single antenna and cannot produce it.

The attribute is stored, reported, and round-trips correctly to the app. The behaviour
behind it is not implemented, and on this hardware cannot be.

## 9. Port coverage

| Port | Status |
|---|---|
| `ports/esp32/apps/matter-lock` | **implemented**: runtime `cluster::create()` in `app_main.cpp` |
| nRF5340 door lock app | **implemented**: static tables, `ports/nrf5340dk/patches/approach-direction-cluster.patch` |

The two ports build their data models differently. The ESP32 port constructs endpoints at
runtime, so the cluster is a few lines of C++. The nRF app's is a set of static tables
compiled into the image.

### 9.1 The nRF data model is not regenerated at build time

**VERIFIED.** It is tempting to assume the `.zap` file is the source of truth and edit it.
It is not, on this build. `ncs_configure_data_model` passes `BYPASS_IDL` to
`chip_configure_data_model`, which takes the branch that *skips* the `chip_zapgen` target
entirely and instead adds the checked-in `zap-generated/` directory as an include path.
`endpoint_config.h` appears nowhere in `build.ninja` as a build rule. Editing `lock.zap`
alone changes nothing in the image.

The variant in use is selected by `src/matter/CMakeLists.txt`: with
`CONFIG_NCS_SAMPLE_MATTER_ZAP_FILE_PATH` unset and `CONFIG_DOOR_LOCK_BLE_UWB=y`, that is
`zap_uwb/`. Read it from a configured build's `.config`, not from Kconfig defaults.

So the cluster is added by patching the generated `endpoint_config.h`: three attribute
records, one cluster record, and the counters that describe them.

### 9.2 The counters must stay consistent with the tables

The static tables are index-addressed, and four `#define`s must agree with them:
`GENERATED_ATTRIBUTE_COUNT`, `GENERATED_CLUSTER_COUNT`,
`ZAP_FIXED_ENDPOINT_DATA_VERSION_COUNT` and `ATTRIBUTE_MAX_SIZE`, plus the per-endpoint
`GENERATED_ENDPOINT_TYPES` row `{ cluster index, cluster count, endpoint size }`.

Two structural rules make the edit an append rather than a renumbering:

- A cluster's attributes must be **contiguous** in the attribute table, and an endpoint's
  clusters contiguous in the cluster table. The door lock endpoint's clusters are already
  last, so a new cluster appends at the end of both tables without moving anything.
- `clusterSize` is the sum of the sizes of that cluster's attributes that are **not**
  `EXTERNAL_STORAGE`, and an endpoint's size is the sum of its clusters' sizes. All three
  new attributes are RAM-backed, so the cluster adds 1 + 4 + 2 = 7 bytes, and both the
  endpoint row and `ATTRIBUTE_MAX_SIZE` grow by 7.

These are checkable rather than guessable: every counter can be re-derived from the tables
in the same file, which is the cheapest way to catch an arithmetic slip before a build.

One thing this path does get for free: `FeatureMap` and `ClusterRevision` are written out
per cluster like any other attribute, so §3 — the trap that cost the most time on the
other port — cannot occur here as an omission by a helper.

### 9.3 It collides with the opt-in Home Assistant patches

`ha-occupancy-endpoint.patch` adds a third endpoint and so bumps the **same** counter
lines. Two patches cannot both change `GENERATED_ATTRIBUTE_COUNT 205`, in either order.
The stack is therefore cumulative: Approach Direction applies first, and the HA patches
are cut against a tree that already has it. `tests/tooling/patch_drift_check.sh` applies
all of them in that order, so a future edit that breaks the composition fails there.
