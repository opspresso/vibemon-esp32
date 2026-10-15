#include "release_json.h"
#include <ArduinoJson.h>

namespace vibemon {
bool decodeRelease(std::string_view json, FirmwareRelease& output) {
  if (json.empty() || json.size() > MaxReleaseBytes) return false;
  JsonDocument document;
  if (deserializeJson(document, json.data(), json.size(), DeserializationOption::NestingLimit(2))) return false;
  if (document["board"].as<std::string>() != "ESP32-4848S040" || !document["version"].is<const char*>() ||
      !document["url"].is<const char*>() || !document["size"].is<std::uint32_t>() || !document["sha256"].is<const char*>()) return false;
  FirmwareRelease release{document["version"].as<std::string>(), document["url"].as<std::string>(),
                          document["size"].as<std::uint32_t>(), document["sha256"].as<std::string>()};
  if (!validRelease(release)) return false;
  output = std::move(release);
  return true;
}
}  // namespace vibemon
