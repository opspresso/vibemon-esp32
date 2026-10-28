#include "model.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace vibemon {
namespace {
bool asciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool hex(char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
int number(const std::string& value, std::size_t at, std::size_t size) {
  int result = 0;
  for (std::size_t i = at; i < at + size; ++i) {
    if (!digit(value[i])) return -1;
    result = result * 10 + value[i] - '0';
  }
  return result;
}
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
}  // namespace

const Monitor* Catalog::find(const Selection& selection) const {
  for (const auto& item : items) if (item.selection == selection) return &item;
  return nullptr;
}

bool validText(const std::string& text, std::size_t maxBytes, bool allowEmpty) {
  if ((!allowEmpty && text.empty()) || text.size() > maxBytes) return false;
  // Reject controls and malformed UTF-8 at the input boundary. Names retain Unicode.
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x20 || c == 0x7f) return false;
    if (c < 0x80) { ++i; continue; }
    unsigned count = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!count || i + count > text.size()) return false;
    for (unsigned j = 1; j < count; ++j) if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return false;
    const auto second = static_cast<unsigned char>(text[i + 1]);
    if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
        (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90)) return false;
    i += count;
  }
  return true;
}

bool validSourceId(const std::string& id) {
  if (id.empty() || id.size() > 128 || !(asciiAlpha(id[0]) || digit(id[0]))) return false;
  for (const char c : id) if (!(asciiAlpha(c) || digit(c) || c == '_' || c == '.' || c == ':' || c == '-')) return false;
  return true;
}

bool validAccountId(const std::string& id) {
  const auto colon = id.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 32 || id[0] < 'a' || id[0] > 'z') return false;
  for (std::size_t i = 0; i < colon; ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || digit(c) || c == '_' || c == '-')) return false;
  }
  return validSourceId(id.substr(colon + 1));
}

Validation normalizeOrigin(const std::string& input, std::string& output) {
  std::size_t first = 0, last = input.size();
  while (first < last && space(input[first])) ++first;
  while (last > first && space(input[last - 1])) --last;
  std::string value = input.substr(first, last - first);
  if (value.empty() || value.size() > MaxOriginBytes) return {false, "Enter your VibeMon server address."};
  if (value.find("://") == std::string::npos) value = "https://" + value;
  for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  if (value.size() > MaxOriginBytes) return {false, "The server address is too long."};
  if (value.compare(0, 8, "https://") != 0) return {false, "Use HTTPS to protect your read token."};
  if (value.back() == '/') value.pop_back();
  const auto authority = value.substr(8);
  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  if (host.empty() || host.size() > 253) return {false, "Enter a server hostname."};
  std::size_t labelStart = 0;
  for (std::size_t i = 0; i <= host.size(); ++i) {
    if (i == host.size() || host[i] == '.') {
      const auto length = i - labelStart;
      if (!length || length > 63 || host[labelStart] == '-' || host[i - 1] == '-') return {false, "Enter a valid server hostname."};
      labelStart = i + 1;
    } else if (!(asciiAlpha(host[i]) || digit(host[i]) || host[i] == '-')) {
      return {false, "Use a hostname without a path, query or sign-in details."};
    }
  }
  value = "https://" + host;
  if (colon != std::string::npos) {
    const auto port = authority.substr(colon + 1);
    if (port.empty() || !std::all_of(port.begin(), port.end(), digit)) return {false, "Enter a valid HTTPS port."};
    unsigned valuePort = 0;
    for (char c : port) {
      valuePort = valuePort * 10 + static_cast<unsigned>(c - '0');
      if (valuePort > 65535) return {false, "HTTPS port must be 1 to 65535."};
    }
    if (!valuePort) return {false, "HTTPS port must be 1 to 65535."};
    if (valuePort != 443) value += ':' + std::to_string(valuePort);
  }
  output = value;
  return {true, {}};
}

