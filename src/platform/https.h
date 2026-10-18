#pragma once
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace vibemon {
// Verifies servers with the ESP32 core's CA bundle and hostname checks.
// Callers choose the redirect policy.
inline void prepareHttps(NetworkClientSecure& client, HTTPClient& http, std::uint16_t readTimeoutMs) {
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(8);
  http.setConnectTimeout(4000);
  http.setTimeout(readTimeoutMs);
}
}  // namespace vibemon
