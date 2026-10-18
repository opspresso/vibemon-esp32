#pragma once
#include "../application/ports.h"
#include <optional>
#include <string>
#include <vector>
#include <string_view>

namespace vibemon {
// Longest server-requested delay that the device honors.
constexpr std::uint32_t MaxRetryAfterMs = 15u * 60 * 1000;
// Request path for GET /api/v1/sources. A scope adds the `ids` and `accounts`
// filters; Web then returns only those sources and the accounts' projects.
std::string sourcesPath(const std::vector<Selection>& scope);
// Maps a received HTTP status of GET /api/v1/sources to a cloud error.
// Transport failures have no status; the platform classifies them.
CloudError cloudStatusError(int status);
// Longest ETag that the device stores and sends back.
constexpr std::size_t MaxEntityTagBytes = 256;
// Returns `value` without surrounding spaces when it is one strong or weak
// entity tag of visible ASCII. Otherwise returns an empty string, so the next
// request is unconditional.
std::string entityTag(std::string_view value);
// Completes a 304 reply. It is valid only for a request that sent
// If-None-Match and only with a valid `X-Server-Time`; otherwise the result is
// InvalidResponse.
void notModifiedResult(bool conditional, std::string_view serverTime, CloudResult& result);
// Reads a Retry-After value in delta-seconds, capped at MaxRetryAfterMs.
// HTTP dates and malformed values return no value.
std::optional<std::uint32_t> retryAfterMs(std::string_view value);
}  // namespace vibemon
