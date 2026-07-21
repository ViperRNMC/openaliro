# Home Assistant bridge

`aliro_mqtt_bridge.py` republishes the lock's console log to MQTT as two Home
Assistant Discovery entities:

| Entity | Platform | Source line |
| --- | --- | --- |
| Distance (mm) | `sensor` | `rng  blk=N d=Xmm  tof=Y` (`modules/woz_uwb/src/ccc/ccc_shim_rx.c:498`) |
| Access | `event`, types `granted` / `denied` | `ACCESS GRANTED` / `ACCESS DENIED` (`access_manager_impl.cpp:756`/`:762`) |

Nothing on the device changes. The bridge only reads the console, so it runs
alongside the existing Apple Home and Wallet setup without taking a Matter
fabric slot.

## The optional `HA=1` firmware build

Separate from the bridge, `HA=1` opts into a firmware variant that exposes the
same information over Matter instead of over the console: a DoorLock
`LockOperation` event, a UWB-proximity occupancy endpoint, and
`CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE`. It needs both halves, because
the bootstrap step is what applies the matching data-model patches:

```
make bootstrap HA=1
make build HA=1
```

**Not hardware-validated.** This has never been run on a board. It changes the
Matter data model of the lock, so an already-commissioned controller may need
the lock re-commissioned to pick the new endpoint up, and
`CONFIG_LOCK_PASS_CREDENTIALS_TO_SET_LOCK_STATE` is disabled upstream pending
connectedhomeip issue 38222 (a TC-DRLK-2.3 certification failure). Default
builds are unaffected: with `HA` unset, neither the patches nor the overlay are
applied. Treat `HA=1` as untested until someone flashes it.

## Prerequisites

The range line is compiled behind `CONFIG_WOZ_PRETTY_SHELL` and gated at runtime
by `uwb_rxdiag_rng_get()`, so issue `aliro frames on` on the shell first.
Without it the access events still flow but distance stays unpublished.

```
pip install paho-mqtt pyserial   # pyserial only for a real serial port
```

## Usage

```
# live, against a broker (--broker defaults to localhost)
aliro_mqtt_bridge.py --port /dev/tty.usbmodem1234 --broker mqtt.example.com

# parse only: no broker, no board, reads a captured log on stdin
aliro_mqtt_bridge.py --port - --dry-run < captured.log
```

`--node` (default `aliro-lock`) sets the MQTT node id and the Home Assistant
device name, so a second lock just needs a different value.

Topics are `aliro/<node>/distance`, `aliro/<node>/access`, and
`aliro/<node>/status` for availability, which is also the last-will topic.
Discovery configs are published retained under `homeassistant/`.

## Notes

Lines matching neither pattern are ignored, including the `DIST tof=… d=…mm`
diagnostic that carries the same distance value: the parser requires the
`rng`/`blk=` prefix, so a block is never counted twice.

`--dry-run` prints each topic and payload instead of connecting, which is the
quickest way to check a parser change against a captured log.
