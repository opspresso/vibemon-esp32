#pragma once

#include "sign_in.h"
#include "updater.h"
#include <map>

namespace vibemon {

enum class Connection { NeedWifi, ConnectingWifi, WifiUnavailable, SettingClock, NeedCloud, CheckingCloud, Online, CloudUnavailable, TokenRejected, StorageError, ClockUnavailable };

struct Workspace {
  Connection connection = Connection::NeedWifi;
  std::string message;
  std::string ssid;
  std::string origin;
  bool tokenConfigured = false;
  bool refreshing = false;
  bool hasSnapshot = false;
  // False while the catalog holds only the selected panels' items.
  bool catalogComplete = false;
  // Changes only when a complete HTTP response accepts or revalidates the catalog.
  std::uint32_t catalogRevision = 0;
  std::uint32_t revision = 0;
  std::uint64_t snapshotAgeMs = 0;
  std::array<Selection, PanelCount> panels{};
  ScreenRotation rotation = ScreenRotation::Deg0;
  Catalog catalog;
  FirmwareView firmware;
};

class Controller {
 public:
  Controller(SettingsStore& settings, WifiPort& wifi, CloudPort& cloud, UpdatePort& update, std::string version);
  void begin(std::uint32_t now);
  void tick(std::uint32_t now);
  Validation saveWifi(const std::string& ssid, const std::string& password);
  Validation saveCloud(const std::string& origin, const std::string& token);
  // Signs in with a code that the person approves on the server's /device page.
  // The approved read token is checked and saved like one entered by hand.
  Validation startSignIn(const std::string& origin);
  void cancelSignIn();
  const SignInView& signIn() const { return signIn_.view(); }
  Validation select(std::size_t panel, const Selection& selection);
  // Saves the screen rotation. The display adapter applies workspace().rotation.
  Validation rotate(ScreenRotation rotation);
  void refresh();
  void retryWifi();
  void cancelSetup();
  void scanWifi();
  // While browsing, polls read every item for the picker. Otherwise they read
  // only the selected panels' items.
  void browse(bool browsing);
  void checkFirmware() { updater_.checkNow(); }
  const std::string& firmwareVersion() const { return updater_.version(); }
  const Workspace& workspace() const { return workspace_; }
  std::uint64_t snapshotAgeMs(std::uint32_t now) const {
    return workspace_.snapshotAgeMs + static_cast<std::uint32_t>(now - now_);
  }
  const std::vector<WifiNetwork>& networks() const { return wifi_.networks(); }
  bool scanning() const { return wifi_.scanning(); }
  bool scanFailed() const { return wifi_.scanFailed(); }
  bool changingWifi() const { return pendingWifi_.has_value(); }
  bool changingCloud() const { return pendingCloud_.has_value(); }

 private:
  struct WifiCredentials { std::string ssid; std::string password; };
  struct CloudCredentials { std::string origin; std::string token; };
  // The ETag of the displayed catalog and the exact request that returned it.
  struct Validator {
    std::uint32_t generation;
    std::string origin;
    std::vector<Selection> scope;
    std::string etag;
    // The catalog's serverTime, at which its item ages were measured.
    std::int64_t serverTimeMs;
  };
  SettingsStore& settings_;
  WifiPort& wifi_;
  CloudPort& cloud_;
  Updater updater_;
  DeviceSignIn signIn_;
  std::uint32_t signInRevision_ = 0;
  Config config_;
  Workspace workspace_;
  std::optional<WifiCredentials> pendingWifi_;
  std::optional<CloudCredentials> pendingCloud_;
  std::optional<Validator> validator_;
  // The origin, scope and If-None-Match of the request in flight.
  std::string requestedOrigin_;
  std::vector<Selection> requestedScope_;
  bool conditional_ = false;
  std::uint32_t generation_ = 1;
  std::uint32_t now_ = 0;
  std::uint32_t nextPoll_ = 0;
  std::uint32_t clockStartedAt_ = 0;
  unsigned failures_ = 0;
  // Keep each server's delay while another workspace is checked or restored.
  std::map<std::string, std::uint32_t> retryAfter_;
  bool inFlight_ = false;
  // Rejection of the saved credential; candidate checks do not replace it.
  std::optional<CloudError> tokenRejected_;
  bool storageBlocked_ = false;
  bool hadConnection_ = false;
  bool browsing_ = false;
  bool requestedAll_ = true;
  bool refreshRequested_ = false;
  void updateConfigView();
  std::vector<Selection> requestScope() const;
  std::optional<std::uint32_t> retryAfter();
  std::unique_ptr<CloudResult> takeCloudResult();
  void state(Connection connection, const std::string& message);
  void needCloud();
  void accept(CloudResult& result);
  void online();
  void failure(const CloudResult& result, bool validating);
};

}  // namespace vibemon
