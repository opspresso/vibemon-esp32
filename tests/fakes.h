#pragma once
#include "application/controller.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace vibemon;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

struct Store final : SettingsStore {
  Config saved;
  LoadResult result = LoadResult::Missing;
  int writes = 0;
  bool writable = true;
  LoadResult load(Config& config) override { config = saved; return result; }
  bool save(const Config& config) override { ++writes; if (!writable) return false; saved = config; result = LoadResult::Loaded; return true; }
};
struct Wifi final : WifiPort {
  WifiStatus current;
  std::vector<WifiNetwork> found;
  int scans = 0, connects = 0;
  bool scanError = false;
  std::string requested;
  void connect(const std::string& ssid, const std::string&) override { requested = ssid; current.link = WifiLink::Connecting; ++connects; }
  void disconnect() override { requested.clear(); current = {}; }
  void scan() override { ++scans; }
  WifiStatus status() const override { return current; }
  bool scanning() const override { return false; }
  bool scanFailed() const override { return scanError; }
  const std::vector<WifiNetwork>& networks() const override { return found; }
  void connected(const std::string& ssid = "Workshop", bool clock = true) { current = {WifiLink::Connected, clock, ssid, "192.0.2.1"}; }
};
struct Cloud final : CloudPort {
  std::vector<CloudRequest> requests;
  std::unique_ptr<CloudResult> reply;
  bool busy = false;
  bool start(const CloudRequest& request) override { if (busy) return false; busy = true; requests.push_back(request); return true; }
  std::unique_ptr<CloudResult> take() override { return std::move(reply); }
  void complete(Catalog catalog = {}, CloudError error = CloudError::None) {
    CHECK(busy && !requests.empty());
    reply = std::make_unique<CloudResult>(CloudResult{requests.back().generation, error, std::move(catalog)});
    busy = false;
  }
};
struct Update final : UpdatePort {
  int checks = 0, confirms = 0, restarts = 0;
  std::vector<FirmwareRelease> installs;
  std::unique_ptr<UpdateResult> reply;
  std::uint8_t percent = 0;
  bool busy = false;
  bool check() override { if (busy) return false; busy = true; ++checks; return true; }
  bool install(const FirmwareRelease& release) override { if (busy) return false; busy = true; installs.push_back(release); return true; }
  std::unique_ptr<UpdateResult> take() override { return std::move(reply); }
  std::uint8_t progress() const override { return percent; }
  void confirm() override { ++confirms; }
  void restart() override { ++restarts; }
  void complete(FirmwareRelease release = {}, UpdateError error = UpdateError::None) {
    CHECK(busy);
    reply = std::make_unique<UpdateResult>(UpdateResult{error, std::move(release)});
    busy = false;
  }
};
inline FirmwareRelease firmwareRelease(const std::string& version) {
  return {version, std::string(FirmwareDownloadPrefix) + "v" + version + "/vibemon-esp32.ino.bin", 3800000, std::string(64, 'a')};
}
inline Catalog catalog(const std::string& id = "one") {
  Catalog value;
  Monitor resource;
  resource.selection = {ItemKind::Resource, id}; resource.name = "Resource";
  value.items.push_back(resource);
  Monitor account;
  account.selection = {ItemKind::Account, "codex:work"}; account.name = "Work";
  value.items.push_back(account);
  return value;
}
inline void configured(Store& store) {
  store.result = LoadResult::Loaded;
  store.saved.ssid = "Workshop"; store.saved.wifiPassword = "test-password";
  store.saved.readToken = "vm_" + std::string(60, 'a');
}
