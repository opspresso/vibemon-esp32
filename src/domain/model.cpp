#include "model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

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
  if (value.empty() || value.size() > 253) return {false, "Enter your VibeMon server address."};
  if (value.find("://") == std::string::npos) value = "https://" + value;
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
  if (colon != std::string::npos) {
    const auto port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), digit)) return {false, "Enter a valid HTTPS port."};
    unsigned valuePort = 0;
    for (char c : port) valuePort = valuePort * 10 + static_cast<unsigned>(c - '0');
    if (!valuePort || valuePort > 65535) return {false, "HTTPS port must be 1 to 65535."};
  }
  for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
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

bool stale(const Monitor& item, std::uint32_t elapsedMs) {
  return item.reportedStale || static_cast<std::uint64_t>(item.ageMs) + elapsedMs >= StaleAfterMs;
}

std::string activityLabel(const Monitor& item, std::uint32_t elapsedMs) {
  std::string state = item.activity;
  const std::uint64_t age = static_cast<std::uint64_t>(item.activityAgeMs) + elapsedMs;
  const bool active = state == "thinking" || state == "planning" || state == "working" || state == "notification" || state == "packing" || state == "alert";
  const auto idleAfter = state == "start" || state == "done" ? 60000 : active ? 300000 : state == "idle" ? 0 : -1;
  if (idleAfter >= 0 && age >= static_cast<std::uint64_t>(idleAfter + 300000)) state = "sleep";
  else if (idleAfter >= 0 && age >= static_cast<std::uint64_t>(idleAfter)) state = "idle";
  if (state == "notification") return "Needs input";
  if (state == "sleep") return "Resting";
  if (state.empty()) return "Unknown";
  if (state[0] >= 'a' && state[0] <= 'z') state[0] = static_cast<char>(state[0] - 'a' + 'A');
  return state;
}

std::string metricValue(const Metric& metric) {
  if (!metric.value || !std::isfinite(*metric.value)) return "--";
  const double value = *metric.value;
  char buffer[64];
  if (std::abs(value) >= 1e9) std::snprintf(buffer, sizeof(buffer), "%.1fG", value / 1e9);
  else if (std::abs(value) >= 1e6) std::snprintf(buffer, sizeof(buffer), "%.1fM", value / 1e6);
  else if (std::abs(value) >= 10000) std::snprintf(buffer, sizeof(buffer), "%.1fk", value / 1e3);
  else if (std::abs(value) >= 100 || std::floor(value) == value) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
  else if (std::abs(value) > 0 && std::abs(value) < 0.1) std::snprintf(buffer, sizeof(buffer), "%.2g", value);
  else std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

std::string ageLabel(std::uint64_t milliseconds) {
  if (milliseconds < 60000) return "Just now";
  if (milliseconds < 3600000) return std::to_string(milliseconds / 60000) + "m ago";
  if (milliseconds < 86400000) return std::to_string(milliseconds / 3600000) + "h ago";
  return std::to_string(milliseconds / 86400000) + "d ago";
}

std::array<Selection, PanelCount> defaultPanels(const Catalog& catalog) {
  std::array<Selection, PanelCount> result{};
  std::size_t next = 0;
  // Alternate the two categories so the first dashboard includes both when available.
  std::array<std::size_t, 2> cursor{};
  for (std::size_t round = 0; next < PanelCount && round < PanelCount; ++round) {
    for (std::size_t group = 0; group < 2 && next < PanelCount; ++group) {
      auto& index = cursor[group];
      while (index < catalog.items.size() && catalog.items[index].selection.kind != (group ? ItemKind::Account : ItemKind::Resource)) ++index;
      if (index < catalog.items.size()) result[next++] = catalog.items[index++].selection;
    }
  }
  return result;
}

}  // namespace vibemon
