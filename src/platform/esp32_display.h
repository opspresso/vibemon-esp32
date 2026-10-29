#pragma once
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>
#include "../domain/model.h"

namespace vibemon {
class EspDisplay {
 public:
  bool begin();
  std::uint32_t touchPresses() const { return touchPresses_; }
  std::uint32_t completedFrames() const { return completedFrames_; }
  std::uint32_t redraws() const { return redraws_; }
  std::uint32_t renderedPixels() const { return renderedPixels_; }
  bool setPixelClock(std::uint32_t hz);
  std::uint32_t pixelClock() const { return pixelClockHz_; }
  // Call on the UI task outside lv_timer_handler(). Fails only when the canvas cannot be allocated.
  bool setRotation(ScreenRotation rotation);
 private:
  esp_panel::board::Board board_;
  lv_display_t* display_ = nullptr;
  lv_indev_t* touch_ = nullptr;
  StaticSemaphore_t frameReadyStorage_{};
  SemaphoreHandle_t frameReady_ = nullptr;
  volatile std::uint32_t completedFrames_ = 0;
  std::uint32_t pixelClockHz_ = 16000000;
  static bool IRAM_ATTR frameComplete(void* context);
  // The panel cannot rotate RGB scanout. Unrotated, LVGL draws straight into the two
  // framebuffers. Rotated, it draws into canvas_ and flush copies rotated areas.
  ScreenRotation rotation_ = ScreenRotation::Deg0;
  std::uint16_t* frames_[2] = {};
  std::uint16_t* scanout_ = nullptr;  // the framebuffer the panel shows
  std::uint16_t* canvas_ = nullptr;
  std::vector<lv_area_t> copied_;  // canvas areas copied to the back framebuffer this frame
  std::uint16_t* back() const { return scanout_ == frames_[0] ? frames_[1] : frames_[0]; }
  void copyRotated(const lv_area_t& area, std::uint16_t* framebuffer);
  void show(std::uint16_t* framebuffer);
  std::uint32_t touchPresses_ = 0;
  std::uint32_t redraws_ = 0;
  std::uint32_t renderedPixels_ = 0;
  bool pressed_ = false;
};
}  // namespace vibemon
