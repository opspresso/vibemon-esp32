#pragma once
#include "../adapters/bounded_body.h"
#include <Arduino.h>
#include <cstdint>
#include <esp_heap_caps.h>

namespace vibemon {
// HTTPClient decodes chunk framing before writing here, including when the
// server sends no Content-Length. Both cloud and release reads use this bound.
class ResponseBuffer final : public Stream {
 public:
  explicit ResponseBuffer(std::size_t limit)
      : body_(limit, [](void* pointer, std::size_t bytes) { return heap_caps_realloc(pointer, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }, heap_caps_free),
        startedAt_(millis()) {}
  bool oversized() const { return body_.oversized(); }
  bool outOfMemory() const { return body_.outOfMemory(); }
  bool timedOut() const { return timedOut_; }
  std::string_view body() const { return body_.body(); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  std::size_t write(std::uint8_t byte) override { return write(&byte, 1); }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    if (static_cast<std::uint32_t>(millis() - startedAt_) > 15000) { timedOut_ = true; return 0; }
    return body_.append(data, size) ? size : 0;
  }
 private:
  BoundedBody body_;
  std::uint32_t startedAt_;
  bool timedOut_ = false;
};
}  // namespace vibemon
