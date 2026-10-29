#include "esp32_display.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_rgb.h>

#if CONFIG_ESP32S3_DATA_CACHE_SIZE < 65536 || CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE != 64 || CONFIG_LCD_RGB_RESTART_IN_VSYNC
#error "Build VibeMon with tools/build_firmware.py: RGB requires the configured ESP-IDF cache and DMA settings."
#endif
#if !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || !CONFIG_SPIRAM_RODATA || !CONFIG_LCD_RGB_ISR_IRAM_SAFE
#error "RGB scanout must remain available during NVS writes; use the VibeMon ESP-IDF configuration."
#endif

namespace vibemon {
namespace {
constexpr std::int32_t Side = 480;
constexpr std::size_t FrameBytes = Side * Side * sizeof(std::uint16_t);

lv_display_rotation_t displayRotation(ScreenRotation rotation) {
  switch (rotation) {
    case ScreenRotation::Deg90: return LV_DISPLAY_ROTATION_90;
    case ScreenRotation::Deg180: return LV_DISPLAY_ROTATION_180;
    case ScreenRotation::Deg270: return LV_DISPLAY_ROTATION_270;
    default: return LV_DISPLAY_ROTATION_0;
  }
}
}  // namespace

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
  display_ = lv_display_create(Side, Side);
  if (!display_) return false;
  // Start drawing into the back buffer; RGB scanout starts with framebuffer 0.
  // DIRECT mode renders changed areas and synchronizes the two buffers after each swap.
  frames_[0] = static_cast<std::uint16_t*>(lcd->getFrameBufferByIndex(0));
  frames_[1] = static_cast<std::uint16_t*>(lcd->getFrameBufferByIndex(1));
  scanout_ = frames_[0];
  if (!frames_[0] || !frames_[1] || !lcd->attachRefreshFinishCallback(frameComplete, this)) return false;
  copied_.reserve(32);  // LV_INV_BUF_SIZE: LVGL joins invalidated areas into at most 32 per frame
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display_, back(), scanout_, FrameBytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_user_data(display_, this);
  lv_display_set_flush_cb(display_, [](lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels) {
    auto& self = *static_cast<EspDisplay*>(lv_display_get_user_data(display));
    self.renderedPixels_ += static_cast<std::uint32_t>(lv_area_get_width(area) * lv_area_get_height(area));
    if (self.rotation_ == ScreenRotation::Deg0) {
      if (lv_display_flush_is_last(display)) self.show(reinterpret_cast<std::uint16_t*>(pixels));
    } else {
      self.copyRotated(*area, self.back());
      self.copied_.push_back(*area);
      if (lv_display_flush_is_last(display)) {
        self.show(self.back());
        // The previous front buffer is now the back buffer. Bring it up to date as well.
        for (const auto& copied : self.copied_) self.copyRotated(copied, self.back());
        self.copied_.clear();
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

void EspDisplay::show(std::uint16_t* framebuffer) {
  ++redraws_;
  if (!board_.getLCD()->switchFrameBufferTo(framebuffer)) {
    Serial.println("Display framebuffer switch failed");
    abort();
  }
  // Discard completions preceding the switch. Only the next completion permits
  // reuse of the old front buffer; never acknowledge an in-flight frame.
  xSemaphoreTake(frameReady_, 0);
  if (xSemaphoreTake(frameReady_, pdMS_TO_TICKS(500)) != pdTRUE) {
    Serial.println("Display framebuffer completion timed out");
    abort();
  }
  scanout_ = framebuffer;
}

void EspDisplay::copyRotated(const lv_area_t& area, std::uint16_t* framebuffer) {
  lv_area_t target = area;
  lv_display_rotate_area(display_, &target);
  constexpr std::int32_t stride = Side * sizeof(std::uint16_t);
  lv_draw_rotate(canvas_ + area.y1 * Side + area.x1, framebuffer + target.y1 * Side + target.x1,
                 lv_area_get_width(&area), lv_area_get_height(&area), stride, stride,
                 lv_display_get_rotation(display_), LV_COLOR_FORMAT_RGB565);
}

bool EspDisplay::setRotation(ScreenRotation rotation) {
  if (!display_ || rotation == rotation_) return display_ != nullptr;
  if (rotation == ScreenRotation::Deg0) {
    // Both framebuffers match the last frame; LVGL resumes drawing into the back one.
    lv_display_set_buffers(display_, back(), scanout_, FrameBytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    heap_caps_free(canvas_);
    canvas_ = nullptr;
  } else if (!canvas_) {
    canvas_ = static_cast<std::uint16_t*>(heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!canvas_) return false;
    lv_display_set_buffers(display_, canvas_, nullptr, FrameBytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  }
  rotation_ = rotation;
  copied_.clear();
  // Changing the rotation invalidates the whole screen, so the next frame redraws every pixel.
  lv_display_set_rotation(display_, displayRotation(rotation));
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
