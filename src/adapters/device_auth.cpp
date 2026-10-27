#include "device_auth.h"
#include <ArduinoJson.h>
#include <algorithm>

namespace vibemon {
namespace {
std::string formValue(const std::string& value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char c : value) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') encoded += static_cast<char>(c);
    else if (c == ' ') encoded += '+';
    else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
  }
  return encoded;
}

bool parse(std::string_view body, JsonDocument& document) {
  return !body.empty() && body.size() <= MaxDeviceReplyBytes && !deserializeJson(document, body.data(), body.size(), DeserializationOption::NestingLimit(1)) && document.is<JsonObject>();
}

bool printable(const std::string& value) {
  return std::all_of(value.begin(), value.end(), [](char c) { return c > 0x20 && c < 0x7f; });
}

// VibeMon user codes: two groups of four consonants, such as BCDF-GHJK.
bool userCode(const std::string& value) {
  constexpr std::string_view alphabet = "BCDFGHJKLMNPQRSTVWXZ";
  if (value.size() != 9 || value[4] != '-') return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (i != 4 && alphabet.find(value[i]) == std::string_view::npos) return false;
  }
  return true;
}

// The page must belong to the server that issued the code, so a person never
// approves a code on another site.
bool verificationPage(const std::string& value, const std::string& origin) {
  return value.size() <= MaxVerificationUriBytes && printable(value) && value.compare(0, origin.size(), origin) == 0 && value.size() > origin.size() && value[origin.size()] == '/';
}

SignInError statusError(int status) {
  if (status == 429) return SignInError::RateLimited;
  if (status >= 300 && status < 400) return SignInError::Redirected;
  if (status >= 500) return SignInError::Server;
  return SignInError::InvalidResponse;
}

// RFC 6749 5.2 and RFC 8628 3.5 error codes. A server that does not know this
// client or its scope cannot sign in this device.
SignInError oauthError(const JsonDocument& document) {
  const auto error = document["error"].as<std::string>();
  if (error == "authorization_pending") return SignInError::Pending;
  if (error == "slow_down") return SignInError::SlowDown;
  if (error == "access_denied") return SignInError::Denied;
  if (error == "expired_token" || error == "invalid_grant") return SignInError::Expired;
  if (error == "invalid_client" || error == "invalid_scope" || error == "unauthorized_client" || error == "unsupported_grant_type") return SignInError::Unsupported;
  return SignInError::InvalidResponse;
}

template <typename T> bool integer(const JsonVariantConst value, T low, T high, T& output) {
  if (!value.is<T>() || value.as<T>() < low || value.as<T>() > high) return false;
  output = value.as<T>();
  return true;
}
}  // namespace

std::string deviceStartBody(const std::string& deviceName) {
  return std::string("client_id=") + DeviceClientId + "&scope=read&device_name=" + formValue(deviceName);
}

std::string deviceTokenBody(const std::string& deviceCode) {
  return std::string("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&client_id=") + DeviceClientId + "&device_code=" + formValue(deviceCode);
}

SignInResult decodeDeviceStart(int status, std::string_view body, const std::string& origin) {
  SignInResult result;
  JsonDocument document;
  // A server without device authorization for this client answers 404 or invalid_client.
  if (status == 404) { result.error = SignInError::Unsupported; return result; }
  if (status != 200) { result.error = status == 400 && parse(body, document) ? oauthError(document) : statusError(status); return result; }
  auto& code = result.code;
  if (!parse(body, document) || !document["device_code"].is<const char*>() || !document["user_code"].is<const char*>() ||
      !document["verification_uri"].is<const char*>() || !document["verification_uri_complete"].is<const char*>() ||
      !integer<std::uint32_t>(document["expires_in"], 60, 3600, code.expiresInS) || !integer<std::uint32_t>(document["interval"], 1, 60, code.intervalS)) {
    result.error = SignInError::InvalidResponse;
    return result;
  }
  code.deviceCode = document["device_code"].as<std::string>();
  code.userCode = document["user_code"].as<std::string>();
  code.verificationUri = document["verification_uri"].as<std::string>();
  code.verificationUriComplete = document["verification_uri_complete"].as<std::string>();
  if (code.deviceCode.empty() || code.deviceCode.size() > 100 || !printable(code.deviceCode) || !userCode(code.userCode) ||
      !verificationPage(code.verificationUri, origin) || !verificationPage(code.verificationUriComplete, origin)) {
    result.code = {};
    result.error = SignInError::InvalidResponse;
  }
  return result;
}

SignInResult decodeDeviceToken(int status, std::string_view body) {
  SignInResult result;
  JsonDocument document;
  if (status == 400 && parse(body, document)) { result.error = oauthError(document); return result; }
  if (status != 200) { result.error = statusError(status); return result; }
  const bool valid = parse(body, document) && document["access_token"].is<const char*>() && document["token_type"].as<std::string>() == "Bearer" &&
                     document["scope"].as<std::string>() == "read" && validateToken(document["access_token"].as<std::string>()).ok;
  if (!valid) { result.error = SignInError::InvalidResponse; return result; }
  result.token = document["access_token"].as<std::string>();
  return result;
}
}  // namespace vibemon
