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
