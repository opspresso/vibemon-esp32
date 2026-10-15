#include "firmware.h"

#include <array>
#include <optional>

namespace vibemon {
namespace {
std::optional<std::array<std::uint32_t, 3>> parse(std::string_view text) {
  std::array<std::uint32_t, 3> parts{};
  std::size_t part = 0;
  bool digit = false;
  for (const char value : text) {
    if (value == '.') {
      if (!digit || ++part == parts.size()) return std::nullopt;
      digit = false;
    } else if (value >= '0' && value <= '9') {
      if (parts[part] > 99999) return std::nullopt;
      parts[part] = parts[part] * 10 + static_cast<std::uint32_t>(value - '0');
      digit = true;
    } else return std::nullopt;
  }
  if (!digit || part != parts.size() - 1) return std::nullopt;
  return parts;
}
}  // namespace

bool newerVersion(std::string_view current, std::string_view candidate) {
  const auto installed = parse(current);
  const auto offered = parse(candidate);
  return installed && offered && *offered > *installed;
}

bool validRelease(const FirmwareRelease& release) {
  if (!parse(release.version) || release.size == 0 || release.size > MaxFirmwareBytes || release.sha256.size() != 64) return false;
  for (const char value : release.sha256) {
    if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
  }
  if (release.url.size() > 512 || release.url.compare(0, FirmwareDownloadPrefix.size(), FirmwareDownloadPrefix) != 0) return false;
  for (const char value : release.url) {
    if (value <= ' ' || value == '"' || value == '\\' || value == 0x7f) return false;
  }
  return true;
}

}  // namespace vibemon
