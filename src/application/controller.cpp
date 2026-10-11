#include "controller.h"

#include <algorithm>
#include <utility>

namespace vibemon {
namespace {
bool validConfig(const Config& config) {
  std::string origin;
  if (!normalizeOrigin(config.origin, origin).ok || origin != config.origin || config.brightness < 10 || config.brightness > 100) return false;
  if (!config.ssid.empty() && !validateWifi(config.ssid, config.wifiPassword).ok) return false;
  if (!config.readToken.empty() && !validateToken(config.readToken).ok) return false;
  for (const auto& selection : config.panels) {
    if (!selection.id.empty() && !(selection.kind == ItemKind::Account ? validAccountId(selection.id) : validSourceId(selection.id))) return false;
  }
  return true;
}
bool due(std::uint32_t now, std::uint32_t deadline) { return static_cast<std::int32_t>(now - deadline) >= 0; }
}  // namespace

Controller::Controller(SettingsStore& settings, WifiPort& wifi, CloudPort& cloud)
    : settings_(settings), wifi_(wifi), cloud_(cloud) {}

void Controller::state(Connection connection, const std::string& message) {
  if (workspace_.connection == connection && workspace_.message == message) return;
  workspace_.connection = connection;
  workspace_.message = message;
  ++workspace_.revision;
}

void Controller::updateConfigView() {
  workspace_.ssid = config_.ssid;
  workspace_.origin = config_.origin;
  workspace_.tokenConfigured = !config_.readToken.empty();
  workspace_.panels = config_.panels;
  ++workspace_.revision;
}

void Controller::begin(std::uint32_t now) {
  now_ = nextPoll_ = now;
  Config loaded;
  const auto result = settings_.load(loaded);
  const bool usable = result == LoadResult::Loaded && validConfig(loaded);
  if (usable) config_ = std::move(loaded);
  updateConfigView();
  if (result == LoadResult::Invalid || result == LoadResult::Unavailable || (result == LoadResult::Loaded && !usable)) {
    state(Connection::StorageError, "Saved settings could not be read. Configure this device again.");
    wifi_.scan();
  } else if (config_.ssid.empty()) {
    state(Connection::NeedWifi, "Connect to a 2.4 GHz Wi-Fi network.");
    wifi_.scan();
  } else retryWifi();
}

Validation Controller::saveWifi(const std::string& ssid, const std::string& password) {
  const auto checked = validateWifi(ssid, password);
  if (!checked.ok) return checked;
  pendingWifi_ = WifiCredentials{ssid, password};
  storageBlocked_ = false;
  ++generation_;
  inFlight_ = false;
  workspace_.refreshing = false;
  pendingCloud_.reset();
  wifi_.connect(ssid, password);
  state(Connection::ConnectingWifi, "Connecting to Wi-Fi...");
  return {true, {}};
}

Validation Controller::saveCloud(const std::string& origin, const std::string& token) {
  std::string normalized;
  const auto checked = normalizeOrigin(origin, normalized);
  if (!checked.ok) return checked;
  // A blank field retains a saved key only for the same origin.
  const auto candidateToken = token.empty() && normalized == config_.origin ? config_.readToken : token;
  const auto checkedToken = validateToken(candidateToken);
  if (!checkedToken.ok) return checkedToken;
  const auto wifi = wifi_.status();
  if (wifi.link != WifiLink::Connected || pendingWifi_) return {false, "Connect Wi-Fi before checking your workspace."};
  pendingCloud_ = CloudCredentials{normalized, candidateToken};
  storageBlocked_ = false;
  ++generation_;
  inFlight_ = false;
  tokenRejected_ = false;
  workspace_.refreshing = false;
  nextPoll_ = now_;
  state(Connection::CheckingCloud, "Checking your read token...");
  return {true, {}};
}

Validation Controller::select(std::size_t panel, const Selection& selection) {
  if (panel >= PanelCount) return {false, "Choose one of the four panels."};
  if (!selection.id.empty() && !workspace_.catalog.find(selection)) return {false, "This item is no longer in your workspace. Refresh the list."};
  if (config_.panelsInitialized && config_.panels[panel] == selection) return {true, {}};
  Config candidate = config_;
  candidate.panels[panel] = selection;
  candidate.panelsInitialized = true;
  if (!settings_.save(candidate)) return {false, "Panel selection could not be saved. Try again."};
  config_ = std::move(candidate);
  storageBlocked_ = false;
  updateConfigView();
  return {true, {}};
}

void Controller::scanWifi() { wifi_.scan(); }

void Controller::retryWifi() {
  if (config_.ssid.empty()) { scanWifi(); return; }
  ++generation_;
  inFlight_ = false;
  workspace_.refreshing = false;
  pendingWifi_.reset();
  pendingCloud_.reset();
  wifi_.connect(config_.ssid, config_.wifiPassword);
  state(Connection::ConnectingWifi, "Connecting to Wi-Fi...");
}

void Controller::refresh() {
  nextPoll_ = now_;
  tokenRejected_ = false;
  failures_ = 0;
  storageBlocked_ = false;
}

void Controller::failure(CloudError error, bool validating) {
  if (validating) pendingCloud_.reset();
  if (error == CloudError::Unauthorized || error == CloudError::Forbidden) {
    // A rejected replacement must not revoke the previous, still-valid connection.
    tokenRejected_ = !validating || config_.readToken.empty();
    state(Connection::TokenRejected, error == CloudError::Forbidden ? "This token needs read permission." : "This token was rejected. Check or replace it.");
  } else {
    const char* message = error == CloudError::Tls ? "The server certificate could not be verified." :
        error == CloudError::InvalidResponse ? "The server returned an invalid workspace." :
        error == CloudError::TooLarge ? "The workspace exceeds the device's response or source limit." :
        error == CloudError::Server ? "The server is unavailable. Your last readings are shown." :
        "Could not reach the server. Your last readings are shown.";
    state(Connection::CloudUnavailable, message);
  }
  failures_ = std::min(failures_ + 1, 3u);
  nextPoll_ = now_ + std::min<std::uint32_t>(60000, PollIntervalMs << failures_);
}

void Controller::accept(CloudResult& result) {
  const bool validating = pendingCloud_.has_value();
  if (result.error != CloudError::None) { failure(result.error, validating); return; }
  Config candidate = config_;
  bool needsSave = false;
  if (validating) {
    needsSave = candidate.origin != pendingCloud_->origin || candidate.readToken != pendingCloud_->token;
    candidate.origin = pendingCloud_->origin;
    candidate.readToken = pendingCloud_->token;
    if (needsSave) {
      candidate.panels = {};
      candidate.panelsInitialized = false;
    }
  }
  if (!candidate.panelsInitialized && !result.catalog.items.empty()) {
    candidate.panels = defaultPanels(result.catalog);
    candidate.panelsInitialized = true;
    needsSave = true;
  }
  if (needsSave && !settings_.save(candidate)) {
    pendingCloud_.reset();
    storageBlocked_ = true;
    state(Connection::StorageError, "Connection verified, but settings could not be saved. Try again.");
    nextPoll_ = now_ + PollIntervalMs;
    return;
  }
  config_ = std::move(candidate);
  pendingCloud_.reset();
  workspace_.catalog = std::move(result.catalog);
  workspace_.receivedAtMs = now_;
  workspace_.hasSnapshot = true;
  updateConfigView();
  failures_ = 0;
  tokenRejected_ = false;
  nextPoll_ = now_ + PollIntervalMs;
  state(Connection::Online, workspace_.catalog.items.empty() ? "Connected. Waiting for your first report." : "Connected");
}

void Controller::tick(std::uint32_t now) {
  now_ = now;
  const auto wifi = wifi_.status();
  if (wifi.link != WifiLink::Connected) {
    if (hadConnection_) {
      ++generation_; inFlight_ = false; workspace_.refreshing = false; hadConnection_ = false;
    }
    // Drain outdated work without applying data from a previous connection.
    cloud_.take();
    if (wifi.link == WifiLink::Connecting) state(Connection::ConnectingWifi, "Connecting to Wi-Fi...");
    else if (pendingWifi_ || !config_.ssid.empty()) state(Connection::WifiUnavailable, "Wi-Fi is unavailable. Check the network name and password.");
    return;
  }
  if (pendingWifi_) {
    if (wifi.ssid != pendingWifi_->ssid) return;
    Config candidate = config_;
    candidate.ssid = pendingWifi_->ssid; candidate.wifiPassword = pendingWifi_->password;
    pendingWifi_.reset();
    if (!settings_.save(candidate)) {
      storageBlocked_ = true;
      state(Connection::StorageError, "Wi-Fi connected, but settings could not be saved. Try again.");
      return;
    }
    config_ = std::move(candidate);
    updateConfigView();
  }
  if (wifi.ssid != config_.ssid) return;
  if (!hadConnection_) { hadConnection_ = true; nextPoll_ = now; }
  if (!wifi.clockReady) {
    state(Connection::SettingClock, "Setting the clock for a secure connection...");
    return;
  }
  if (auto result = cloud_.take()) {
    if (result->generation == generation_) {
      inFlight_ = false; workspace_.refreshing = false;
      accept(*result);
    }
  }
  if (!pendingCloud_ && config_.readToken.empty()) {
    if (workspace_.connection != Connection::TokenRejected && workspace_.connection != Connection::StorageError) state(Connection::NeedCloud, "Enter your server address and a read token.");
    return;
  }
  if (inFlight_ || tokenRejected_ || storageBlocked_ || !due(now, nextPoll_)) return;
  const auto& origin = pendingCloud_ ? pendingCloud_->origin : config_.origin;
  const auto& token = pendingCloud_ ? pendingCloud_->token : config_.readToken;
  if (cloud_.start({generation_, origin, token})) {
    inFlight_ = true; workspace_.refreshing = true;
    ++workspace_.revision;
    if (!workspace_.hasSnapshot || pendingCloud_) state(Connection::CheckingCloud, "Loading your workspace...");
  }
}

}  // namespace vibemon
