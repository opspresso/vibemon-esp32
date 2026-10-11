#pragma once
#include "../domain/model.h"
#include <string_view>

namespace vibemon {
constexpr std::size_t MaxSettingsBytes = 8192;
bool decodeSettings(std::string_view json, Config& output);
std::string encodeSettings(const Config& config);
}  // namespace vibemon
