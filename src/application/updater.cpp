#include "updater.h"
#include "../domain/clock.h"

namespace vibemon {
void Updater::confirm() {
  if (confirmed_) return;
  port_.confirm();
  confirmed_ = true;
}

void Updater::checkNow() {
  if (inFlight_ || busy()) return;
  scheduled_ = true;
  nextCheck_ = now_;
}

void Updater::finish(UpdateResult& result) {
  inFlight_ = false;
  if (view_.state == FirmwareState::Installing) {
    if (result.error == UpdateError::None) {
      view_.state = FirmwareState::Restarting;
      view_.progress = 100;
      restartAt_ = now_ + RestartDelayMs;
    } else {
      view_.state = FirmwareState::Failed;
      nextCheck_ = now_ + CheckIntervalMs;
    }
    return;
  }
  if (result.error != UpdateError::None) {
    view_.state = FirmwareState::Unavailable;
    nextCheck_ = now_ + RetryDelayMs;
    return;
  }
  // The release server answered over TLS, so this image can receive a fix.
  confirm();
  nextCheck_ = now_ + CheckIntervalMs;
  if (!newerVersion(version_, result.release.version)) {
    view_.state = FirmwareState::Current;
    view_.available.clear();
    return;
  }
  view_.available = result.release.version;
  view_.progress = 0;
  if (port_.install(result.release)) {
    view_.state = FirmwareState::Installing;
    inFlight_ = true;
  } else {
    view_.state = FirmwareState::Failed;
  }
}

bool Updater::tick(std::uint32_t now, bool networkReady, bool healthy) {
  now_ = now;
  if (healthy) confirm();
  if (view_.state == FirmwareState::Restarting) {
    if (due(now, restartAt_)) port_.restart();
    return false;
  }
  bool changed = false;
  if (auto result = port_.take()) {
    finish(*result);
    changed = true;
  }
  if (view_.state == FirmwareState::Installing) {
    const auto progress = port_.progress();
    if (progress != view_.progress) { view_.progress = progress; changed = true; }
  }
  if (inFlight_ || !networkReady) return changed;
  if (!scheduled_) {
    scheduled_ = true;
    nextCheck_ = now + FirstCheckDelayMs;
  }
  if (due(now, nextCheck_) && port_.check()) {
    inFlight_ = true;
    if (view_.state != FirmwareState::Checking) { view_.state = FirmwareState::Checking; changed = true; }
  }
  return changed;
}

}  // namespace vibemon
