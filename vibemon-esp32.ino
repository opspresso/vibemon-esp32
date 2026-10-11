#include <Arduino.h>
#include <esp_heap_caps.h>
#include "src/platform/esp32_display.h"
#include "src/platform/esp32_services.h"
#include "src/platform/serial_console.h"
#include "src/presentation/dashboard_ui.h"
#include "src/version.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "VibeMon Touch requires the ESP32-4848S040 (ESP32-S3) build profile."
#endif

namespace {
vibemon::EspDisplay display;
vibemon::NvsSettings settings;
vibemon::EspWifi wifi;
vibemon::EspCloud cloud;
vibemon::Controller controller(settings, wifi, cloud);
vibemon::DashboardUi ui(controller);
vibemon::SerialConsole console(controller, ui, display);
bool ready = false;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("VibeMon Touch %s / ESP32-4848S040\n", VIBEMON_VERSION);
  if (!psramFound() || ESP.getPsramSize() < 8 * 1024 * 1024) {
    Serial.println("Startup failed: select 8 MB OPI PSRAM in the board profile.");
    return;
  }
  if (!display.begin()) { Serial.println("Startup failed: LCD or touch initialization."); return; }
  wifi.begin();
  if (!cloud.begin()) { Serial.println("Startup failed: HTTPS worker allocation."); return; }
  // Initialize drivers first. Application strings, catalogs and font caches then prefer PSRAM,
  // preserving internal memory for DMA, FreeRTOS stacks and Wi-Fi's capability-specific allocations.
  heap_caps_malloc_extmem_enable(0);
  controller.begin(millis());
  if (!ui.begin()) { Serial.println("Startup failed: UI font allocation."); return; }
  ready = true;
  Serial.printf("Ready / internal heap %lu / PSRAM free %lu\n", static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getFreePsram()));
}

void loop() {
  if (!ready) { delay(100); return; }
  const auto now = static_cast<std::uint32_t>(millis());
  wifi.tick(now);
  controller.tick(now);
  ui.update(now);
  console.tick();
  lv_timer_handler();
  delay(5);
}
