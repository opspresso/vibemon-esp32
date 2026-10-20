#include "esp32_update.h"
#include "https.h"
#include "response_buffer.h"
#include "../adapters/release_json.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

namespace vibemon {
namespace {
constexpr std::uint32_t DownloadBudgetMs = 10u * 60 * 1000;
constexpr std::uint32_t StallLimitMs = 20000;

// GitHub release assets redirect to a separate download host.
void prepare(HTTPClient& http, NetworkClientSecure& client) {
  prepareHttps(client, http, 8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);
}

std::string hex(const std::array<unsigned char, 32>& digest) {
  std::string result(64, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) std::snprintf(&result[i * 2], 3, "%02x", digest[i]);
  return result;
}

struct Sha256 {
  mbedtls_sha256_context context;
  Sha256() { mbedtls_sha256_init(&context); mbedtls_sha256_starts(&context, 0); }
  ~Sha256() { mbedtls_sha256_free(&context); }
  void update(const std::uint8_t* data, std::size_t size) { mbedtls_sha256_update(&context, data, size); }
  std::string finish() { std::array<unsigned char, 32> digest{}; mbedtls_sha256_finish(&context, digest.data()); return hex(digest); }
};
}  // namespace

bool EspUpdate::begin() {
  jobs_ = xQueueCreate(1, sizeof(Job*));
  results_ = xQueueCreate(1, sizeof(UpdateResult*));
  if (!jobs_ || !results_) return false;
  return xTaskCreatePinnedToCore(work, "vibemon-update", 12288, this, 1, &worker_, 0) == pdPASS;
}

bool EspUpdate::submit(Job* job) {
  bool expected = false;
  if (!jobs_ || !busy_.compare_exchange_strong(expected, true)) { delete job; return false; }
  progress_ = 0;
  if (xQueueSend(jobs_, &job, 0) == pdTRUE) return true;
  delete job;
  busy_ = false;
  return false;
}

bool EspUpdate::check() { return submit(new Job{false, {}}); }

bool EspUpdate::install(const FirmwareRelease& release) {
  if (!validRelease(release)) return false;
  return submit(new Job{true, release});
}

std::unique_ptr<UpdateResult> EspUpdate::take() {
  UpdateResult* result = nullptr;
  if (!results_ || xQueueReceive(results_, &result, 0) != pdTRUE) return {};
  busy_ = false;
  return std::unique_ptr<UpdateResult>(result);
}

bool EspUpdate::confirm() {
  esp_ota_img_states_t state;
  // Only a trial image can roll back. A USB upload leaves boot metadata blank,
  // so the SDK reports no state for an image that is already accepted.
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return true;
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

void EspUpdate::restart() { esp_restart(); }

const char* EspUpdate::runningSlot() const {
  const auto* running = esp_ota_get_running_partition();
  return running ? running->label : "unknown";
}

void EspUpdate::work(void* context) {
  auto& self = *static_cast<EspUpdate*>(context);
  Job* job = nullptr;
  for (;;) {
    if (xQueueReceive(self.jobs_, &job, portMAX_DELAY) != pdTRUE) continue;
    std::unique_ptr<Job> owned(job);
    auto result = owned->install ? self.write(owned->release) : fetchRelease();
    auto* published = result.release();
    if (xQueueSend(self.results_, &published, portMAX_DELAY) != pdTRUE) delete published;
  }
}

std::unique_ptr<UpdateResult> EspUpdate::fetchRelease() {
  auto result = std::make_unique<UpdateResult>();
  if (WiFi.status() != WL_CONNECTED) { result->error = UpdateError::Network; return result; }
  NetworkClientSecure client;
  HTTPClient http;
  prepare(http, client);
  if (!http.begin(client, FirmwareManifestUrl)) { result->error = UpdateError::Network; return result; }
  const int status = http.GET();
  if (status != 200) {
    http.end();
    result->error = status == 404 || (status > 0 && status < 500) ? UpdateError::InvalidRelease : UpdateError::Network;
    return result;
  }
  const int expected = http.getSize();
  if (expected > static_cast<int>(MaxReleaseBytes)) { http.end(); result->error = UpdateError::InvalidRelease; return result; }
  ResponseBuffer body(expected >= 0 ? static_cast<std::size_t>(expected) : MaxReleaseBytes);
  const int read = http.writeToStream(&body);
  http.end();
  if (body.oversized()) result->error = UpdateError::InvalidRelease;
  else if (body.outOfMemory()) result->error = UpdateError::Storage;
  else if (body.timedOut() || read < 0 || (expected >= 0 && read != expected)) result->error = UpdateError::Network;
  else if (!decodeRelease(body.body(), result->release)) result->error = UpdateError::InvalidRelease;
  return result;
}

std::unique_ptr<UpdateResult> EspUpdate::write(const FirmwareRelease& release) {
  auto result = std::make_unique<UpdateResult>();
  result->release = release;
  const auto* target = esp_ota_get_next_update_partition(nullptr);
  if (!target || release.size > target->size) { result->error = UpdateError::Storage; return result; }
  if (WiFi.status() != WL_CONNECTED) { result->error = UpdateError::Network; return result; }
  NetworkClientSecure client;
  HTTPClient http;
  prepare(http, client);
  if (!http.begin(client, release.url.c_str())) { result->error = UpdateError::Network; return result; }
  const int status = http.GET();
  if (status != 200 || http.getSize() != static_cast<int>(release.size)) {
    http.end();
    result->error = status > 0 && status < 500 && status != 429 ? UpdateError::Download : UpdateError::Network;
    return result;
  }
  esp_ota_handle_t handle = 0;
  // Sequential erase keeps each flash operation short while the display runs.
  if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
    http.end(); result->error = UpdateError::Storage; return result;
  }
  // Flash writes read the source while the cache is off; keep it in internal RAM.
  std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)> buffer(
      static_cast<std::uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)), heap_caps_free);
  Sha256 digest;
  auto* stream = http.getStreamPtr();
  const std::uint32_t started = millis();
  std::uint32_t lastData = started;
  std::uint32_t written = 0;
  while (buffer && written < release.size) {
    const auto now = static_cast<std::uint32_t>(millis());
    if (now - started > DownloadBudgetMs || now - lastData > StallLimitMs || (!stream->connected() && !stream->available())) break;
    const int available = stream->available();
    if (available <= 0) { delay(10); continue; }
    const auto wanted = std::min<std::size_t>({static_cast<std::size_t>(available), std::size_t{4096}, static_cast<std::size_t>(release.size - written)});
    const auto read = stream->readBytes(buffer.get(), wanted);
    if (read == 0) continue;
    if (esp_ota_write(handle, buffer.get(), read) != ESP_OK) { esp_ota_abort(handle); http.end(); result->error = UpdateError::Storage; return result; }
    digest.update(buffer.get(), read);
    written += read;
    lastData = now;
    // Let core 0's idle task run so the task watchdog stays satisfied.
    vTaskDelay(1);
    progress_ = static_cast<std::uint8_t>(static_cast<std::uint64_t>(written) * 100 / release.size);
  }
  http.end();
  if (written != release.size) { esp_ota_abort(handle); result->error = buffer ? UpdateError::Download : UpdateError::Storage; return result; }
  if (digest.finish() != release.sha256) { esp_ota_abort(handle); result->error = UpdateError::Integrity; return result; }
  // esp_ota_end checks the image header, segments and appended hash.
  if (esp_ota_end(handle) != ESP_OK) { result->error = UpdateError::Integrity; return result; }
  if (esp_ota_set_boot_partition(target) != ESP_OK) { result->error = UpdateError::Storage; return result; }
  return result;
}

}  // namespace vibemon
