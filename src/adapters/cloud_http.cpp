#include "cloud_http.h"

namespace vibemon {
CloudError cloudStatusError(int status) {
  if (status == 200) return CloudError::None;
  if (status == 401) return CloudError::Unauthorized;
  if (status == 403) return CloudError::Forbidden;
  if (status == 429) return CloudError::RateLimited;
  if (status >= 300 && status < 400) return CloudError::Redirected;
  if (status >= 500) return CloudError::Server;
  return CloudError::InvalidResponse;
}

std::optional<std::uint32_t> retryAfterMs(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  if (value.empty() || value.size() > 10) return std::nullopt;
  std::uint64_t seconds = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') return std::nullopt;
    seconds = seconds * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return seconds * 1000 >= MaxRetryAfterMs ? MaxRetryAfterMs : static_cast<std::uint32_t>(seconds * 1000);
}
}  // namespace vibemon