Validation validateWifi(const std::string& ssid, const std::string& password) {
  if (!validText(ssid, 32)) return {false, "Network name must contain 1 to 32 bytes."};
  if (password.empty()) return {true, {}};
  if (password.size() == 64 && std::all_of(password.begin(), password.end(), hex)) return {true, {}};
  if (password.size() < 8 || password.size() > 63 || !validText(password, 63)) return {false, "Wi-Fi password must contain 8 to 63 bytes."};
  return {true, {}};
}

Validation validateToken(const std::string& token) {
  if (token.size() != 63 || token.compare(0, 3, "vm_") != 0 ||
      !std::all_of(token.begin() + 3, token.end(), [](char c) { return digit(c) || (c >= 'a' && c <= 'f'); })) {
    return {false, "Enter a read token from vibemon.io/account."};
  }
  return {true, {}};
}

bool validConfig(const Config& config) {
  std::string origin;
  if (!normalizeOrigin(config.origin, origin).ok || origin != config.origin) return false;
  if (config.ssid.empty() ? !config.wifiPassword.empty() : !validateWifi(config.ssid, config.wifiPassword).ok) return false;
  if (!config.readToken.empty() && !validateToken(config.readToken).ok) return false;
  for (const auto& selection : config.panels) {
    if (!selection.id.empty() && !(selection.kind == ItemKind::Account ? validAccountId(selection.id) : validSourceId(selection.id))) return false;
  }
  return true;
}

std::optional<std::int64_t> utcMilliseconds(const std::string& value) {
  if ((value.size() != 20 && value.size() != 24) || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':' || value.back() != 'Z' ||
      (value.size() == 24 && value[19] != '.')) return {};
  const int year = number(value, 0, 4), month = number(value, 5, 2), day = number(value, 8, 2);
  const int hour = number(value, 11, 2), minute = number(value, 14, 2), second = number(value, 17, 2);
  const int ms = value.size() == 24 ? number(value, 20, 3) : 0;
  if (year < 1970 || year > 2199 || month < 1 || month > 12 || day < 1 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 || ms < 0) return {};
  const int monthDays[] = {31, 28 + static_cast<int>(leap(year)), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day > monthDays[month - 1]) return {};
  std::int64_t days = day - 1;
  for (int y = 1970; y < year; ++y) days += leap(y) ? 366 : 365;
  for (int m = 1; m < month; ++m) days += monthDays[m - 1];
  return (((days * 24 + hour) * 60 + minute) * 60 + second) * 1000 + ms;
}

bool stale(const ReportState& item, std::uint64_t elapsedMs) {
  return item.reportedStale || static_cast<std::uint64_t>(item.ageMs) + elapsedMs >= StaleAfterMs;
}

namespace {
const contract::StateRule* stateRule(const std::string& state) {
  for (const auto& rule : contract::States) if (state == rule.id) return &rule;
  return nullptr;
}
unsigned attentionRank(const std::string& state) {
  const auto* rule = stateRule(state);
  return rule && rule->attentionRank ? rule->attentionRank : std::numeric_limits<unsigned>::max();
}
std::string healthState(Health health) {
  switch (health) {
    case Health::Ok: return "ok";
    case Health::Warning: return "warning";
    case Health::Error: return "error";
    default: return "unknown";
  }
}
std::string reportState(const ReportState& item, bool agent, std::uint64_t elapsedMs) {
  const auto health = healthState(item.health);
  if (!agent) return stale(item, elapsedMs) ? StandbyState : health;
  // Coding hooks report events plus a heartbeat while a turn runs, so liveness
  // follows the clock of the newest report: a turn that stops reporting ages.
  std::string state = item.activity;
  auto age = static_cast<std::uint64_t>(item.activityAgeMs) + elapsedMs;
  auto* rule = stateRule(state);
  while (rule && rule->afterMs && age >= rule->afterMs) {
    age -= rule->afterMs;
    state = rule->next;
    rule = stateRule(state);
  }
  if (state == "sleep") return StandbyState;
  // Health belongs to the event; once its state has aged, so has its health.
  return state == item.activity && attentionRank(health) < attentionRank(state) ? health : state;
}
}  // namespace

