#include "domain/model.h"
#include "adapters/catalog_json.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace vibemon;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

std::string sample() {
  return R"({"serverTime":"2026-10-11T10:00:00.000Z","sources":[
    {"sourceId":"spark","kind":"resource","displayName":"DGX Spark","resourceType":"GPU host","observedAt":"2026-10-11T09:59:50.000Z","receivedAt":"2026-10-11T09:59:55.000Z","health":"ok","stale":false,
     "metrics":{"gpu":28.4,"memory":null,"temperature":43},"metricOrder":["temperature","gpu","memory"],
     "metricDefinitions":{"gpu":{"label":"GPU","unit":"%","display":"gauge","min":0,"max":100},"memory":{"label":"Memory","unit":"%","display":"gauge","min":0,"max":100},"temperature":{"label":"Temperature","unit":"°C","display":"number"}}},
    {"sourceId":"coding-one","kind":"agent","displayName":"Editor","observedAt":"2026-10-11T09:58:00.000Z","receivedAt":"2026-10-11T09:59:55.000Z","health":"ok","stale":false,
     "metrics":{"contextPercent":12,"usageSessionPercent":null,"usageWeekPercent":24},"account":{"id":"work","provider":"codex","displayName":"Work account"},"agent":{"state":"thinking","project":"web"}},
    {"sourceId":"coding-two","kind":"agent","displayName":"Editor","observedAt":"2026-10-11T09:59:59.000Z","receivedAt":"2026-10-11T09:59:59.000Z","health":"ok","stale":false,
     "metrics":{"contextPercent":43,"usageSessionPercent":0,"usageWeekPercent":24},"account":{"id":"work","provider":"codex","displayName":"Work account"},"agent":{"state":"working","project":"api"}},
    {"sourceId":"coding-three","kind":"agent","displayName":"Editor","observedAt":"2026-10-11T09:59:58.000Z","receivedAt":"2026-10-11T09:59:58.000Z","health":"ok","stale":false,
     "metrics":{"contextPercent":7,"usageSessionPercent":8,"usageWeekPercent":18},"account":{"id":"personal","provider":"codex","displayName":"Personal"},"agent":{"state":"idle","project":"tools"}}
  ],"accounts":[{"ignored":"The firmware groups source identities without retaining duplicate server data."}]})";
}

