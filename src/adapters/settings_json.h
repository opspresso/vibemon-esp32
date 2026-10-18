#pragma once
#include "../domain/model.h"
#include <string_view>

namespace vibemon {
// NVS stores a string of at most 4000 bytes, including its terminator.
constexpr std::size_t MaxSettingsBytes = 3999;
bool decodeSettings(std::string_view json, Config& output);
std::string encodeSettings(const Config& config);
}  // namespace vibemon