std::string projectState(const ReportState& item, std::uint64_t elapsedMs) {
  return reportState(item, true, elapsedMs);
}

std::string monitorState(const Monitor& item, std::uint64_t elapsedMs) {
  if (item.selection.kind != ItemKind::Account) return reportState(item, false, elapsedMs);
  std::string state = StandbyState;
  // The adapter sorts newest first, with stable source-ID ties, as App and Web do.
  for (const auto& project : item.projects) {
    const auto current = reportState(project, true, elapsedMs);
    if (current == StandbyState) continue;
    if (state == StandbyState || attentionRank(current) < attentionRank(state)) state = current;
  }
  return state;
}

const ReportState* accountActivity(const Monitor& item, std::uint64_t elapsedMs) {
  const auto state = monitorState(item, elapsedMs);
  for (const auto& project : item.projects) {
    if (projectState(project, elapsedMs) == state) return &project;
  }
  return nullptr;
}

std::string monitorTitle(const Monitor& item, std::uint64_t elapsedMs) {
  if (item.selection.kind == ItemKind::Account) {
    const auto* project = accountActivity(item, elapsedMs);
    if (project && !project->project.empty()) return project->project;
  }
  return item.name;
}

std::string accountName(const std::string& accountKey) {
  const auto colon = accountKey.find(':');
  const auto id = colon == std::string::npos ? accountKey : accountKey.substr(colon + 1);
  for (const char* prefix : {"auto:", "profile:", "credential:"}) {
    const std::string generated(prefix);
    if (id.size() != generated.size() + 64 || id.compare(0, generated.size(), generated) != 0) continue;
    if (std::all_of(id.begin() + generated.size(), id.end(), [](char c) { return digit(c) || (c >= 'a' && c <= 'f'); })) return id.substr(generated.size(), 8);
  }
  return id.size() > 24 ? id.substr(0, 15) + "\u2026" + id.substr(id.size() - 6) : id;
}

std::string providerLabel(const std::string& provider) {
  for (const auto& known : contract::Providers) if (provider == known.id) return known.name;
  return provider;
}

std::string sharedLabel(const std::string& key) {
  for (const auto& label : contract::Labels) if (key == label.key) return label.text;
  return key;
}

std::string stateLabel(const std::string& state) {
  const auto* rule = stateRule(state);
  return rule ? rule->label : state;
}
std::string stateTone(const std::string& state) {
  const auto* rule = stateRule(state);
  return rule ? rule->tone : "neutral";
}
bool needsAttention(const std::string& state) { return attentionRank(state) != std::numeric_limits<unsigned>::max(); }

