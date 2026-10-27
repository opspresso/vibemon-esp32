#pragma once

#include "ports.h"
#include <optional>

namespace vibemon {

enum class SignInStage { Idle, Requesting, Waiting, Approved, Failed };
struct SignInView {
  SignInStage stage = SignInStage::Idle;
  std::string userCode;
  std::string verificationUri;
  std::string verificationUriComplete;
  // A sentence for the person; empty while nothing needs saying.
  std::string message;
};

// Device authorization for VibeMon Touch. It asks the server for a user code,
// shows it until the person approves it on /device, and hands over the read
// token once. The device code stays here; the view never contains it.
class DeviceSignIn {
 public:
  explicit DeviceSignIn(CloudPort& cloud) : cloud_(cloud) {}
  void start(const std::string& origin, std::uint32_t now);
  void cancel();
  void fail(const std::string& message);
  // Sends due requests and applies replies. Returns the read token once, when the person approves.
  std::optional<std::string> tick(std::uint32_t now);
  bool active() const { return view_.stage == SignInStage::Requesting || view_.stage == SignInStage::Waiting; }
  const std::string& origin() const { return origin_; }
  const SignInView& view() const { return view_; }
  // Changes whenever the view changes.
  std::uint32_t revision() const { return revision_; }

 private:
  CloudPort& cloud_;
  SignInView view_;
  std::string origin_;
  std::string deviceCode_;
  std::uint32_t attempt_ = 0;
  std::uint32_t nextAt_ = 0;
  std::uint32_t expiresAt_ = 0;
  std::uint32_t intervalMs_ = 0;
  std::uint32_t revision_ = 0;
  bool inFlight_ = false;
  void set(SignInStage stage, const std::string& message);
  void apply(const SignInResult& result, std::uint32_t now);
};

}  // namespace vibemon
