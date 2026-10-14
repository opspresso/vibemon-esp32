#include "esp32_display.h"
#include <Arduino.h>
#include <esp_lcd_panel_rgb.h>

#if CONFIG_ESP32S3_DATA_CACHE_SIZE < 65536 || CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE != 64 || CONFIG_LCD_RGB_RESTART_IN_VSYNC
#error "Build VibeMon with tools/build_firmware.py: RGB requires the configured ESP-IDF cache and DMA settings."
#endif
#if !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || !CONFIG_SPIRAM_RODATA || !CONFIG_LCD_RGB_ISR_IRAM_SAFE
#error "RGB scanout must remain available during NVS writes; use the VibeMon ESP-IDF configuration."
#endif

namespace vibemon {
bool EspDisplay::begin() {
  if (!board_.init() || !board_.getLCD()) return false;
  auto* lcd = board_.getLCD();
  // 520 clocks/line × 510 lines/frame: 16 MHz yields about 60 Hz. The
  // profile's 26 MHz scans at 98 Hz and competes with rendering/TLS for PSRAM.
  auto* bus = static_cast<esp_panel::drivers::BusRGB*>(lcd->getBus());
  if (!bus->configRGB_FreqHz(pixelClockHz_)) return false;
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
  lv_display_set_flush_cb(display_, [](lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels) {
    auto& self = *static_cast<EspDisplay*>(lv_display_get_user_data(display));
    self.renderedPixels_ += static_cast<std::uint32_t>(lv_area_get_width(area) * lv_area_get_height(area));
    if (lv_display_flush_is_last(display)) {
      ++self.redraws_;
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

bool EspDisplay::setPixelClock(std::uint32_t hz) {
  if (hz != 8000000 && hz != 12000000 && hz != 16000000 && hz != 20000000 && hz != 26000000) return false;
  if (!display_ || esp_lcd_rgb_panel_set_pclk(board_.getLCD()->getRefreshPanelHandle(), hz) != ESP_OK) return false;
  pixelClockHz_ = hz;
  return true;
}
}  // namespace vibemon
