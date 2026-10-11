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
  void scan() override;
  WifiStatus status() const override;
  bool scanning() const override { return scanning_; }
  const std::vector<WifiNetwork>& networks() const override { return networks_; }
 private:
  std::string ssid_;
  std::string password_;
  std::vector<WifiNetwork> networks_;
  WifiLink link_ = WifiLink::Offline;
  std::uint32_t startedAt_ = 0;
  std::uint32_t retryAt_ = 0;
  bool disconnecting_ = false;
  bool joining_ = false;
  bool scanning_ = false;
  bool scanRequested_ = false;
  bool clockRequested_ = false;
};

class EspCloud final : public CloudPort {
 public:
  bool begin();
  bool start(const CloudRequest& request) override;
  std::unique_ptr<CloudResult> take() override;
 private:
  struct Job { std::uint32_t generation; char origin[272]; char token[64]; };
  QueueHandle_t jobs_ = nullptr;
  QueueHandle_t results_ = nullptr;
  TaskHandle_t worker_ = nullptr;
  std::atomic<bool> busy_{false};
  static void work(void* context);
  static std::unique_ptr<CloudResult> fetch(const Job& job);
};

}  // namespace vibemon
