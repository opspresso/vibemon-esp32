set(source "${VIBEMON_ROOT}/build/arduino/libraries/ESP32_Display_Panel/src")
file(GLOB_RECURSE sources "${source}/*.cpp" "${source}/*.c")
idf_component_register(SRCS ${sources} INCLUDE_DIRS "${source}" REQUIRES driver esp_lcd esp-lib-utils io_expander)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
