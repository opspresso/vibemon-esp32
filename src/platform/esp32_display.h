#pragma once
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace vibemon {
class EspDisplay {
 public:
  bool begin();
  std::uint32_t touchPresses() const { return touchPresses_; }
  std::uint32_t completedFrames() const { return completedFrames_; }
  bool setPixelClock(std::uint32_t hz);
  std::uint32_t pixelClock() const { return pixelClockHz_; }
 private:
  esp_panel::board::Board board_;
  lv_display_t* display_ = nullptr;
  lv_indev_t* touch_ = nullptr;
  StaticSemaphore_t frameReadyStorage_{};
  SemaphoreHandle_t frameReady_ = nullptr;
  volatile std::uint32_t completedFrames_ = 0;
  std::uint32_t pixelClockHz_ = 16000000;
  static bool IRAM_ATTR frameComplete(void* context);
  std::uint32_t touchPresses_ = 0;
  bool pressed_ = false;
};
}  // namespace vibemon
