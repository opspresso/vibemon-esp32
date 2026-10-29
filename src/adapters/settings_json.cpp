#include "settings_json.h"
#include <ArduinoJson.h>

namespace vibemon {
bool decodeSettings(std::string_view json, Config& output) {
  if (json.empty() || json.size() > MaxSettingsBytes) return false;
  JsonDocument document;
  if (deserializeJson(document, json.data(), json.size(), DeserializationOption::NestingLimit(4))) return false;
  if (!document["version"].is<unsigned>() || document["version"].as<unsigned>() != 1 ||
      !document["ssid"].is<const char*>() || !document["wifiPassword"].is<const char*>() ||
      !document["origin"].is<const char*>() || !document["readToken"].is<const char*>() ||
      !document["panelsInitialized"].is<bool>()) return false;
  const auto panels = document["panels"].as<JsonArrayConst>();
  if (panels.isNull() || panels.size() != PanelCount) return false;
  Config config;
  config.ssid = document["ssid"].as<std::string>();
  config.wifiPassword = document["wifiPassword"].as<std::string>();
  config.origin = document["origin"].as<std::string>();
  // Older firmware kept explicit default ports. Normalize without replacing credentials or panels.
  std::string normalizedOrigin;
  if (!normalizeOrigin(config.origin, normalizedOrigin).ok) return false;
  config.origin = std::move(normalizedOrigin);
  config.readToken = document["readToken"].as<std::string>();
  config.panelsInitialized = document["panelsInitialized"].as<bool>();
  // Settings saved before screen rotation existed have no rotation.
  const auto degrees = document["rotation"];
  if (!degrees.isNull()) {
    if (!degrees.is<unsigned>() || degrees.as<unsigned>() % 90 != 0 || degrees.as<unsigned>() > 270) return false;
    config.rotation = static_cast<ScreenRotation>(degrees.as<unsigned>() / 90);
  }
  for (std::size_t i = 0; i < PanelCount; ++i) {
    if (!panels[i]["id"].is<const char*>()) return false;
    const auto kind = panels[i]["kind"].as<std::string>();
    if (kind != "resource" && kind != "account") return false;
    config.panels[i] = {kind == "resource" ? ItemKind::Resource : ItemKind::Account, panels[i]["id"].as<std::string>()};
  }
  if (!validConfig(config)) return false;
  output = std::move(config);
  return true;
}

std::string encodeSettings(const Config& config) {
  if (!validConfig(config)) return {};
  JsonDocument document;
  document["version"] = 1;
  document["ssid"] = config.ssid;
  document["wifiPassword"] = config.wifiPassword;
  document["origin"] = config.origin;
  document["readToken"] = config.readToken;
  document["panelsInitialized"] = config.panelsInitialized;
  document["rotation"] = static_cast<unsigned>(config.rotation) * 90;
  auto panels = document["panels"].to<JsonArray>();
  for (const auto& selection : config.panels) {
    auto panel = panels.add<JsonObject>();
    panel["kind"] = selection.kind == ItemKind::Account ? "account" : "resource";
    panel["id"] = selection.id;
  }
  std::string result;
  serializeJson(document, result);
  return result.size() <= MaxSettingsBytes ? result : std::string{};
}
}  // namespace vibemon
