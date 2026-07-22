# Aliro key schedule + crypto primitives as an ESP-IDF component.
# The sources are the shared ones in modules/woz_aliro (built for the nRF side
# by that module's CMakeLists.txt); nothing here is ESP32-specific except the
# mbedTLS-PSA provider that backs aliro_prim_psa.c.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ALIRO "${REPO_ROOT}/modules/woz_aliro")

idf_component_register(
	SRCS
		"${ALIRO}/src/aliro_hash.c"
		"${ALIRO}/src/aliro_crypto.c"
		"${ALIRO}/src/aliro_advtag.c"
		"${ALIRO}/src/aliro_prim_psa.c"
	INCLUDE_DIRS "${ALIRO}/include"
	PRIV_INCLUDE_DIRS "${ALIRO}/src"
	REQUIRES mbedtls
)
