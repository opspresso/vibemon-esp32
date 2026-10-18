#include "domain/model.h"
#include "adapters/catalog_json.h"
#include "adapters/release_json.h"

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
     "metrics":{"contextPercent":43,"usageSessionPercent":0,"usageWeekPercent":24},"account":{"id":"work","provider":"codex","displayName":"Work account"},"agent":{"state":"working","project":"api","usageWeekModel":31.5,"usageWeekModelLabel":"Opus","usageWeekModelResetsIn":90}},
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
    CHECK(normalizeOrigin("HTTPS://DASH.EXAMPLE:443/", origin).ok && origin == "https://dash.example");
    CHECK(normalizeOrigin("https://dash.example:000443", origin).ok && origin == "https://dash.example");
    CHECK(normalizeOrigin("https://dash.example:08443", origin).ok && origin == "https://dash.example:8443");
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
    CHECK(account->projects[0].project == "api" && account->projects[1].project == "web");
    CHECK(account->metrics[1].value == 0);
    CHECK(account->metrics.size() == 4 && account->metrics[3].key == "usageWeekModel" && account->metrics[3].label == "Weekly Opus");
    CHECK(account->metrics[3].value == 31.5 && account->metrics[3].resetMinutes == 90u && account->metrics[3].display == MetricDisplay::Bar);
    CHECK(catalog.find({ItemKind::Account, "codex:personal"})->metrics.size() == 3);
    for (const auto& [from, to] : std::initializer_list<std::pair<std::string, std::string>>{
             {"\"usageWeekModelLabel\":\"Opus\"", "\"usageWeekModelLabel\":\"\""}, {"\"usageWeekModel\":31.5", "\"usageWeekModel\":null"}}) {
      auto variant = sample(); variant.replace(variant.find(from), from.size(), to);
      Catalog decoded; CHECK(decodeCatalog(variant, decoded).ok);
      const auto& metrics = decoded.find({ItemKind::Account, "codex:work"})->metrics;
      CHECK(to.find("null") != std::string::npos ? metrics.size() == 3 : metrics[3].label == "Weekly model");
    }
    for (const auto& [from, to] : std::initializer_list<std::pair<std::string, std::string>>{
             {"\"usageWeekModel\":31.5", "\"usageWeekModel\":100.5"}, {"\"usageWeekModel\":31.5", "\"usageWeekModel\":\"31\""},
             {"\"Opus\"", "\"Op\\nus\""}, {"\"usageWeekModelResetsIn\":90", "\"usageWeekModelResetsIn\":-1"}}) {
      auto variant = sample(); variant.replace(variant.find(from), from.size(), to);
      Catalog decoded; CHECK(!decodeCatalog(variant, decoded).ok);
    }
    CHECK(stateLabel(monitorState(*account, 0)) == "Working");
    CHECK(stateLabel(monitorState(*account, 180000)) == "Working");
    CHECK(stateLabel(monitorState(*account, 600000)) == "Inactive");
    CHECK(!needsAttention(monitorState(*account, 600000)));
    std::uint64_t rolloverAge = 1;
    rolloverAge <<= 32;
    CHECK(stale(*resource, rolloverAge));
    CHECK(monitorState(*account, rolloverAge) == "inactive");
    auto panels = defaultPanels(catalog);
    CHECK(panels[0].kind == ItemKind::Resource && panels[1].kind == ItemKind::Account && panels[2].id == "codex:work" && panels[3].id.empty());
    const auto before = catalog.items.size();
    auto malformed = sample();
    const auto at = malformed.find("\"gpu\":28.4");
    malformed.replace(at, std::string("\"gpu\":28.4").size(), "\"gpu\":\"unknown\"");
    const auto rejected = decodeCatalog(malformed, catalog);
    CHECK(!rejected.ok && !rejected.exceedsLimit && catalog.items.size() == before);
    CHECK(rejected.message == "A resource contains invalid metric definitions or values.");
    for (const char* invalid : {R"(First\nSecond)", R"(First\u0000Second)", R"(First\u007fSecond)", R"(\ud800)"}) {
      auto badName = sample();
      badName.replace(badName.find("DGX Spark"), 9, invalid);
      CHECK(!decodeCatalog(badName, catalog).ok && catalog.items.size() == before);
    }
    auto unicodeName = sample();
    unicodeName.replace(unicodeName.find("DGX Spark"), 9, "한글 🐱");
    CHECK(decodeCatalog(unicodeName, catalog).ok);
    CHECK(catalog.find({ItemKind::Resource, "spark"})->name == "한글 🐱");
    for (const auto& reading : {std::pair<const char*, double>{"1.005", 1.005}, {"0.1234567890123456", 0.1234567890123456}, {"999999.995", 999999.995}}) {
      auto precise = sample();
      precise.replace(precise.find("28.4"), 4, reading.first);
      CHECK(decodeCatalog(precise, catalog).ok);
      CHECK(catalog.find({ItemKind::Resource, "spark"})->metrics[1].value == reading.second);
    }
    CHECK(decodeCatalog("{\"sources\":[]}", catalog).message == "The server response is not a VibeMon workspace.");
    CHECK(decodeCatalog("{\"sources\":", catalog).message == "The response is not valid JSON.");
    const auto oversized = decodeCatalog(std::string(MaxResponseBytes + 1, 'x'), catalog);
    CHECK(!oversized.ok && oversized.exceedsLimit);
    std::string crowded = "{\"serverTime\":\"2026-10-11T00:00:00Z\",\"sources\":[";
    for (std::size_t i = 0; i <= MaxSources; ++i) crowded += i ? ",{}" : "{}";
    const auto tooMany = decodeCatalog(crowded + "]}", catalog);
    CHECK(!tooMany.ok && tooMany.exceedsLimit && tooMany.message == "This device supports up to 128 reporting sources.");
    const auto empty = decodeCatalog("{\"serverTime\":\"2026-10-11T00:00:00Z\",\"sources\":[]}", catalog);
    CHECK(empty.ok && catalog.items.empty() && empty.serverTimeMs == *utcMilliseconds("2026-10-11T00:00:00Z"));
    Metric metric;
    CHECK(metricValue(metric) == "—");
    metric.value = 0; CHECK(metricValue(metric) == "0");
    metric.value = 12.4; CHECK(metricValue(metric) == "12.4");
    metric.value = 14200; CHECK(metricValue(metric) == "14.2k");
    CHECK(metricValue(metric, false) == "14,200");
    CHECK(ageLabel(14999) == "Just now" && ageLabel(15000) == "15s ago" && ageLabel(60000) == "1m ago");
    CHECK(newerVersion("3.3.2", "3.4.0") && newerVersion("3.3.2", "3.3.10") && newerVersion("3.9.9", "4.0.0"));
    CHECK(!newerVersion("3.4.0", "3.4.0") && !newerVersion("3.4.0", "3.3.9") && !newerVersion("3.4.0", "3.4"));
    CHECK(!newerVersion("3.4.0", "3.4.0-rc.1") && !newerVersion("3.4.0", "v3.5.0") && !newerVersion("3.4.0", "3..5"));
    const std::string digest(64, 'a');
    const std::string release = "{\"board\":\"ESP32-4848S040\",\"version\":\"3.4.0\",\"url\":\"https://github.com/opspresso/vibemon-esp32/releases/download/v3.4.0/vibemon-esp32.ino.bin\",\"size\":3800000,\"sha256\":\"" + digest + "\"}";
    FirmwareRelease firmware;
    CHECK(decodeRelease(release, firmware) && firmware.version == "3.4.0" && firmware.size == 3800000 && firmware.sha256 == digest);
    auto replace = [&](const std::string& from, const std::string& to) { auto value = release; value.replace(value.find(from), from.size(), to); return value; };
    CHECK(!decodeRelease(replace("ESP32-4848S040", "ESP32-S3-Box"), firmware));
    CHECK(!decodeRelease(replace("https://github.com/opspresso/vibemon-esp32/", "https://example.com/"), firmware));
    CHECK(!decodeRelease(replace("3800000", "7340033"), firmware) && !decodeRelease(replace("3800000", "0"), firmware));
    CHECK(!decodeRelease(replace(digest, std::string(64, 'A')), firmware) && !decodeRelease(replace(digest, digest.substr(1)), firmware));
    CHECK(!decodeRelease(replace("\"3.4.0\"", "\"latest\""), firmware) && !decodeRelease("[]", firmware));
    std::cout << "PASS domain: credential boundaries, Unicode, timestamps, account isolation, metric order, missing values, stale data, atomic snapshots and firmware releases\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
