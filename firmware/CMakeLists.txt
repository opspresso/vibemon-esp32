cmake_minimum_required(VERSION 3.22)
get_filename_component(VIBEMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IDF_TARGET esp32s3)
set(EXTRA_COMPONENT_DIRS
  "${VIBEMON_ROOT}/build/sdk-components/arduino"
  "${VIBEMON_ROOT}/build/arduino/libraries/lvgl"
  "${VIBEMON_ROOT}/build/arduino/libraries/esp-lib-utils")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include_directories("${VIBEMON_ROOT}")
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE=1 LV_KCONFIG_IGNORE=1
  ESP_PANEL_KCONFIG_IGNORE=1 ESP_PANEL_BOARD_FILE_SKIP=1
  ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1 BOARD_JINGCAI_ESP32_4848S040C_I_Y_3=1
  ESP_UTILS_KCONFIG_IGNORE=1)
file(STRINGS "${VIBEMON_ROOT}/src/version.h" version_define REGEX "^#define VIBEMON_VERSION")
string(REPLACE "#define VIBEMON_VERSION " "" PROJECT_VER "${version_define}")
string(REPLACE "\"" "" PROJECT_VER "${PROJECT_VER}")
project(vibemon_touch)