namespace {
std::string groupInteger(std::string value) {
  if (value.find('e') != std::string::npos) return value;
  const auto dot = value.find('.');
  const auto length = dot == std::string::npos ? value.size() : dot;
  const int sign = !value.empty() && value.front() == '-' ? 1 : 0;
  for (int at = static_cast<int>(length) - 3; at > sign; at -= 3) value.insert(at, ",");
  return value;
}

std::string decimalValue(double value, int precision) {
  // Round the shortest decimal representation, as Intl does. Multiplication by
  // 100 first would turn 1.005 into 100.49999999999999 and round it incorrectly.
  char buffer[64];
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), std::abs(value));
  std::string digits(buffer, converted.ptr);
  int exponent = 0;
  const auto exponentAt = digits.find('e');
  if (exponentAt != std::string::npos) {
    exponent = std::stoi(digits.substr(exponentAt + 1));
    digits.resize(exponentAt);
  }
  const auto dot = digits.find('.');
  const int decimalAt = static_cast<int>(dot == std::string::npos ? digits.size() : dot) + exponent;
  if (dot != std::string::npos) digits.erase(dot, 1);
  const int keep = decimalAt + precision;
  if (keep < 0) digits = "0";
  else if (static_cast<std::size_t>(keep) < digits.size()) {
    const bool roundUp = digits[keep] >= '5';
    digits.resize(keep);
    if (roundUp) {
      auto index = digits.size();
      while (index && digits[index - 1] == '9') digits[--index] = '0';
      if (index) ++digits[index - 1];
      else digits.insert(0, "1");
    }
  } else digits.append(static_cast<std::size_t>(keep) - digits.size(), '0');
  if (digits.size() <= static_cast<std::size_t>(precision)) digits.insert(0, precision + 1 - digits.size(), '0');
  const auto integerSize = digits.size() - precision;
  if (precision) {
    digits.insert(integerSize, ".");
    while (digits.back() == '0') digits.pop_back();
    if (digits.back() == '.') digits.pop_back();
  }
  return groupInteger(std::signbit(value) ? "-" + digits : digits);
}
}  // namespace

MetricTone metricTone(const std::string& key, std::optional<double> value) {
  for (const auto& rule : contract::MeasurementTones) {
    if (key.compare(0, std::char_traits<char>::length(rule.keyPrefix), rule.keyPrefix) != 0) continue;
    if (!value || !(*value >= rule.warningFrom)) return MetricTone::Normal;
    return *value >= rule.dangerFrom ? MetricTone::Danger : MetricTone::Warning;
  }
  return MetricTone::Normal;
}

std::string metricValue(const Metric& metric, bool compact) {
  if (!metric.value || !std::isfinite(*metric.value)) return "—";
  const double value = *metric.value;
  char buffer[64];
  for (const auto& precision : contract::UnitPrecisions) {
    if (metric.unit != precision.unit) continue;
    return decimalValue(value, precision.fractionDigits);
  }
  if (!compact) {
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value,
        value != 0 && std::abs(value) < 0.001 ? std::chars_format::general : std::chars_format::fixed);
    return groupInteger(std::string(buffer, converted.ptr));
  }
  if (std::abs(value) >= 1e9) std::snprintf(buffer, sizeof(buffer), "%.1fG", value / 1e9);
  else if (std::abs(value) >= 1e6) std::snprintf(buffer, sizeof(buffer), "%.1fM", value / 1e6);
  else if (std::abs(value) >= 10000) std::snprintf(buffer, sizeof(buffer), "%.1fk", value / 1e3);
  else if (std::abs(value) >= 100 || std::floor(value) == value) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
  else if (std::abs(value) > 0 && std::abs(value) < 0.1) std::snprintf(buffer, sizeof(buffer), "%.2g", value);
  else std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

std::string metricText(const Metric& metric, bool compact) {
  const bool hasValue = metric.value && std::isfinite(*metric.value);
  const bool adjacent = metric.unit.rfind("%", 0) == 0 || metric.unit.rfind("°", 0) == 0;
  return metricValue(metric, compact) + (hasValue && !metric.unit.empty() ? (adjacent ? "" : " ") + metric.unit : "");
}

std::string ageLabel(std::uint64_t milliseconds) {
  if (milliseconds < 15000) return "Just now";
  if (milliseconds < 60000) return std::to_string(milliseconds / 1000) + "s ago";
  if (milliseconds < 3600000) return std::to_string(milliseconds / 60000) + "m ago";
  if (milliseconds < 86400000) return std::to_string(milliseconds / 3600000) + "h ago";
  return std::to_string(milliseconds / 86400000) + "d ago";
}

