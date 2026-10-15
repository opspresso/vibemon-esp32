file(GLOB_RECURSE sources "${VIBEMON_ROOT}/src/*.cpp")
idf_component_register(SRCS main.cpp ${sources}
  INCLUDE_DIRS "${VIBEMON_ROOT}" "${VIBEMON_ROOT}/src" "${VIBEMON_ROOT}/build/generated"
    "${VIBEMON_ROOT}/build/arduino/libraries/ArduinoJson/src"
  REQUIRES arduino lvgl esp_panel esp_netif esp_wifi esp_event lwip esp_psram nvs_flash app_update mbedtls)
