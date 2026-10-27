#include "esp32_services.h"
#include "https.h"
#include "secrets.h"
#include "response_buffer.h"
#include "../adapters/catalog_json.h"
#include "../adapters/cloud_http.h"
#include "../adapters/device_auth.h"
#include "../adapters/settings_json.h"
#include "../domain/clock.h"

#include <Preferences.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <esp_heap_caps.h>
#include <map>
#include <mbedtls/x509.h>

namespace vibemon {
namespace {
constexpr std::uint32_t ConnectTimeoutMs = 20000;
constexpr std::uint32_t ReconnectDelayMs = 30000;
// Keep the TLS session between regular polls, and release it when polling
// pauses, for example during a firmware install or after a rejected token.
// vibemon.io kept idle connections for more than 60 s when measured.
constexpr std::uint32_t IdleSessionMs = PollIntervalMs + 15000;
constexpr std::size_t JsonBudgetBytes = 512 * 1024;

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

struct CloudJob {
  std::uint32_t generation;
  std::string origin;
  std::string token;
  std::string path;
  std::string ifNoneMatch;
  ~CloudJob() { wipe(token); }
};

struct SignInJob {
  std::uint32_t attempt;
  std::string origin;
  std::string deviceCode;
  ~SignInJob() { wipe(deviceCode); }
};

// The worker queue holds one of the two requests.
struct WorkerJob {
  std::unique_ptr<CloudJob> read;
  std::unique_ptr<SignInJob> signIn;
};

// The token name on the server: the last three bytes of the station MAC, such as 1A2B3C.
std::string deviceName() {
  std::uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char name[7];
  std::snprintf(name, sizeof(name), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return name;
}

// Owned by the worker task. HTTP keep-alive reuses the TLS connection while
// the origin stays the same and every response is read completely.
class CloudSession {
 public:
  CloudSession() {
    http_.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http_.setReuse(true);
    const char* collected[] = {"Retry-After", "ETag", "X-Server-Time"};
    http_.collectHeaders(collected, 3);
  }
  // Frees the TLS session's internal-RAM buffers. The next request starts a
  // new client, so its error state cannot describe an earlier connection.
  void close() {
    if (!client_) return;
    http_.end();
    client_.reset();
    origin_.clear();
  }
  std::unique_ptr<CloudResult> fetch(const CloudJob& job) {
    if (origin_ != job.origin) close();
    const bool reused = client_ && client_->connected();
    auto result = request(job);
    // The server may close an idle connection as the request starts. Retry
    // once on a new connection, which also reports certificate failures.
    if (reused && (result->error == CloudError::Network || result->error == CloudError::Tls)) {
      close();
      result = request(job);
    }
    if (result->error != CloudError::None) close();
    return result;
  }
  std::unique_ptr<SignInResult> signIn(const SignInJob& job) {
    if (origin_ != job.origin) close();
    const bool reused = client_ && client_->connected();
    auto result = post(job);
    if (reused && (result->error == SignInError::Network || result->error == SignInError::Tls)) {
      close();
      result = post(job);
    }
    // Pending and slow-down replies are complete responses on a healthy connection.
    if (result->error != SignInError::None && result->error != SignInError::Pending && result->error != SignInError::SlowDown) close();
    return result;
  }
 private:
  std::unique_ptr<NetworkClientSecure> client_;
  HTTPClient http_;
  std::string origin_;  // the origin of client_

  void connect(const std::string& origin) {
    if (client_) return;
    client_ = std::make_unique<NetworkClientSecure>();
    prepareHttps(*client_, http_, 4000);
    origin_ = origin;
  }

  // RFC 8628 requests are form posts. Only 200 and 400 replies carry a body to read.
  std::unique_ptr<SignInResult> post(const SignInJob& job) {
    const bool start = job.deviceCode.empty();
    auto failed = [&](SignInError error) { auto result = std::make_unique<SignInResult>(); result->attempt = job.attempt; result->error = error; return result; };
    connect(job.origin);
    if (WiFi.status() != WL_CONNECTED || !http_.begin(*client_, (job.origin + (start ? DeviceStartPath : DeviceTokenPath)).c_str())) return failed(SignInError::Network);
    http_.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http_.addHeader("Accept", "application/json");
    std::string form = start ? deviceStartBody(deviceName()) : deviceTokenBody(job.deviceCode);
    const int status = http_.POST(reinterpret_cast<std::uint8_t*>(form.data()), form.size());
    wipe(form);
    if (status <= 0) {
      char error[128];
      const auto tls = client_->lastError(error, sizeof(error)) == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
      http_.end();
      return failed(tls ? SignInError::Tls : SignInError::Network);
    }
    std::unique_ptr<SignInResult> result;
    if (status == 200 || status == 400) {
      ResponseBuffer body(MaxDeviceReplyBytes);
      const int read = http_.writeToStream(&body);
      http_.end();
      if (body.timedOut() || read < 0) return failed(SignInError::Network);
      if (body.oversized() || body.outOfMemory()) return failed(SignInError::InvalidResponse);
      result = std::make_unique<SignInResult>(start ? decodeDeviceStart(status, body.body(), job.origin) : decodeDeviceToken(status, body.body()));
    } else {
      result = std::make_unique<SignInResult>(start ? decodeDeviceStart(status, {}, job.origin) : decodeDeviceToken(status, {}));
      if (status == 429 || status >= 500) result->retryAfterMs = retryAfterMs(http_.header("Retry-After").c_str()).value_or(0);
      http_.end();
    }
    result->attempt = job.attempt;
    return result;
  }

  std::unique_ptr<CloudResult> request(const CloudJob& job) {
    auto result = std::make_unique<CloudResult>();
    result->generation = job.generation;
    connect(job.origin);
    if (WiFi.status() != WL_CONNECTED || !http_.begin(*client_, (job.origin + job.path).c_str())) {
      result->error = CloudError::Network;
      return result;
    }
    String authorization;
    authorization.reserve(7 + job.token.size());
    authorization += "Bearer ";
    authorization += job.token.c_str();
    http_.addHeader("Authorization", authorization);
    wipe(authorization.begin(), authorization.length());
    http_.addHeader("Accept", "application/json");
    if (!job.ifNoneMatch.empty()) http_.addHeader("If-None-Match", job.ifNoneMatch.c_str());
    const int status = http_.GET();
    if (status == 304) {
      notModifiedResult(!job.ifNoneMatch.empty(), http_.header("X-Server-Time").c_str(), *result);
      http_.end();
      return result;
    }
    if (status != 200) {
      if (status > 0) {
        result->error = cloudStatusError(status);
        // 429 and 503 may say when to retry; the controller only lengthens its backoff with it.
        if (result->error == CloudError::RateLimited || result->error == CloudError::Server) result->retryAfterMs = retryAfterMs(http_.header("Retry-After").c_str()).value_or(0);
      } else {
        char error[128];
        result->error = client_->lastError(error, sizeof(error)) == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? CloudError::Tls : CloudError::Network;
      }
      http_.end();
      return result;
    }
    result->etag = entityTag(http_.header("ETag").c_str());
    const int expected = http_.getSize();
    if (expected > static_cast<int>(MaxResponseBytes)) { result->error = CloudError::TooLarge; http_.end(); return result; }
    ResponseBuffer body(expected >= 0 ? static_cast<std::size_t>(expected) : MaxResponseBytes);
    const int read = http_.writeToStream(&body);
    http_.end();
    if (body.oversized()) result->error = CloudError::TooLarge;
    else if (body.outOfMemory()) result->error = CloudError::Memory;
    else if (body.timedOut() || read < 0 || (expected >= 0 && read != expected)) result->error = CloudError::Network;
    if (result->error != CloudError::None) return result;
    PsramJsonAllocator allocator;
    const auto decoded = decodeCatalog(body.body(), result->catalog, &allocator);
    if (!decoded.ok) {
      result->error = decoded.exceedsLimit ? CloudError::TooLarge : CloudError::InvalidResponse;
      result->detail = decoded.message;
      result->etag.clear();
    }
    result->serverTimeMs = decoded.serverTimeMs;
    return result;
  }
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
  publish();
}

void EspWifi::scan() { scanRequested_ = true; scanFailed_ = false; }

void EspWifi::disconnect() {
  ssid_.clear(); password_.clear();
  disconnecting_ = joining_ = clockRequested_ = false;
  link_ = WifiLink::Offline;
  WiFi.disconnect(false, false);
  publish();
}

void EspWifi::publish() {
  status_.link = link_;
  status_.clockReady = std::time(nullptr) >= 1735689600;
  // The join check verified that the connected network is ssid_.
  if (link_ != WifiLink::Connected) status_.ssid.clear();
  else if (status_.ssid != ssid_) status_.ssid = ssid_;
}

void EspWifi::tick(std::uint32_t now) {
  const auto physical = WiFi.status();
  if (disconnecting_ && physical != WL_CONNECTED) {
    disconnecting_ = false;
    joining_ = true;
    WiFi.begin(ssid_.c_str(), password_.c_str());
  } else if (joining_ && !disconnecting_ && physical == WL_CONNECTED && WiFi.SSID() == ssid_.c_str()) {
    joining_ = false;
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
  publish();
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
  jobs_ = xQueueCreate(1, sizeof(WorkerJob*));
  results_ = xQueueCreate(1, sizeof(CloudResult*));
  signInResults_ = xQueueCreate(1, sizeof(SignInResult*));
  if (!jobs_ || !results_ || !signInResults_) return false;
  return xTaskCreatePinnedToCore(work, "vibemon-https", 12288, this, 1, &worker_, 0) == pdPASS;
}

bool EspCloud::start(const CloudRequest& request) {
  if (!jobs_ || request.origin.size() > MaxOriginBytes) return false;
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true)) return false;
  // The queue holds only a pointer, so the token exists in one wiped copy.
  auto* job = new WorkerJob{std::unique_ptr<CloudJob>(new CloudJob{request.generation, request.origin, request.token, sourcesPath(request.scope), request.ifNoneMatch}), nullptr};
  if (xQueueSend(jobs_, &job, 0) == pdTRUE) return true;
  delete job;
  busy_ = false;
  return false;
}

bool EspCloud::startSignIn(const SignInRequest& request) {
  if (!jobs_ || request.origin.size() > MaxOriginBytes) return false;
  bool expected = false;
  if (!busy_.compare_exchange_strong(expected, true)) return false;
  auto* job = new WorkerJob{nullptr, std::unique_ptr<SignInJob>(new SignInJob{request.attempt, request.origin, request.deviceCode})};
  if (xQueueSend(jobs_, &job, 0) == pdTRUE) return true;
  delete job;
  busy_ = false;
  return false;
}

std::unique_ptr<SignInResult> EspCloud::takeSignIn() {
  SignInResult* result = nullptr;
  if (!signInResults_ || xQueueReceive(signInResults_, &result, 0) != pdTRUE) return {};
  busy_ = false;
  return std::unique_ptr<SignInResult>(result);
}

std::unique_ptr<CloudResult> EspCloud::take() {
  CloudResult* result = nullptr;
  if (!results_ || xQueueReceive(results_, &result, 0) != pdTRUE) return {};
  busy_ = false;
  return std::unique_ptr<CloudResult>(result);
}

void EspCloud::work(void* context) {
  auto& self = *static_cast<EspCloud*>(context);
  CloudSession session;
  WorkerJob* received = nullptr;
  for (;;) {
    if (xQueueReceive(self.jobs_, &received, pdMS_TO_TICKS(IdleSessionMs)) != pdTRUE) { session.close(); continue; }
    std::unique_ptr<WorkerJob> job(received);
    if (job->signIn) {
      auto* result = session.signIn(*job->signIn).release();
      if (xQueueSend(self.signInResults_, &result, portMAX_DELAY) != pdTRUE) delete result;
      continue;
    }
    auto* result = session.fetch(*job->read).release();
    if (xQueueSend(self.results_, &result, portMAX_DELAY) != pdTRUE) delete result;
  }
}
}  // namespace vibemon
