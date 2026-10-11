#include "esp32_display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

namespace vibemon {
bool EspDisplay::begin() {
  if (!board_.begin() || !board_.getLCD() || !board_.getTouch()) return false;
  if (board_.getLCD()->getFrameWidth() != 480 || board_.getLCD()->getFrameHeight() != 480) return false;
  if (board_.getBacklight()) board_.getBacklight()->setBrightness(80);
  lv_init();
  lv_tick_set_cb([] { return static_cast<std::uint32_t>(millis()); });
  display_ = lv_display_create(480, 480);
  if (!display_) return false;
  // RGB scanout owns its PSRAM framebuffer. LVGL renders only changed rectangles.
  constexpr std::size_t bytes = 480 * 40 * sizeof(std::uint16_t);
  drawBuffer_ = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!drawBuffer_) return false;
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display_, drawBuffer_, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(display_, this);
  lv_display_set_flush_cb(display_, [](lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels) {
    auto& self = *static_cast<EspDisplay*>(lv_display_get_user_data(display));
    if (!self.board_.getLCD()->drawBitmap(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, pixels)) {
      Serial.println("Display transfer failed");
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
    data->state = count > 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (count > 0) { data->point.x = point.x; data->point.y = point.y; }
  });
  return true;
}
}  // namespace vibemon
