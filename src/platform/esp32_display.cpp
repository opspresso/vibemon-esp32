#include "esp32_display.h"
#include <Arduino.h>

namespace vibemon {
bool EspDisplay::begin() {
  if (!board_.init() || !board_.getLCD()) return false;
  auto* lcd = board_.getLCD();
  if (!lcd->configFrameBufferNumber(2)) return false;
  frameReady_ = xSemaphoreCreateBinaryStatic(&frameReadyStorage_);
  if (!frameReady_) return false;
  if (!board_.begin() || !board_.getLCD() || !board_.getTouch()) return false;
  if (board_.getLCD()->getFrameWidth() != 480 || board_.getLCD()->getFrameHeight() != 480) return false;
  if (board_.getBacklight()) board_.getBacklight()->setBrightness(80);
  lv_init();
  lv_tick_set_cb([] { return static_cast<std::uint32_t>(millis()); });
  display_ = lv_display_create(480, 480);
  if (!display_) return false;
  // Start drawing into the back buffer; RGB scanout starts with framebuffer 0.
  // DIRECT mode renders changed areas and synchronizes the two buffers after each swap.
  constexpr std::size_t bytes = 480 * 480 * sizeof(std::uint16_t);
  auto* front = lcd->getFrameBufferByIndex(0);
  auto* back = lcd->getFrameBufferByIndex(1);
  if (!front || !back || !lcd->attachRefreshFinishCallback(frameComplete, this)) return false;
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display_, back, front, bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_user_data(display_, this);
  lv_display_set_flush_cb(display_, [](lv_display_t* display, const lv_area_t*, std::uint8_t* pixels) {
    auto& self = *static_cast<EspDisplay*>(lv_display_get_user_data(display));
    if (lv_display_flush_is_last(display)) {
      if (!self.board_.getLCD()->switchFrameBufferTo(pixels)) {
        Serial.println("Display framebuffer switch failed");
        abort();
      }
      // Discard completions preceding the switch. Only the next completion permits
      // LVGL to reuse the old front buffer; never acknowledge an in-flight frame.
      xSemaphoreTake(self.frameReady_, 0);
      if (xSemaphoreTake(self.frameReady_, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.println("Display framebuffer completion timed out");
        abort();
      }
    }
    lv_display_flush_ready(display);
  });
  touch_ = lv_indev_create();
  if (!touch_) return false;
  lv_indev_set_type(touch_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(touch_, this);
  lv_indev_set_read_cb(touch_, [](lv_indev_t* input, lv_indev_data_t* data) {
    auto& self = *static_cast<EspDisplay*>(lv_indev_get_user_data(input));
    esp_panel::drivers::TouchPoint point;
    const int count = self.board_.getTouch()->readPoints(&point, 1, 0);
    const bool pressed = count > 0 && point.x >= 0 && point.y >= 0 && point.x < 480 && point.y < 480;
    if (pressed && !self.pressed_) ++self.touchPresses_;
    self.pressed_ = pressed;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (pressed) { data->point.x = point.x; data->point.y = point.y; }
  });
  return true;
}

bool IRAM_ATTR EspDisplay::frameComplete(void* context) {
  auto& self = *static_cast<EspDisplay*>(context);
  self.completedFrames_ = self.completedFrames_ + 1;
  BaseType_t awakened = pdFALSE;
  xSemaphoreGiveFromISR(self.frameReady_, &awakened);
  return awakened == pdTRUE;
}
}  // namespace vibemon
