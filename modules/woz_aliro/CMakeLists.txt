# Woz Aliro credential-auth stack as a Zephyr module.
# Contributes nothing unless CONFIG_WOZ_ALIRO_STACK=y (stock nRF build
# byte-identical). The same src/ is compiled by the ESP-IDF ports through
# ports/esp32/components/aliro_{crypto,reader}/CMakeLists.txt.
#
# Every source here is platform-agnostic: the k_*/LOG_* calls are Zephyr's own
# API, which the ESP-IDF side backs with the woz_uwb compat headers. What is NOT
# here is the BLE transport (aliro_ble.h has no Zephyr backend yet) and the
# aliro_prov storage backend, so linking this alone does not yield a reader.

if(CONFIG_WOZ_ALIRO_STACK)

  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  zephyr_library_named(woz_aliro)

  # Key schedule + the one AEAD/EC backend. aliro_prim_psa.c is pure PSA Crypto,
  # which NCS provides via nrf_security/CryptoCell on the nRF5340's Cortex-M33.
  zephyr_library_sources(
    ${SRC}/aliro_hash.c
    ${SRC}/aliro_crypto.c
    ${SRC}/aliro_prim_psa.c
    # BER-TLV / ISO7816 / BLE-envelope wire codec.
    ${SRC}/aliro_apdu.c
    # Reader identity + raw-key trust store (storage backend is per-platform).
    ${SRC}/aliro_prov.c
  )

  # NOT listed yet: src/aliro_reader.c and src/aliro_ranging.c. They no longer
  # contain any ESP-IDF call (both are now written to Zephyr's k_*/LOG_* API),
  # but they have never been compiled by a Zephyr toolchain, and aliro_ranging.c
  # additionally needs modules/woz_uwb's facade/cherry/adapter headers on the
  # include path. Add them here in the same change that first builds them.

  zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SRC}
  )

  zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

endif()
