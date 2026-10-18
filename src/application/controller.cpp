#include "controller.h"
#include "../domain/clock.h"

#include <algorithm>
#include <utility>

namespace vibemon {
Controller::Controller(SettingsStore& settings, WifiPort& wifi, CloudPort& cloud, UpdatePort& update, std::string version)
    : settings_(settings), wifi_(wifi), cloud_(cloud), updater_(update, std::move(version)) {}

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
  hadConnection_ = false;
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
  const auto& wifi = wifi_.status();
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
  if (pendingCloud_) return {false, "Finish or cancel workspace setup before changing panels."};
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
  ++generation_;
  inFlight_ = false;
  workspace_.refreshing = false;
  pendingWifi_.reset();
  pendingCloud_.reset();
  hadConnection_ = false;
  if (config_.ssid.empty()) {
    wifi_.disconnect();
    state(Connection::NeedWifi, "Connect to a 2.4 GHz Wi-Fi network.");
    scanWifi();
    return;
  }
  wifi_.connect(config_.ssid, config_.wifiPassword);
  state(Connection::ConnectingWifi, "Connecting to Wi-Fi...");
}

void Controller::cancelSetup() {
  if (pendingWifi_) { retryWifi(); return; }
  if (!pendingCloud_) {
    // Leaving the form dismisses a failed first setup's cause.
    if (config_.readToken.empty() && (workspace_.connection == Connection::CloudUnavailable || workspace_.connection == Connection::TokenRejected)) needCloud();
    return;
  }
  pendingCloud_.reset();
  ++generation_;
  inFlight_ = false;
  workspace_.refreshing = false;
  tokenRejected_ = false;
  nextPoll_ = now_;
  if (config_.readToken.empty()) needCloud();
  else state(Connection::CheckingCloud, "Restoring your saved workspace...");
}

void Controller::needCloud() { state(Connection::NeedCloud, "Enter your server address and a read token."); }

void Controller::refresh() {
  nextPoll_ = now_;
  tokenRejected_ = false;
  failures_ = 0;
  storageBlocked_ = false;
}

void Controller::failure(const CloudResult& result, bool validating) {
  const auto error = result.error;
  if (validating) pendingCloud_.reset();
  if (error == CloudError::Unauthorized || error == CloudError::Forbidden) {
    // A rejected replacement must not revoke the previous, still-valid connection.
    tokenRejected_ = !validating || config_.readToken.empty();
    state(Connection::TokenRejected, error == CloudError::Forbidden ? "This token needs read permission." : "This token was rejected. Check or replace it.");
  } else {
    std::string message = error == CloudError::Tls ? "The server certificate could not be verified." :
        error == CloudError::InvalidResponse ? "The server returned an invalid workspace." :
        error == CloudError::TooLarge ? "The workspace exceeds this device's limits." :
        error == CloudError::Memory ? "Not enough memory to load the workspace. Restart this device." :
        error == CloudError::Server ? "The server is unavailable. Your last readings are shown." :
        error == CloudError::RateLimited ? "The server is limiting requests. Your last readings are shown." :
        error == CloudError::Redirected ? "The server redirected the request. Check the server address." :
        "Could not reach the server. Your last readings are shown.";
    if (!result.detail.empty() && (error == CloudError::InvalidResponse || error == CloudError::TooLarge)) message += " " + result.detail;
    state(Connection::CloudUnavailable, message);
  }
  failures_ = std::min(failures_ + 1, 3u);
  // A server's Retry-After can only lengthen the backoff.
  nextPoll_ = now_ + std::max(result.retryAfterMs, std::min<std::uint32_t>(60000, PollIntervalMs << failures_));
}

void Controller::accept(CloudResult& result) {
  const bool validating = pendingCloud_.has_value();
  if (result.error != CloudError::None) { failure(result, validating); return; }
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
  workspace_.catalogComplete = requestedAll_;
  workspace_.snapshotAgeMs = 0;
  workspace_.hasSnapshot = true;
  updateConfigView();
  failures_ = 0;
  tokenRejected_ = false;
  // The picker needs every item; replace a panel-only snapshot at once.
  nextPoll_ = browsing_ && !requestedAll_ ? now_ : now_ + PollIntervalMs;
  state(Connection::Online, requestedAll_ && workspace_.catalog.items.empty() ? "Connected. Waiting for your first report." : "Connected");
}

std::vector<Selection> Controller::requestScope() const {
  // Validation, first panel defaults and the picker need every item.
  if (pendingCloud_ || !config_.panelsInitialized || browsing_) return {};
  std::vector<Selection> scope;
  for (const auto& selection : config_.panels) {
    if (!selection.id.empty() && std::find(scope.begin(), scope.end(), selection) == scope.end()) scope.push_back(selection);
  }
  return scope;
}

void Controller::browse(bool browsing) {
  if (browsing_ == browsing) return;
  browsing_ = browsing;
  if (browsing && !workspace_.catalogComplete) nextPoll_ = now_;
}

void Controller::tick(std::uint32_t now) {
  // Accumulate each loop's unsigned delta so an offline snapshot cannot become
  // fresh again when the platform's 32-bit millisecond clock wraps.
  if (workspace_.hasSnapshot) workspace_.snapshotAgeMs += static_cast<std::uint32_t>(now - now_);
  now_ = now;
  const auto& wifi = wifi_.status();
  if (updater_.tick(now, wifi.link == WifiLink::Connected && wifi.clockReady, workspace_.connection == Connection::Online)) {
    workspace_.firmware = updater_.view();
    ++workspace_.revision;
  }
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
    const bool changed = candidate.ssid != config_.ssid || candidate.wifiPassword != config_.wifiPassword;
    if (changed && !settings_.save(candidate)) {
      storageBlocked_ = true;
      state(Connection::StorageError, "Wi-Fi connected, but settings could not be saved. Try again.");
      return;
    }
    config_ = std::move(candidate);
    updateConfigView();
  }
  if (wifi.ssid != config_.ssid) return;
  if (!hadConnection_) { hadConnection_ = true; nextPoll_ = clockStartedAt_ = now; }
  if (!wifi.clockReady) {
    if (static_cast<std::uint32_t>(now - clockStartedAt_) >= 30000) {
      state(Connection::ClockUnavailable, "Network time is unavailable. Check this network's internet access.");
    } else state(Connection::SettingClock, "Setting the clock for a secure connection...");
    return;
  }
  if (auto result = cloud_.take()) {
    if (result->generation == generation_) {
      inFlight_ = false; workspace_.refreshing = false;
      accept(*result);
    }
  }
  if (!pendingCloud_ && config_.readToken.empty()) {
    // Keep a failed first setup's cause on screen until the user tries again.
    const auto current = workspace_.connection;
    if (current != Connection::CloudUnavailable && current != Connection::TokenRejected && current != Connection::StorageError) needCloud();
    return;
  }
  // A firmware download owns the network until the device restarts.
  if (inFlight_ || tokenRejected_ || storageBlocked_ || updater_.busy() || !due(now, nextPoll_)) return;
  const auto& origin = pendingCloud_ ? pendingCloud_->origin : config_.origin;
  const auto& token = pendingCloud_ ? pendingCloud_->token : config_.readToken;
  auto scope = requestScope();
  const bool all = scope.empty();
  if (cloud_.start({generation_, origin, token, std::move(scope)})) {
    inFlight_ = true; workspace_.refreshing = true; requestedAll_ = all;
    ++workspace_.revision;
    if (!workspace_.hasSnapshot || pendingCloud_) state(Connection::CheckingCloud, "Loading your workspace...");
  }
}

}  // namespace vibemon
