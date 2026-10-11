#include "assets.h"
#include <assets.inc>

namespace vibemon {
const lv_image_dsc_t* brandImage() {
  static const lv_image_dsc_t image = [] {
    lv_image_dsc_t value{};
    value.header.magic = LV_IMAGE_HEADER_MAGIC;
    value.header.cf = LV_COLOR_FORMAT_ARGB8888;
    value.header.w = 32; value.header.h = 32; value.header.stride = 32 * 4;
    value.data_size = sizeof(BrandPixels); value.data = BrandPixels;
    return value;
  }();
  return &image;
}

bool Fonts::begin() {
  unicode14_ = lv_tiny_ttf_create_data(UnicodeFontData, sizeof(UnicodeFontData), 14);
  unicode16_ = lv_tiny_ttf_create_data(UnicodeFontData, sizeof(UnicodeFontData), 16);
  unicode20_ = lv_tiny_ttf_create_data(UnicodeFontData, sizeof(UnicodeFontData), 20);
  if (!unicode14_ || !unicode16_ || !unicode20_) return false;
  normal_ = lv_font_montserrat_16; normal_.fallback = unicode16_;
  small_ = lv_font_montserrat_14; small_.fallback = unicode14_;
  title_ = lv_font_montserrat_20; title_.fallback = unicode20_;
  return true;
}

Fonts::~Fonts() {
  if (unicode14_) lv_tiny_ttf_destroy(unicode14_);
  if (unicode16_) lv_tiny_ttf_destroy(unicode16_);
  if (unicode20_) lv_tiny_ttf_destroy(unicode20_);
}
}  // namespace vibemon
