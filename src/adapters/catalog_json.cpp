#include "catalog_json.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace vibemon {
namespace {
bool text(JsonVariantConst raw, std::string& out, std::size_t limit, bool empty = false) {
  if (!raw.is<const char*>()) return false;
  const JsonString value = raw.as<JsonString>();
  std::string candidate(value.c_str(), value.size());
  if (!validText(candidate, limit, empty)) return false;
  out = std::move(candidate);
  return true;
}

bool numeric(JsonVariantConst raw, std::optional<double>& out) {
  if (raw.isNull()) { out.reset(); return true; }
  if (raw.is<bool>() || !raw.is<double>()) return false;
  const double value = raw.as<double>();
  if (!std::isfinite(value) || std::abs(value) > 9007199254740991.0) return false;
  out = value;
  return true;
}

bool metricKey(const std::string& key) {
  if (key.empty() || key.size() > 64 || key == "constructor" || key == "prototype" || key == "__proto__") return false;
  auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (!alpha(key[0])) return false;
  return std::all_of(key.begin(), key.end(), [&](char c) { return alpha(c) || (c >= '0' && c <= '9') || c == '_'; });
}

bool customMetrics(JsonObjectConst source, Monitor& monitor) {
  const auto definitions = source["metricDefinitions"].as<JsonObjectConst>();
  const auto values = source["metrics"].as<JsonObjectConst>();
  if (definitions.isNull() || values.isNull() || definitions.size() > MaxMetrics) return false;
  std::vector<std::string> keys;
  if (!source["metricOrder"].isNull()) {
    const auto order = source["metricOrder"].as<JsonArrayConst>();
    if (order.isNull() || order.size() != definitions.size()) return false;
    std::set<std::string> unique;
    for (JsonVariantConst key : order) {
      if (!key.is<const char*>()) return false;
      const auto value = key.as<std::string>();
      if (definitions[value].isNull() || !unique.insert(value).second) return false;
      keys.push_back(value);
    }
  } else {
    for (JsonPairConst pair : definitions) keys.emplace_back(pair.key().c_str());
  }
  for (const auto& key : keys) {
    Metric metric;
    metric.key = key;
    const auto definition = definitions[key].as<JsonObjectConst>();
    std::string display;
    std::optional<double> min;
    if (!metricKey(key) || definition.isNull() || !text(definition["label"], metric.label, 320) ||
        !text(definition["unit"], metric.unit, 64, true) || !text(definition["display"], display, 16) ||
        (display != "number" && display != "gauge") || !numeric(definition["min"], min) ||
        !numeric(definition["max"], metric.maximum) || !numeric(values[key], metric.value)) return false;
    if (min && metric.maximum && *min >= *metric.maximum) return false;
    if (display == "gauge" && (!min || !metric.maximum)) return false;
    metric.minimum = min.value_or(0);
    metric.display = display == "gauge" ? MetricDisplay::Bar : MetricDisplay::Number;
    monitor.metrics.push_back(std::move(metric));
  }
  for (JsonPairConst pair : values) if (definitions[pair.key()].isNull()) return false;
  return true;
}

bool presetMetrics(JsonObjectConst source, const std::string& kind, Monitor& monitor) {
  const auto values = source["metrics"].as<JsonObjectConst>();
  if (values.isNull()) return false;
  struct Preset { const char* key; const char* label; const char* unit; bool gauge; };
  const Preset host[] = {{"cpuPercent", "CPU", "%", true}, {"memoryPercent", "Memory", "%", true}, {"gpuPercent", "GPU", "%", true}, {"gpuTemperatureC", "GPU temperature", "°C", false}};
  const Preset cluster[] = {{"cpuPercent", "CPU", "%", true}, {"memoryPercent", "Memory", "%", true}, {"nodesReady", "Ready nodes", "", false}, {"nodesTotal", "Total nodes", "", false}, {"podsUnhealthy", "Unhealthy pods", "", false}};
  const Preset agent[] = {{"contextPercent", "Context", "%", true}, {"usageSessionPercent", "Session", "%", true}, {"usageWeekPercent", "Weekly", "%", true}};
  const Preset* presets = kind == "agent" ? agent : kind == "cluster" ? cluster : host;
  const std::size_t count = kind == "agent" ? 3 : kind == "cluster" ? 5 : 4;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& preset = presets[i];
    Metric metric;
    metric.key = preset.key; metric.label = preset.label; metric.unit = preset.unit;
    if (metric.key == "memoryPercent" && source["memoryType"] == "unified") metric.label = "Unified memory";
    metric.display = preset.gauge ? MetricDisplay::Bar : MetricDisplay::Number;
    if (preset.gauge) metric.maximum = 100;
    if (!numeric(values[preset.key], metric.value)) return false;
    monitor.metrics.push_back(std::move(metric));
  }
  return true;
}

std::uint32_t age(std::int64_t now, std::int64_t then) {
  return static_cast<std::uint32_t>(std::min<std::int64_t>(std::numeric_limits<std::uint32_t>::max(), std::max<std::int64_t>(0, now - then)));
}
}  // namespace

