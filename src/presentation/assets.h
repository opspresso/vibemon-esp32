#pragma once
#include <lvgl.h>

namespace vibemon {
const lv_image_dsc_t* brandImage();
class Fonts {
 public:
  bool begin();
  ~Fonts();
  const lv_font_t* normal() const { return &normal_; }
  const lv_font_t* small() const { return &small_; }
  const lv_font_t* title() const { return &title_; }
 private:
  lv_font_t normal_{};
  lv_font_t small_{};
  lv_font_t title_{};
  lv_font_t* unicode14_ = nullptr;
  lv_font_t* unicode16_ = nullptr;
  lv_font_t* unicode20_ = nullptr;
};
}  // namespace vibemon
