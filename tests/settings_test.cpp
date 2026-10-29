#include "adapters/settings_json.h"
#include "fakes.h"

int main() {
  try {
    Store store; configured(store);
    store.saved.ssid = "한글 Wi-Fi";
    store.saved.panelsInitialized = true;
    store.saved.panels = {Selection{ItemKind::Resource, "spark"}, Selection{ItemKind::Account, "codex:work"}, Selection{ItemKind::Account, "claude:personal"}, Selection{}};
    Config recovered;
    CHECK(decodeSettings(encodeSettings(store.saved), recovered));
    CHECK(recovered.ssid == store.saved.ssid && recovered.wifiPassword == store.saved.wifiPassword);
    CHECK(recovered.readToken == store.saved.readToken && recovered.origin == store.saved.origin && recovered.panels == store.saved.panels && recovered.panelsInitialized);
    // Existing NVS settings keep credentials and selections when the default port is normalized.
    auto legacy = encodeSettings(store.saved);
    legacy.replace(legacy.find("https://vibemon.io"), std::string("https://vibemon.io").size(), "https://vibemon.io:00443");
    CHECK(decodeSettings(legacy, recovered) && recovered.origin == "https://vibemon.io");
    CHECK(recovered.readToken == store.saved.readToken && recovered.wifiPassword == store.saved.wifiPassword && recovered.panels == store.saved.panels);
    // The screen rotation round-trips; settings saved before rotation existed stay upright.
    store.saved.rotation = ScreenRotation::Deg270;
    CHECK(decodeSettings(encodeSettings(store.saved), recovered) && recovered.rotation == ScreenRotation::Deg270);
    auto unrotated = encodeSettings(store.saved);
    unrotated.erase(unrotated.find(",\"rotation\":270"), std::string(",\"rotation\":270").size());
    CHECK(decodeSettings(unrotated, recovered) && recovered.rotation == ScreenRotation::Deg0 && recovered.readToken == store.saved.readToken);
    for (const char* degrees : {"45", "360", "-90", "\"90\""}) {
      auto invalidRotation = encodeSettings(store.saved);
      invalidRotation.replace(invalidRotation.find("270"), 3, degrees);
      CHECK(!decodeSettings(invalidRotation, recovered) && recovered.rotation == ScreenRotation::Deg0);
    }
    for (const char* invalid : {"", "{}", "null", "[]", "{\"version\":2}", "not json"}) {
      CHECK(!decodeSettings(invalid, recovered));
      CHECK(recovered.readToken == store.saved.readToken);
    }
    auto invalid = store.saved; invalid.panels[1].id = "codex:../../account";
    CHECK(encodeSettings(invalid).empty());
    invalid = store.saved; invalid.origin = "https://host.example/path";
    CHECK(encodeSettings(invalid).empty());
    CHECK(!decodeSettings(std::string(MaxSettingsBytes + 1, 'x'), recovered));
    // Every valid configuration fits one NVS string, including JSON escapes.
    Config largest; largest.ssid = std::string(32, '"'); largest.wifiPassword = std::string(63, '\\');
    std::string origin; CHECK(normalizeOrigin(std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(61, 'd') + ":65535", origin).ok);
    largest.origin = origin; largest.readToken = "vm_" + std::string(60, 'f'); largest.panelsInitialized = true;
    largest.panels.fill({ItemKind::Account, std::string(32, 'a') + ":" + std::string(128, 'b')});
    CHECK(validConfig(largest));
    const auto encoded = encodeSettings(largest);
    CHECK(!encoded.empty() && encoded.size() <= MaxSettingsBytes && decodeSettings(encoded, recovered) && recovered.ssid == largest.ssid);
    Config empty; CHECK(decodeSettings(encodeSettings(empty), recovered));
    CHECK(recovered.ssid.empty() && recovered.readToken.empty() && !recovered.panelsInitialized);
    std::cout << "PASS settings: complete atomic document, Unicode, four selections, masked-key backing value and invalid-version preservation\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
