#include "sign_in.h"

namespace vibemon {
namespace {
// RFC 8628 3.5: each slow_down adds 5 seconds to the polling interval.
constexpr std::uint32_t SlowDownMs = 5000;
// A failed start request and a lost poll wait this long before the next try.
constexpr std::uint32_t RetryMs = 5000;

bool due(std::uint32_t now, std::uint32_t at) { return static_cast<std::int32_t>(now - at) >= 0; }

std::string transient(SignInError error) {
  switch (error) {
    case SignInError::Network: return "The server could not be reached. Retrying...";
    case SignInError::Tls: return "The server's certificate could not be verified. Retrying...";
    case SignInError::Server: return "The server is unavailable. Retrying...";
    case SignInError::RateLimited: return "The server asked to wait. Retrying...";
    default: return {};
  }
}
}  // namespace

void DeviceSignIn::set(SignInStage stage, const std::string& message) {
  view_.stage = stage; view_.message = message; ++revision_;
}

void DeviceSignIn::start(const std::string& origin, std::uint32_t now) {
  ++attempt_;
  origin_ = origin; deviceCode_.clear(); inFlight_ = false; nextAt_ = now;
  view_ = {};
  set(SignInStage::Requesting, "Getting a code...");
}

void DeviceSignIn::cancel() {
  // A reply to this attempt is discarded when it arrives.
  ++attempt_;
  deviceCode_.clear(); inFlight_ = false;
  view_ = {};
  ++revision_;
}

void DeviceSignIn::fail(const std::string& message) {
  ++attempt_;
  deviceCode_.clear(); inFlight_ = false;
  set(SignInStage::Failed, message);
}

std::optional<std::string> DeviceSignIn::tick(std::uint32_t now) {
  std::optional<std::string> token;
  if (auto result = cloud_.takeSignIn()) {
    if (result->attempt == attempt_ && inFlight_) {
      inFlight_ = false;
      if (result->error == SignInError::None && !result->token.empty()) {
        token = std::move(result->token);
        deviceCode_.clear();
        set(SignInStage::Approved, "Approved. Connecting...");
      } else apply(*result, now);
    }
  }
  if (!active() || inFlight_) return token;
  if (view_.stage == SignInStage::Waiting && due(now, expiresAt_)) {
    fail("The code expired. Get a new code to try again.");
    return token;
  }
  if (!due(now, nextAt_)) return token;
  if (cloud_.startSignIn({attempt_, origin_, deviceCode_})) inFlight_ = true;
  return token;
}

void DeviceSignIn::apply(const SignInResult& result, std::uint32_t now) {
  const auto retry = now + (result.retryAfterMs ? result.retryAfterMs : RetryMs);
  if (view_.stage == SignInStage::Requesting) {
    if (result.error == SignInError::None) {
      deviceCode_ = result.code.deviceCode;
      view_.userCode = result.code.userCode;
      view_.verificationUri = result.code.verificationUri;
      view_.verificationUriComplete = result.code.verificationUriComplete;
      intervalMs_ = result.code.intervalS * 1000;
      expiresAt_ = now + result.code.expiresInS * 1000;
      nextAt_ = now + intervalMs_;
      set(SignInStage::Waiting, {});
    } else if (result.error == SignInError::Unsupported) {
      fail("This server does not support sign-in with a code. Enter a read token instead.");
    } else if (!transient(result.error).empty()) {
      nextAt_ = retry;
      set(SignInStage::Requesting, transient(result.error));
    } else {
      fail(result.error == SignInError::Redirected ? "The server redirected the request. Check the server address." : "The server sent an invalid reply. Check the server address.");
    }
    return;
  }
  switch (result.error) {
    case SignInError::Pending: nextAt_ = now + intervalMs_; if (!view_.message.empty()) set(SignInStage::Waiting, {}); return;
    case SignInError::SlowDown: intervalMs_ += SlowDownMs; nextAt_ = now + intervalMs_; return;
    case SignInError::Denied: fail("The request was denied. Get a new code to try again."); return;
    case SignInError::Expired: fail("The code expired. Get a new code to try again."); return;
    case SignInError::Unsupported: fail("This server does not support sign-in with a code. Enter a read token instead."); return;
    default: break;
  }
  if (!transient(result.error).empty()) {
    nextAt_ = retry > now + intervalMs_ ? retry : now + intervalMs_;
    set(SignInStage::Waiting, transient(result.error));
  } else fail(result.error == SignInError::Redirected ? "The server redirected the request. Check the server address." : "The server sent an invalid reply. Get a new code to try again.");
}

}  // namespace vibemon
