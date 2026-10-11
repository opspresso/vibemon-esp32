#pragma once
#include <esp_display_panel.hpp>
#include <lvgl.h>

namespace vibemon {
class EspDisplay {
 public:
  bool begin();
 private:
  esp_panel::board::Board board_;
  lv_display_t* display_ = nullptr;
  lv_indev_t* touch_ = nullptr;
  void* drawBuffer_ = nullptr;
};
}  // namespace vibemon
