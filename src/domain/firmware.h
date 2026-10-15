#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vibemon {

// One application slot in partitions.csv. An update must fit the slot it replaces.
constexpr std::uint32_t MaxFirmwareBytes = 0x700000;
// Releases publish the update description under this name. GitHub resolves
// "latest" to the newest non-prerelease release.
constexpr const char* FirmwareManifestUrl = "https://github.com/opspresso/vibemon-esp32/releases/latest/download/ota.json";
// Accept images only from this repository's releases.
constexpr std::string_view FirmwareDownloadPrefix = "https://github.com/opspresso/vibemon-esp32/releases/download/";

struct FirmwareRelease {
  std::string version;
  std::string url;
  std::uint32_t size = 0;
  std::string sha256;  // 64 lowercase hexadecimal digits of the application image
};

// True only when both values are MAJOR.MINOR.PATCH and `candidate` is newer.
bool newerVersion(std::string_view current, std::string_view candidate);
bool validRelease(const FirmwareRelease& release);

}  // namespace vibemon
