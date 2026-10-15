#pragma once

#include "ports.h"

namespace vibemon {

enum class FirmwareState { Waiting, Checking, Current, Unavailable, Installing, Restarting, Failed };
struct FirmwareView {
  FirmwareState state = FirmwareState::Waiting;
  std::string available;  // the newer version that is installing or failed
  std::uint8_t progress = 0;
};

// Checks for a newer release, installs it and restarts into it.
// The running image is confirmed when it reaches the workspace or the release
// server, so a broken update rolls back instead of remaining installed.
class Updater {
 public:
  static constexpr std::uint32_t FirstCheckDelayMs = 30000;
  static constexpr std::uint32_t CheckIntervalMs = 6u * 60 * 60 * 1000;
  static constexpr std::uint32_t RetryDelayMs = 15u * 60 * 1000;
  static constexpr std::uint32_t RestartDelayMs = 3000;

  Updater(UpdatePort& port, std::string version) : port_(port), version_(std::move(version)) {}
  // Returns true when view() changed.
  bool tick(std::uint32_t now, bool networkReady, bool healthy);
  void checkNow();
  // An install or the restart after it owns the network and the next boot.
  bool busy() const { return view_.state == FirmwareState::Installing || view_.state == FirmwareState::Restarting; }
  const FirmwareView& view() const { return view_; }

 private:
  UpdatePort& port_;
  std::string version_;
  FirmwareView view_;
  std::uint32_t now_ = 0;
  std::uint32_t nextCheck_ = 0;
  std::uint32_t restartAt_ = 0;
  bool scheduled_ = false;
  bool inFlight_ = false;
  bool confirmed_ = false;
  void confirm();
  void finish(UpdateResult& result);
};

}  // namespace vibemon
