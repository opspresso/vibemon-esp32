#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "monitoring_policy.h"

namespace vibemon {

constexpr std::size_t PanelCount = 4;
constexpr std::size_t MaxSources = 128;
constexpr std::size_t MaxMetrics = 32;
constexpr std::size_t MaxResponseBytes = 1024 * 1024;
constexpr std::size_t MaxOriginBytes = 272;
constexpr std::uint32_t StaleAfterMs = contract::StaleAfterMs;
constexpr std::uint32_t PollIntervalMs = 10000;

enum class ItemKind { Resource, Account };
enum class Health { Ok, Warning, Error, Unknown };
enum class MetricDisplay { Number, Bar };

struct Selection {
  ItemKind kind = ItemKind::Resource;
  std::string id;
  bool operator==(const Selection& other) const { return kind == other.kind && id == other.id; }
  bool operator!=(const Selection& other) const { return !(*this == other); }
};

struct Config {
  std::string ssid;
  std::string wifiPassword;
  std::string origin = "https://vibemon.io";
  std::string readToken;
  std::array<Selection, PanelCount> panels{};
  bool panelsInitialized = false;
};

struct Metric {
  std::string key;
  std::string label;
  std::string unit;
  std::optional<double> value;
  MetricDisplay display = MetricDisplay::Number;
  double minimum = 0;
  std::optional<double> maximum;
};

struct ReportState {
  std::string sourceId;
  std::string activity;
  Health health = Health::Unknown;
  bool reportedStale = false;
  std::uint32_t ageMs = StaleAfterMs;
  std::uint32_t activityAgeMs = 0;
  std::int64_t observedAtMs = 0;
};

struct Monitor : ReportState {
  Selection selection;
  std::string name;
  std::string category;
  std::string project;
  // Every project retains its own clock so offline aging cannot hide live requests.
  std::vector<ReportState> projects;
  std::vector<Metric> metrics;
};

struct Catalog {
  std::vector<Monitor> items;
  const Monitor* find(const Selection& selection) const;
};

struct WifiNetwork {
  std::string ssid;
  int signal = -100;
  bool secured = true;
};

struct Validation {
  bool ok;
  std::string message;
};

Validation normalizeOrigin(const std::string& input, std::string& output);
Validation validateWifi(const std::string& ssid, const std::string& password);
Validation validateToken(const std::string& token);
bool validConfig(const Config& config);
bool validSourceId(const std::string& id);
bool validAccountId(const std::string& id);
bool validText(const std::string& text, std::size_t maxBytes, bool allowEmpty = false);
std::optional<std::int64_t> utcMilliseconds(const std::string& value);
bool stale(const ReportState& item, std::uint64_t elapsedMs);
std::string monitorState(const Monitor& item, std::uint64_t elapsedMs);
std::string stateLabel(const std::string& state);
std::string stateTone(const std::string& state);
bool needsAttention(const std::string& state);
std::string metricValue(const Metric& metric);
std::string ageLabel(std::uint64_t milliseconds);
std::array<Selection, PanelCount> defaultPanels(const Catalog& catalog);

}  // namespace vibemon