std::string resetCountdown(const Metric& metric, std::uint64_t observationAgeMs) {
  if (!metric.resetMinutes || !metric.value || !std::isfinite(*metric.value)) return {};
  const auto elapsed = observationAgeMs / 60000;
  const auto minutes = *metric.resetMinutes > elapsed ? *metric.resetMinutes - elapsed : 0;
  if (minutes == 0) return "now";
  if (minutes < 60) return std::to_string(minutes) + "m";
  const bool days = minutes >= 1440;
  const auto large = days ? minutes / 1440 : minutes / 60;
  const auto small = days ? minutes % 1440 / 60 : minutes % 60;
  return std::to_string(large) + (days ? "d" : "h") + (small ? std::to_string(small) + (days ? "h" : "m") : "");
}

std::string describeReset(const std::string& countdown) {
  if (countdown.empty()) return {};
  return countdown == "now" ? "Resets now" : "Resets in " + countdown;
}

std::string projectDetail(const ReportState& project) {
  const auto trim = [](const std::string& value) {
    const auto first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string() : value.substr(first, value.find_last_not_of(" \t") - first + 1);
  };
  const auto model = trim(project.model), tool = trim(project.tool);
  return model.empty() ? tool : tool.empty() ? model : model + " · " + tool;
}

std::string accountActivityLine(const Monitor& item, std::uint64_t elapsedMs) {
  if (const auto* project = accountActivity(item, elapsedMs)) {
    const auto detail = projectDetail(*project);
    if (!detail.empty()) return detail;
  }
  // "N projects": the panel footer also holds the state, so the shorter form fits.
  return item.projects.size() > 1 ? std::to_string(item.projects.size()) + " projects" : std::string();
}

namespace {
// Case-insensitive ASCII comparison first, as a locale collation puts "alpha" before "Zeta".
int compareTitles(const std::string& a, const std::string& b) {
  const auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
  const auto size = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < size; ++i) {
    const auto x = fold(static_cast<unsigned char>(a[i])), y = fold(static_cast<unsigned char>(b[i]));
    if (x != y) return x < y ? -1 : 1;
  }
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  return a.compare(b) < 0 ? -1 : a.compare(b) > 0 ? 1 : 0;
}
}  // namespace

bool cardBefore(const Monitor& a, const Monitor& b, std::uint64_t elapsedMs) {
  const bool accountA = a.selection.kind == ItemKind::Account, accountB = b.selection.kind == ItemKind::Account;
  if (accountA != accountB) return accountA;
  const auto stateA = monitorState(a, elapsedMs), stateB = monitorState(b, elapsedMs);
  const bool standbyA = stateA == StandbyState, standbyB = stateB == StandbyState;
  if (standbyA != standbyB) return standbyB;
  const bool attentionA = needsAttention(stateA), attentionB = needsAttention(stateB);
  if (attentionA != attentionB) return attentionA;
  // An account was last active when its project reported; a resource when the server received it.
  if (standbyA && accountA && a.observedAtMs != b.observedAtMs) return a.observedAtMs > b.observedAtMs;
  if (standbyA && !accountA && a.ageMs != b.ageMs) return a.ageMs < b.ageMs;
  const auto title = compareTitles(monitorTitle(a, elapsedMs), monitorTitle(b, elapsedMs));
  if (title != 0) return title < 0;
  return a.selection.id < b.selection.id;
}

std::array<Selection, PanelCount> defaultPanels(const Catalog& catalog) {
  std::array<Selection, PanelCount> result{};
  std::size_t next = 0;
  // Alternate the two categories, agents first, so the first dashboard includes both when available.
  std::array<std::size_t, 2> cursor{};
  for (std::size_t round = 0; next < PanelCount && round < PanelCount; ++round) {
    for (std::size_t group = 0; group < 2 && next < PanelCount; ++group) {
      auto& index = cursor[group];
      while (index < catalog.items.size() && catalog.items[index].selection.kind != (group ? ItemKind::Resource : ItemKind::Account)) ++index;
      if (index < catalog.items.size()) result[next++] = catalog.items[index++].selection;
    }
  }
  return result;
}

}  // namespace vibemon
