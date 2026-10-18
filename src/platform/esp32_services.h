#pragma once

#include "../application/ports.h"
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace vibemon {

class NvsSettings final : public SettingsStore {
 public:
  LoadResult load(Config& config) override;
  bool save(const Config& config) override;
};

class EspWifi final : public WifiPort {
 public:
  void begin();
  void tick(std::uint32_t now);
  void connect(const std::string& ssid, const std::string& password) override;
  void disconnect() override;
  void scan() override;
  const WifiStatus& status() const override { return status_; }
  bool scanning() const override { return scanning_ || scanRequested_; }
  bool scanFailed() const override { return scanFailed_; }
  const std::vector<WifiNetwork>& networks() const override { return networks_; }
 private:
  std::string ssid_;
  std::string password_;
  std::vector<WifiNetwork> networks_;
  // Updated by tick(), connect() and disconnect(), so status() does not allocate.
  WifiStatus status_;
  WifiLink link_ = WifiLink::Offline;
  std::uint32_t startedAt_ = 0;
  std::uint32_t retryAt_ = 0;
  bool disconnecting_ = false;
  bool joining_ = false;
  bool scanning_ = false;
  bool scanRequested_ = false;
  bool scanFailed_ = false;
  bool clockRequested_ = false;
  void publish();
};

// One worker on core 0 runs requests in order and keeps the TLS connection
// between polls, so a poll normally skips the full handshake.
class EspCloud final : public CloudPort {
 public:
  bool begin();
  bool start(const CloudRequest& request) override;
  std::unique_ptr<CloudResult> take() override;
 private:
  QueueHandle_t jobs_ = nullptr;
  QueueHandle_t results_ = nullptr;
  TaskHandle_t worker_ = nullptr;
  std::atomic<bool> busy_{false};
  static void work(void* context);
};

}  // namespace vibemon
