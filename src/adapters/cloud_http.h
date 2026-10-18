#pragma once
#include "../application/ports.h"
#include <optional>
#include <string_view>

namespace vibemon {
// Longest server-requested delay that the device honors.
constexpr std::uint32_t MaxRetryAfterMs = 15u * 60 * 1000;
// Maps a received HTTP status of GET /api/v1/sources to a cloud error.
// Transport failures have no status; the platform classifies them.
CloudError cloudStatusError(int status);
// Reads a Retry-After value in delta-seconds, capped at MaxRetryAfterMs.
// HTTP dates and malformed values return no value.
std::optional<std::uint32_t> retryAfterMs(std::string_view value);
}  // namespace vibemon
