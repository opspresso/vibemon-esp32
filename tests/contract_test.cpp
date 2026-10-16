#include "adapters/catalog_json.h"
#include "fakes.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {
std::string timestamp(std::uint32_t age) {
  const auto milliseconds = *utcMilliseconds("2026-10-13T08:00:00.000Z") - age;
  const auto seconds = static_cast<std::time_t>(milliseconds / 1000);
  const auto* utc = std::gmtime(&seconds);
  char date[32], result[40];
  std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", utc);
  std::snprintf(result, sizeof(result), "%s.%03dZ", date, static_cast<int>(milliseconds % 1000));
  return result;
}
}

int main(int argc, char** argv) {
  try {
    CHECK(argc == 2);
    std::ifstream input(argv[1]); CHECK(input.good());
    std::ostringstream bytes; bytes << input.rdbuf();
    JsonDocument fixtures;
    CHECK(!deserializeJson(fixtures, bytes.str()));
    CHECK(fixtures["version"] == 1);
    std::size_t count = 0;
    for (JsonObjectConst fixture : fixtures["cases"].as<JsonArrayConst>()) {
      JsonDocument response;
      response["serverTime"] = timestamp(0);
      auto sources = response["sources"].to<JsonArray>();
      for (JsonObjectConst compact : fixture["sources"].as<JsonArrayConst>()) {
        auto source = sources.add<JsonObject>();
        source["sourceId"] = compact["sourceId"];
        source["kind"] = compact["kind"];
        source["displayName"] = "Source";
        source["receivedAt"] = timestamp(compact["receiptAgeMs"] | 0u);
        source["observedAt"] = timestamp(compact["eventAgeMs"] | 0u);
        source["health"] = compact["health"] | "ok";
        source["stale"] = compact["stale"] | false;
        source["metrics"].to<JsonObject>();
        if (compact["kind"] == "agent") {
          source["agent"]["state"] = compact["state"];
          source["agent"]["project"] = "Project";
          source["account"]["id"] = compact["accountId"] | "work";
          source["account"]["provider"] = compact["provider"] | "codex";
          source["account"]["displayName"] = "Account";
        } else if (compact["kind"] == "resource") {
          source["metricDefinitions"]["value"]["label"] = "Value";
          source["metricDefinitions"]["value"]["unit"] = "";
          source["metricDefinitions"]["value"]["display"] = "number";
          source["metrics"]["value"] = nullptr;
        }
      }
      std::string body; serializeJson(response, body);
      Catalog catalog;
      CHECK(decodeCatalog(body, catalog).ok);
      const auto elapsed = fixture["elapsedMs"].as<std::uint32_t>();
      for (const auto& item : catalog.items) {
        const auto expected = fixture["expected"][item.selection.kind == ItemKind::Account ? "accounts" : "resources"][item.selection.id];
        CHECK(expected.is<const char*>());
        const auto state = monitorState(item, elapsed);
        if (state != expected.as<std::string>()) throw std::runtime_error(fixture["name"].as<std::string>() + ": " + item.selection.id + " was " + state);
        const auto latest = fixture["expected"]["latest"][item.selection.id];
        if (!latest.isNull()) CHECK(item.sourceId == latest.as<std::string>());
        const auto activity = fixture["expected"]["activity"][item.selection.id];
        if (!activity.isNull()) {
          const auto* project = accountActivity(item, elapsed);
          CHECK(project && project->sourceId == activity.as<std::string>());
        }
      }
      ++count;
    }
    for (JsonObjectConst fixture : fixtures["measurements"].as<JsonArrayConst>()) {
      Metric metric;
      metric.unit = fixture["unit"].as<std::string>();
      if (!fixture["value"].isNull()) metric.value = fixture["value"].as<double>();
      const auto expected = fixture["expected"].as<std::string>();
      const auto actual = metricValue(metric);
      if (actual != expected) throw std::runtime_error("Measurement " + fixture["value"].as<std::string>() + " " + metric.unit + ": expected " + expected + ", got " + actual);
    }
    for (JsonObjectConst fixture : fixtures["resetCountdowns"].as<JsonArrayConst>()) {
      JsonDocument response;
      response["serverTime"] = timestamp(0);
      auto source = response["sources"].to<JsonArray>().add<JsonObject>();
      source["sourceId"] = "agent"; source["kind"] = "agent"; source["displayName"] = "Source";
      source["receivedAt"] = timestamp(fixture["eventAgeMs"].as<std::uint32_t>());
      source["observedAt"] = timestamp(fixture["eventAgeMs"].as<std::uint32_t>());
      source["health"] = "ok"; source["stale"] = false;
      for (const char* key : {"contextPercent", "usageSessionPercent", "usageWeekPercent"}) source["metrics"][key] = 50;
      source["agent"].set(fixture["agent"]);
      source["agent"]["state"] = "working"; source["agent"]["project"] = "Project";
      source["account"]["id"] = "work"; source["account"]["provider"] = "codex"; source["account"]["displayName"] = "Account";
      std::string body; serializeJson(response, body);
      Catalog catalog;
      CHECK(decodeCatalog(body, catalog).ok && catalog.items.size() == 1);
      const auto& item = catalog.items.front();
      const auto metric = std::find_if(item.metrics.begin(), item.metrics.end(), [&](const Metric& m) { return m.key == fixture["metric"].as<std::string>(); });
      CHECK(metric != item.metrics.end());
      const auto expected = fixture["expected"].isNull() ? std::string() : fixture["expected"].as<std::string>();
      const auto actual = resetCountdown(*metric, static_cast<std::uint64_t>(item.activityAgeMs) + fixture["elapsedMs"].as<std::uint32_t>());
      if (actual != expected) throw std::runtime_error(fixture["name"].as<std::string>() + ": expected '" + expected + "', got '" + actual + "'");
      ++count;
    }
    std::cout << "PASS " << count << " shared App/Web/ESP32 monitoring cases through the actual JSON adapter\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
