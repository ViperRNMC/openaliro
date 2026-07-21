idf_component_register(SRC_DIRS "." "./lock"
                       PRIV_INCLUDE_DIRS
                         "." "./lock" "${ESP_MATTER_PATH}/examples/common/utils")
# NOTE: do not add REQUIRES/PRIV_REQUIRES here. The `main` component implicitly
# depends on every other component, which is how it reaches esp_matter +
# connectedhomeip; declaring an explicit list disables that and breaks the
# includes. The Aliro reader stack (aliro_reader/aliro_ble/woz_uwb) is picked up
# via EXTRA_COMPONENT_DIRS in the top-level CMakeLists, so it is reachable too.

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
