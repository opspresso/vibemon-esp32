#pragma once

#include "updater.h"

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
  std::uint32_t revision = 0;
  std::uint64_t snapshotAgeMs = 0;
  std::array<Selection, PanelCount> panels{};
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
  Validation select(std::size_t panel, const Selection& selection);
  void refresh();
  void retryWifi();
  void cancelSetup();
  void scanWifi();
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
  SettingsStore& settings_;
  WifiPort& wifi_;
  CloudPort& cloud_;
  Updater updater_;
  Config config_;
  Workspace workspace_;
  std::optional<WifiCredentials> pendingWifi_;
  std::optional<CloudCredentials> pendingCloud_;
  std::uint32_t generation_ = 1;
  std::uint32_t now_ = 0;
  std::uint32_t nextPoll_ = 0;
  std::uint32_t clockStartedAt_ = 0;
  unsigned failures_ = 0;
  bool inFlight_ = false;
  bool tokenRejected_ = false;
  bool storageBlocked_ = false;
  bool hadConnection_ = false;
  void updateConfigView();
  void state(Connection connection, const std::string& message);
  void needCloud();
  void accept(CloudResult& result);
  void failure(CloudError error, bool validating);
};

}  // namespace vibemon