Validation decodeCatalog(const std::string& body, Catalog& output, ArduinoJson::Allocator* allocator) {
  if (body.size() > MaxResponseBytes) return {false, "Workspace response exceeds the device limit."};
  JsonDocument filter;
  filter["serverTime"] = true;
  auto fields = filter["sources"][0].to<JsonObject>();
  for (const char* name : {"sourceId", "kind", "displayName", "observedAt", "receivedAt", "health", "stale", "memoryType", "resourceType", "metrics", "metricDefinitions", "metricOrder", "account"}) fields[name] = true;
  fields["agent"]["state"] = true;
  fields["agent"]["project"] = true;
  JsonDocument document = allocator ? JsonDocument(allocator) : JsonDocument();
  const auto error = deserializeJson(document, body, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
  if (error) return {false, "The server returned an invalid or oversized workspace."};
  const auto sources = document["sources"].as<JsonArrayConst>();
  std::string serverTime;
  if (sources.isNull() || !text(document["serverTime"], serverTime, 24)) return {false, "The server response is not a VibeMon workspace."};
  const auto now = utcMilliseconds(serverTime);
  if (!now) return {false, "The server returned an invalid timestamp."};
  if (sources.size() > MaxSources) return {false, "This device supports up to 128 reporting sources."};
  Catalog candidate;
  candidate.items.reserve(sources.size());
  std::set<std::string> seenSources;
  std::map<std::string, std::pair<std::size_t, std::int64_t>> accounts;
  for (JsonObjectConst source : sources) {
    Monitor item;
    std::string kind, id, observedAt, receivedAt, health;
    if (!text(source["sourceId"], id, 128) || !validSourceId(id) || !seenSources.insert(id).second ||
        !text(source["kind"], kind, 16) || !text(source["displayName"], item.name, 512) ||
        !text(source["observedAt"], observedAt, 24) || !text(source["receivedAt"], receivedAt, 24) ||
        !text(source["health"], health, 16) || !source["stale"].is<bool>()) return {false, "A source contains invalid identity or status fields."};
    const auto observed = utcMilliseconds(observedAt), received = utcMilliseconds(receivedAt);
    if (!observed || !received) return {false, "A source contains invalid timestamps."};
    item.ageMs = age(*now, *received); item.activityAgeMs = age(*now, *observed);
    item.reportedStale = source["stale"].as<bool>();
    if (health == "ok") item.health = Health::Ok;
    else if (health == "warning") item.health = Health::Warning;
    else if (health == "error") item.health = Health::Error;
    else if (health != "unknown") return {false, "A source contains an unknown health status."};
    if (kind == "agent") {
      std::string provider, accountId;
      if (!text(source["account"]["provider"], provider, 32) || !text(source["account"]["id"], accountId, 128) ||
          !text(source["account"]["displayName"], item.name, 512) || !validAccountId(provider + ":" + accountId) ||
          !text(source["agent"]["state"], item.activity, 256) || !text(source["agent"]["project"], item.project, 1024)) return {false, "A coding account contains invalid fields."};
      item.selection = {ItemKind::Account, provider + ":" + accountId};
      item.category = provider;
      item.projectCount = 1;
      if (!presetMetrics(source, kind, item)) return {false, "A coding account contains invalid measurements."};
      auto previous = accounts.find(item.selection.id);
      if (previous != accounts.end()) {
        auto& current = candidate.items[previous->second.first];
        item.projectCount = static_cast<std::uint16_t>(current.projectCount + 1);
        if (*observed > previous->second.second) {
          current = std::move(item); previous->second.second = *observed;
        } else ++current.projectCount;
        continue;
      }
      accounts.emplace(item.selection.id, std::make_pair(candidate.items.size(), *observed));
    } else if (kind == "resource" || kind == "host" || kind == "cluster") {
      item.selection = {ItemKind::Resource, id};
      if (!source["resourceType"].isNull()) {
        if (!text(source["resourceType"], item.category, 256)) return {false, "A resource type is invalid."};
      } else item.category = kind == "cluster" ? "Kubernetes" : source["memoryType"] == "unified" ? "DGX Spark" : "Host";
      if (kind == "resource" || !source["metricDefinitions"].isNull()) {
        if (!customMetrics(source, item)) return {false, "A resource contains invalid metric definitions or values."};
      } else if (!presetMetrics(source, kind, item)) return {false, "A resource contains invalid measurements."};
    } else return {false, "The server returned an unsupported source kind."};
    candidate.items.push_back(std::move(item));
  }
  std::sort(candidate.items.begin(), candidate.items.end(), [](const Monitor& a, const Monitor& b) {
    if (a.selection.kind != b.selection.kind) return a.selection.kind < b.selection.kind;
    return a.name == b.name ? a.selection.id < b.selection.id : a.name < b.name;
  });
  output = std::move(candidate);
  return {true, {}};
}
}  // namespace vibemon
