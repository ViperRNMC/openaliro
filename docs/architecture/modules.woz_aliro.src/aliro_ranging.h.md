<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_ranging.h`

Aliro M1-M4 ranging-setup interface: negotiates UWB ranging parameters with the device and
produces the BLE ranging-control secure channel used to carry the M1-M4 exchange.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](aliro_reader.c.md)

## API

### `struct aliro_secchan *sc_ble);`
`modules/woz_aliro/src/aliro_ranging.h:46`

Output secure channel for the BLE ranging control channel (M1-M4), populated alongside the AP secure channel during Aliro authentication.
