#pragma once

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
  std::string address;
};
class WifiPort {
 public:
  virtual ~WifiPort() = default;
  virtual void connect(const std::string& ssid, const std::string& password) = 0;
  virtual void scan() = 0;
  virtual WifiStatus status() const = 0;
  virtual bool scanning() const = 0;
  virtual const std::vector<WifiNetwork>& networks() const = 0;
};

struct CloudRequest {
  std::uint32_t generation = 0;
  std::string origin;
  std::string token;
};
enum class CloudError { None, Unauthorized, Forbidden, Network, Tls, InvalidResponse, TooLarge, Server };
struct CloudResult {
  std::uint32_t generation = 0;
  CloudError error = CloudError::None;
  Catalog catalog;
};
class CloudPort {
 public:
  virtual ~CloudPort() = default;
  virtual bool start(const CloudRequest& request) = 0;
  virtual std::unique_ptr<CloudResult> take() = 0;
};

}  // namespace vibemon
