#pragma once

#include "../domain/firmware.h"
#include "../domain/model.h"
#include <memory>

namespace vibemon {

enum class LoadResult { Missing, Loaded, Invalid, Unavailable };
class SettingsStore {
 public:
  virtual ~SettingsStore() = default;
  virtual LoadResult load(Config& config) = 0;
  virtual bool save(const Config& config) = 0;
};

enum class WifiLink { Offline, Connecting, Connected, Failed };
struct WifiStatus {
  WifiLink link = WifiLink::Offline;
  bool clockReady = false;
  std::string ssid;
};
class WifiPort {
 public:
  virtual ~WifiPort() = default;
  virtual void connect(const std::string& ssid, const std::string& password) = 0;
  virtual void disconnect() = 0;
  virtual void scan() = 0;
  virtual const WifiStatus& status() const = 0;
  virtual bool scanning() const = 0;
  virtual bool scanFailed() const = 0;
  virtual const std::vector<WifiNetwork>& networks() const = 0;
};

struct CloudRequest {
  std::uint32_t generation = 0;
  std::string origin;
  std::string token;
  // Sources and accounts to read. Empty reads every source.
  std::vector<Selection> scope;
  // Sent as If-None-Match. Empty sends an unconditional request.
  std::string ifNoneMatch;
};
enum class CloudError { None, Unauthorized, Forbidden, Network, Tls, InvalidResponse, TooLarge, Memory, Server, RateLimited, Redirected };
struct CloudResult {
  std::uint32_t generation = 0;
  CloudError error = CloudError::None;
  Catalog catalog;
  // One sentence that explains InvalidResponse or TooLarge. It never contains response content.
  std::string detail;
  // The server's requested delay before the next request. 0 when it gave none.
  std::uint32_t retryAfterMs = 0;
  // HTTP 304: no requested source changed since `ifNoneMatch`. `catalog` is empty.
  bool notModified = false;
  // The ETag of a 200 response. Empty when the header is missing or invalid.
  std::string etag;
  // `serverTime` of a 200 body or `X-Server-Time` of a 304, in UTC milliseconds.
  std::int64_t serverTimeMs = 0;
};
// VibeMon's device authorization (RFC 8628) for this device's client. A request
// without a device code asks for a new user code; one with a device code polls for
// the read token that the person approves on the server's /device page.
struct SignInRequest {
  std::uint32_t attempt = 0;
  std::string origin;
  std::string deviceCode;
};
enum class SignInError { None, Pending, SlowDown, Denied, Expired, Unsupported, Network, Tls, InvalidResponse, Server, RateLimited, Redirected };
struct DeviceCode {
  std::string deviceCode;
  std::string userCode;
  std::string verificationUri;
  std::string verificationUriComplete;
  std::uint32_t expiresInS = 0;
  std::uint32_t intervalS = 0;
};
struct SignInResult {
  std::uint32_t attempt = 0;
  SignInError error = SignInError::None;
  // Set when a request without a device code succeeds.
  DeviceCode code;
  // Set when a poll succeeds: a read token.
  std::string token;
  std::uint32_t retryAfterMs = 0;
};

// One worker serves workspace reads and sign-in requests in order, on one connection.
class CloudPort {
 public:
  virtual ~CloudPort() = default;
  virtual bool start(const CloudRequest& request) = 0;
  virtual std::unique_ptr<CloudResult> take() = 0;
  virtual bool startSignIn(const SignInRequest& request) = 0;
  virtual std::unique_ptr<SignInResult> takeSignIn() = 0;
};

enum class UpdateError { None, Network, InvalidRelease, Download, Integrity, Storage };
struct UpdateResult {
  UpdateError error = UpdateError::None;
  // A check returns the published release. An install returns the release
  // that the next boot starts.
  FirmwareRelease release;
};
// Downloads run on a worker. Each call to check() or install() produces one result.
class UpdatePort {
 public:
  virtual ~UpdatePort() = default;
  virtual bool check() = 0;
  virtual bool install(const FirmwareRelease& release) = 0;
  virtual std::unique_ptr<UpdateResult> take() = 0;
  virtual std::uint8_t progress() const = 0;  // 0-100 while an install runs
  // Keeps the running image. Without this call, the bootloader returns to the
  // previous image on the next restart.
  // False only when a trial image cannot store its confirmation.
  virtual bool confirm() = 0;
  virtual void restart() = 0;
};

}  // namespace vibemon
