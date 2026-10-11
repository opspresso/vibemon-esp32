#include "esp32_services.h"
#include "../adapters/catalog_json.h"
#include "../adapters/settings_json.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <algorithm>
#include <cstring>
#include <cstddef>
#include <ctime>
#include <esp_heap_caps.h>
#include <map>
#include <mbedtls/x509.h>

namespace vibemon {
namespace {
constexpr std::uint32_t ConnectTimeoutMs = 20000;
constexpr std::uint32_t ReconnectDelayMs = 30000;
constexpr std::uint32_t RequestBudgetMs = 15000;
constexpr std::size_t JsonBudgetBytes = 512 * 1024;
bool due(std::uint32_t now, std::uint32_t deadline) { return static_cast<std::int32_t>(now - deadline) >= 0; }

class PsramJsonAllocator final : public ArduinoJson::Allocator {
  struct alignas(std::max_align_t) Header { std::size_t size; };
  std::size_t allocated_ = 0;
 public:
  void* allocate(std::size_t size) override {
    if (size > JsonBudgetBytes - allocated_) return nullptr;
    auto* block = static_cast<Header*>(heap_caps_malloc(sizeof(Header) + size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!block) return nullptr;
    block->size = size; allocated_ += size; return block + 1;
  }
  void deallocate(void* pointer) override {
    if (!pointer) return;
    auto* block = static_cast<Header*>(pointer) - 1;
    allocated_ -= block->size; heap_caps_free(block);
  }
  void* reallocate(void* pointer, std::size_t size) override {
    if (!pointer) return allocate(size);
    auto* previous = static_cast<Header*>(pointer) - 1;
    const auto old = previous->size;
    if (size > JsonBudgetBytes - (allocated_ - old)) return nullptr;
    auto* block = static_cast<Header*>(heap_caps_realloc(previous, sizeof(Header) + size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!block) return nullptr;
    block->size = size; allocated_ = allocated_ - old + size; return block + 1;
  }
};

class ResponseBuffer final : public Stream {
 public:
  explicit ResponseBuffer(std::size_t capacity) : capacity_(capacity), startedAt_(millis()) {
    data_ = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  ~ResponseBuffer() override { heap_caps_free(data_); }
  bool ready() const { return data_ != nullptr; }
  bool oversized() const { return oversized_; }
  std::string_view body() const { return {data_, size_}; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  std::size_t write(std::uint8_t byte) override { return write(&byte, 1); }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    if (size > capacity_ - size_) { oversized_ = true; return 0; }
    if (!data_ || static_cast<std::uint32_t>(millis() - startedAt_) > RequestBudgetMs) return 0;
    std::memcpy(data_ + size_, data, size); size_ += size; return size;
  }
 private:
  char* data_ = nullptr;
  std::size_t capacity_;
  std::size_t size_ = 0;
  std::uint32_t startedAt_;
  bool oversized_ = false;
};
}  // namespace

LoadResult NvsSettings::load(Config& config) {
  Preferences preferences;
  if (!preferences.begin("vibemon-touch", false)) return LoadResult::Unavailable;
  if (!preferences.isKey("config")) { preferences.end(); return LoadResult::Missing; }
  const String value = preferences.getString("config");
  preferences.end();
  return decodeSettings({value.c_str(), value.length()}, config) ? LoadResult::Loaded : LoadResult::Invalid;
}

bool NvsSettings::save(const Config& config) {
  const auto json = encodeSettings(config);
  if (json.empty()) return false;
  Preferences preferences;
  if (!preferences.begin("vibemon-touch", false)) return false;
  // One NVS value commits the connection and all four selections together.
  const auto written = preferences.putString("config", json.c_str());
  preferences.end();
  return written == json.size();
}

void EspWifi::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
}

void EspWifi::connect(const std::string& ssid, const std::string& password) {
  ssid_ = ssid; password_ = password;
  link_ = WifiLink::Connecting;
  disconnecting_ = true;
  joining_ = false;
  startedAt_ = millis();
  clockRequested_ = false;
  if (scanning_) { WiFi.scanDelete(); scanning_ = false; }
  WiFi.disconnect(false, false);
}

void EspWifi::scan() { scanRequested_ = true; scanFailed_ = false; }

void EspWifi::disconnect() {
  ssid_.clear(); password_.clear();
  disconnecting_ = joining_ = clockRequested_ = false;
  link_ = WifiLink::Offline;
  WiFi.disconnect(false, false);
}

WifiStatus EspWifi::status() const {
  WifiStatus result;
  result.link = link_;
  result.clockReady = std::time(nullptr) >= 1735689600;
  if (link_ == WifiLink::Connected) {
    result.ssid = WiFi.SSID().c_str(); result.address = WiFi.localIP().toString().c_str();
  }
  return result;
}

void EspWifi::tick(std::uint32_t now) {
  const auto physical = WiFi.status();
  if (disconnecting_ && physical != WL_CONNECTED) {
    disconnecting_ = false;
    joining_ = true;
    WiFi.begin(ssid_.c_str(), password_.c_str());
  } else if (joining_ && !disconnecting_ && physical == WL_CONNECTED && WiFi.SSID() == ssid_.c_str()) {
    link_ = WifiLink::Connected;
    if (!clockRequested_) {
      configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
      clockRequested_ = true;
    }
  } else if (link_ == WifiLink::Connecting && static_cast<std::uint32_t>(now - startedAt_) >= ConnectTimeoutMs) {
    WiFi.disconnect(false, false); disconnecting_ = false; joining_ = false; link_ = WifiLink::Failed;
    retryAt_ = now + ReconnectDelayMs;
  } else if (link_ == WifiLink::Connected && physical != WL_CONNECTED) {
    link_ = WifiLink::Offline; retryAt_ = now + 5000; clockRequested_ = false;
  } else if (!ssid_.empty() && link_ != WifiLink::Connecting && link_ != WifiLink::Connected && !scanning_ && due(now, retryAt_)) {
    connect(ssid_, password_);
  }
  if (scanRequested_ && link_ != WifiLink::Connecting && !scanning_) {
    scanRequested_ = false;
    WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true, true);
    scanning_ = result == WIFI_SCAN_RUNNING;
    scanFailed_ = result == WIFI_SCAN_FAILED;
  }
  if (scanning_) {
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    scanning_ = false;
    if (count < 0) { scanFailed_ = true; WiFi.scanDelete(); return; }
    std::map<std::string, WifiNetwork> unique;
    for (int i = 0; i < count; ++i) {
      const std::string name = WiFi.SSID(i).c_str();
      if (!validText(name, 32)) continue;
      WifiNetwork network{name, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
      const auto found = unique.find(name);
      if (found == unique.end() || network.signal > found->second.signal) unique[name] = std::move(network);
    }
    networks_.clear();
    for (auto& entry : unique) networks_.push_back(std::move(entry.second));
    std::sort(networks_.begin(), networks_.end(), [](const auto& a, const auto& b) { return a.signal > b.signal; });
    if (networks_.size() > 24) networks_.resize(24);
    WiFi.scanDelete();
  }
}

bool EspCloud::begin() {
  jobs_ = xQueueCreate(1, sizeof(Job));
  results_ = xQueueCreate(1, sizeof(CloudResult*));
  if (!jobs_ || !results_) return false;
  return xTaskCreatePinnedToCore(work, "vibemon-https", 12288, this, 1, &worker_, 0) == pdPASS;
}

bool EspCloud::start(const CloudRequest& request) {
  if (!jobs_ || request.origin.size() >= sizeof(Job::origin) || request.token.size() >= sizeof(Job::token)) return false;
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true)) return false;
  Job job{}; job.generation = request.generation;
  std::memcpy(job.origin, request.origin.data(), request.origin.size());
  std::memcpy(job.token, request.token.data(), request.token.size());
  if (xQueueSend(jobs_, &job, 0) == pdTRUE) return true;
  busy_ = false;
  return false;
}

std::unique_ptr<CloudResult> EspCloud::take() {
  CloudResult* result = nullptr;
  if (!results_ || xQueueReceive(results_, &result, 0) != pdTRUE) return {};
  busy_ = false;
  return std::unique_ptr<CloudResult>(result);
}

void EspCloud::work(void* context) {
  auto& self = *static_cast<EspCloud*>(context);
  Job job{};
  for (;;) {
    if (xQueueReceive(self.jobs_, &job, portMAX_DELAY) != pdTRUE) continue;
    auto result = fetch(job);
    std::memset(job.token, 0, sizeof(job.token));
    auto* owned = result.release();
    if (xQueueSend(self.results_, &owned, portMAX_DELAY) != pdTRUE) delete owned;
  }
}

std::unique_ptr<CloudResult> EspCloud::fetch(const Job& job) {
  auto result = std::make_unique<CloudResult>();
  result->generation = job.generation;
  if (WiFi.status() != WL_CONNECTED) { result->error = CloudError::Network; return result; }
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(4000); http.setTimeout(4000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, (std::string(job.origin) + "/api/v1/sources").c_str())) { result->error = CloudError::Network; return result; }
  http.addHeader("Authorization", (std::string("Bearer ") + job.token).c_str());
  http.addHeader("Accept", "application/json");
  const int status = http.GET();
  if (status != 200) {
    char error[128];
    const int tlsError = client.lastError(error, sizeof(error));
    result->error = status == 401 ? CloudError::Unauthorized : status == 403 ? CloudError::Forbidden :
        status >= 500 || status == 429 ? CloudError::Server : status > 0 ? CloudError::InvalidResponse :
        tlsError == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? CloudError::Tls : CloudError::Network;
    http.end(); return result;
  }
  const int expected = http.getSize();
  if (expected > static_cast<int>(MaxResponseBytes)) { result->error = CloudError::TooLarge; http.end(); return result; }
  ResponseBuffer body(expected > 0 ? static_cast<std::size_t>(expected) : MaxResponseBytes);
  if (!body.ready()) { result->error = CloudError::Memory; http.end(); return result; }
  const int read = http.writeToStream(&body);
  http.end();
  if (body.oversized()) { result->error = CloudError::TooLarge; return result; }
  if (read < 0 || (expected >= 0 && read != expected)) { result->error = CloudError::Network; return result; }
  PsramJsonAllocator allocator;
  if (!decodeCatalog(body.body(), result->catalog, &allocator).ok) result->error = CloudError::InvalidResponse;
  return result;
}
}  // namespace vibemon
