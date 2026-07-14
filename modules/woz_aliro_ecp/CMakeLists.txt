# Woz Aliro ECP (Apple Enhanced Contactless Polling) emitter — out-of-tree
# Zephyr module. Implements the RFAL proprietary-poll hook (NfcPropInit /
# NfcPropGetCallbacks) that the add-on's subsys/nfc_prop declares, so a matching
# iPhone auto-presents its Aliro pass (Express / no Face ID). Built only when the
# add-on's CONFIG_DOOR_LOCK_NFC_PROP is set; compiles against the add-on's RFAL,
# nfc_prop, reader_storage and aliro headers (global Zephyr include dirs).
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_DOOR_LOCK_NFC_PROP src/nfc_prop_ecp.cpp)