int main() {
  try {
    std::string origin;
    CHECK(normalizeOrigin(" vibemon.io/ ", origin).ok && origin == "https://vibemon.io");
    CHECK(normalizeOrigin("https://DASH.example:8443", origin).ok && origin == "https://dash.example:8443");
    CHECK(normalizeOrigin("HTTPS://DASH.EXAMPLE", origin).ok && origin == "https://dash.example");
    const std::string longHost = std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(61, 'd');
    CHECK(normalizeOrigin(longHost + ":65535", origin).ok);
    Config longOrigin; longOrigin.origin = origin; CHECK(validConfig(longOrigin));
    for (const char* value : {"http://vibemon.io", "https://user:token@vibemon.io", "https://vibemon.io/path", "https://vibemon.io?x=1", "https://vibemon.io#x", "https://-bad.io", "https://bad..io", "https://vibemon.io:0", "https://vibemon.io:65536", "https://vibemon.io\\evil"}) CHECK(!normalizeOrigin(value, origin).ok);
    CHECK(validateWifi("Workshop", "12345678").ok);
    CHECK(validateWifi(" Hidden ", "").ok);
    CHECK(!validateWifi("", "12345678").ok);
    CHECK(!validateWifi(std::string(33, 'a'), "12345678").ok);
    CHECK(!validateWifi("Workshop", "short").ok);
    CHECK(validateWifi("Workshop", std::string(64, 'a')).ok);
    CHECK(!validateWifi("Workshop", std::string(64, 'z')).ok);
    CHECK(validateToken("vm_" + std::string(60, 'a')).ok);
    CHECK(!validateToken("vm_" + std::string(60, 'z')).ok);
    CHECK(!validateToken("vm_short").ok);
    CHECK(validText("한글 이름", 64));
    CHECK(!validText(std::string("bad\0name", 8), 64));
    CHECK(!validText("\xc0\x80", 64));
    CHECK(!validText("\xed\xa0\x80", 64));
    CHECK(!validText("\xf4\x90\x80\x80", 64));
    CHECK(utcMilliseconds("1970-01-01T00:00:00Z") == 0);
    CHECK(utcMilliseconds("2024-02-29T00:00:00.125Z").has_value());
    CHECK(!utcMilliseconds("2025-02-29T00:00:00.000Z"));
    CHECK(!utcMilliseconds("2026-10-11T25:00:00.000Z"));
    CHECK(*utcMilliseconds("2026-10-11T00:00:01.123Z") - *utcMilliseconds("2026-10-11T00:00:00.000Z") == 1123);
    CHECK(validAccountId("codex:work"));
    CHECK(!validAccountId("Codex:work"));
    Catalog catalog;
    CHECK(decodeCatalog(sample(), catalog).ok);
    CHECK(catalog.items.size() == 3);
    const auto* resource = catalog.find({ItemKind::Resource, "spark"});
    CHECK(resource && resource->metrics[0].key == "temperature" && resource->metrics[1].value && std::abs(*resource->metrics[1].value - 28.4) < 1e-5);
    CHECK(!resource->metrics[2].value);
    CHECK(!stale(*resource, 114999) && stale(*resource, 115000));
    const auto* account = catalog.find({ItemKind::Account, "codex:work"});
    CHECK(account && account->project == "api" && account->projects.size() == 2 && account->metrics[0].value == 43);
    CHECK(account->metrics[1].value == 0);
    CHECK(stateLabel(monitorState(*account, 0)) == "Working");
    CHECK(stateLabel(monitorState(*account, 300000)) == "Inactive");
    CHECK(!needsAttention(monitorState(*account, 300000)));
    auto panels = defaultPanels(catalog);
    CHECK(panels[0].kind == ItemKind::Resource && panels[1].kind == ItemKind::Account && panels[2].id == "codex:work" && panels[3].id.empty());
    const auto before = catalog.items.size();
    auto malformed = sample();
    const auto at = malformed.find("\"gpu\":28.4");
    malformed.replace(at, std::string("\"gpu\":28.4").size(), "\"gpu\":\"unknown\"");
    CHECK(!decodeCatalog(malformed, catalog).ok && catalog.items.size() == before);
    for (const auto& reading : {std::pair<const char*, double>{"1.005", 1.005}, {"0.1234567890123456", 0.1234567890123456}, {"999999.995", 999999.995}}) {
      auto precise = sample();
      precise.replace(precise.find("28.4"), 4, reading.first);
      CHECK(decodeCatalog(precise, catalog).ok);
      CHECK(catalog.find({ItemKind::Resource, "spark"})->metrics[1].value == reading.second);
    }
    CHECK(!decodeCatalog("{\"sources\":[]}", catalog).ok);
    CHECK(!decodeCatalog(std::string(MaxResponseBytes + 1, 'x'), catalog).ok);
    CHECK(decodeCatalog("{\"serverTime\":\"2026-10-11T00:00:00Z\",\"sources\":[]}", catalog).ok && catalog.items.empty());
    Metric metric;
    CHECK(metricValue(metric) == "—");
    metric.value = 0; CHECK(metricValue(metric) == "0");
    metric.value = 12.4; CHECK(metricValue(metric) == "12.4");
    metric.value = 14200; CHECK(metricValue(metric) == "14.2k");
    CHECK(ageLabel(14999) == "Just now" && ageLabel(15000) == "15s ago" && ageLabel(60000) == "1m ago");
    std::cout << "PASS domain: credential boundaries, Unicode, timestamps, account isolation, metric order, missing values, stale data and atomic snapshots\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
