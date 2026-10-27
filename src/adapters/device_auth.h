#pragma once
#include "../application/ports.h"
#include <string_view>

namespace vibemon {
// VibeMon registers VibeMon Touch as a device-authorization client that may read only.
constexpr const char* DeviceClientId = "vibemon-touch";
constexpr const char* DeviceStartPath = "/api/oauth/device";
constexpr const char* DeviceTokenPath = "/api/oauth/token";
constexpr std::size_t MaxDeviceReplyBytes = 2048;
constexpr std::size_t MaxVerificationUriBytes = MaxOriginBytes + 32;

// application/x-www-form-urlencoded bodies of the two requests (RFC 8628 3.1 and 3.4).
std::string deviceStartBody(const std::string& deviceName);
std::string deviceTokenBody(const std::string& deviceCode);
// Reads a reply to the start request; `origin` is the server that was asked.
SignInResult decodeDeviceStart(int status, std::string_view body, const std::string& origin);
// Reads a reply to a token poll. Pending and slow-down replies are errors that keep the request.
SignInResult decodeDeviceToken(int status, std::string_view body);
}  // namespace vibemon
