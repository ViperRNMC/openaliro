# Aliro BLE transport as an ESP-IDF component.
# aliro_ble.c is the NimBLE backend and stays ESP32-only; the interface it
# implements (aliro_ble.h) is shared from modules/woz_aliro so a Zephyr
# bt_gatt_*/bt_l2cap_* backend can be written behind the same header.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

idf_component_register(
	SRCS "aliro_ble.c"
	INCLUDE_DIRS "${ALIRO}/include"
	REQUIRES bt nvs_flash mbedtls
)
