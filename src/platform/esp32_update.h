#pragma once

#include "../application/ports.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace vibemon {

// Reads ota.json and streams an application image into the slot that is not
// running. The SHA-256 digest and ESP-IDF image validation must both pass
// before the next boot selects the new slot.
class EspUpdate final : public UpdatePort {
 public:
  bool begin();
  bool check() override;
  bool install(const FirmwareRelease& release) override;
  std::unique_ptr<UpdateResult> take() override;
  std::uint8_t progress() const override { return progress_; }
  void confirm() override;
  void restart() override;
  // The running slot name, for diagnostics.
  const char* runningSlot() const;
 private:
  struct Job { bool install; FirmwareRelease release; };
  QueueHandle_t jobs_ = nullptr;
  QueueHandle_t results_ = nullptr;
  TaskHandle_t worker_ = nullptr;
  std::atomic<bool> busy_{false};
  std::atomic<std::uint8_t> progress_{0};
  bool submit(Job* job);
  static void work(void* context);
  static std::unique_ptr<UpdateResult> fetchRelease();
  std::unique_ptr<UpdateResult> write(const FirmwareRelease& release);
};

}  // namespace vibemon
