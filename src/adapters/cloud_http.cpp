#include "cloud_http.h"

namespace vibemon {
namespace {
std::string_view trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  return value;
}
}  // namespace

std::string sourcesPath(const std::vector<Selection>& scope) {
  std::string ids, accounts;
  for (const auto& selection : scope) {
    // Source and account IDs contain only URL-safe characters.
    auto& list = selection.kind == ItemKind::Account ? accounts : ids;
    if (!list.empty()) list += ',';
    list += selection.id;
  }
  std::string path = "/api/v1/sources";
  if (!ids.empty()) path += "?ids=" + ids;
  if (!accounts.empty()) path += (ids.empty() ? "?accounts=" : "&accounts=") + accounts;
  return path;
}

CloudError cloudStatusError(int status) {
  if (status == 200) return CloudError::None;
  if (status == 401) return CloudError::Unauthorized;
  if (status == 403) return CloudError::Forbidden;
  if (status == 429) return CloudError::RateLimited;
  if (status >= 300 && status < 400) return CloudError::Redirected;
  if (status >= 500) return CloudError::Server;
  return CloudError::InvalidResponse;
}

std::string entityTag(std::string_view value) {
  value = trim(value);
  auto opaque = value;
  if (opaque.substr(0, 2) == "W/") opaque.remove_prefix(2);
  if (value.size() > MaxEntityTagBytes || opaque.size() < 2 || opaque.front() != '"' || opaque.back() != '"') return {};
  // RFC 9110 etagc, without obs-text: no quote, space or control character.
  for (const char c : opaque.substr(1, opaque.size() - 2)) {
    if (c < 0x21 || c == '"' || c > 0x7e) return {};
  }
  return std::string(value);
}

void notModifiedResult(bool conditional, std::string_view serverTime, CloudResult& result) {
  result.catalog = {};
  result.etag.clear();
  serverTime = trim(serverTime);
  const auto time = serverTime.size() <= 24 ? utcMilliseconds(std::string(serverTime)) : std::nullopt;
  if (!conditional) {
    result.error = CloudError::InvalidResponse;
    result.detail = "It reported no change for an unconditional request.";
  } else if (!time) {
    result.error = CloudError::InvalidResponse;
    result.detail = "It reported no change without a valid server time.";
  } else {
    result.error = CloudError::None;
    result.notModified = true;
    result.serverTimeMs = *time;
  }
}

std::optional<std::uint32_t> retryAfterMs(std::string_view value) {
  value = trim(value);
  if (value.empty() || value.size() > 10) return std::nullopt;
  std::uint64_t seconds = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') return std::nullopt;
    seconds = seconds * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return seconds * 1000 >= MaxRetryAfterMs ? MaxRetryAfterMs : static_cast<std::uint32_t>(seconds * 1000);
}
}  // namespace vibemon
