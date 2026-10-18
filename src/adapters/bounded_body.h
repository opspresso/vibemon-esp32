#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace vibemon {
// A streamed response cannot allocate or retain bytes beyond its limit.
// Firmware supplies PSRAM allocation; native checks use the same buffer with libc.
class BoundedBody {
 public:
  using Reallocate = void* (*)(void*, std::size_t);
  using Release = void (*)(void*);
  explicit BoundedBody(std::size_t limit, Reallocate reallocate = std::realloc, Release release = std::free)
      : limit_(limit), reallocate_(reallocate), release_(release) {}
  ~BoundedBody() { if (data_) release_(data_); }
  BoundedBody(const BoundedBody&) = delete;
  BoundedBody& operator=(const BoundedBody&) = delete;
  bool oversized() const { return oversized_; }
  bool outOfMemory() const { return outOfMemory_; }
  std::string_view body() const { return size_ ? std::string_view(data_, size_) : std::string_view{}; }
  bool append(const void* data, std::size_t size) {
    if (oversized_ || outOfMemory_) return false;
    if (size > limit_ - size_) { oversized_ = true; return false; }
    if (!size) return true;
    if (!reserve(size_ + size)) return false;
    std::memcpy(data_ + size_, data, size); size_ += size;
    return true;
  }
 private:
  char* data_ = nullptr;
  std::size_t limit_;
  std::size_t capacity_ = 0;
  std::size_t size_ = 0;
  Reallocate reallocate_;
  Release release_;
  bool oversized_ = false;
  bool outOfMemory_ = false;
  bool reserve(std::size_t needed) {
    if (needed <= capacity_) return true;
    auto capacity = std::max(capacity_, std::min(limit_, std::size_t{16 * 1024}));
    while (capacity < needed) capacity = capacity > limit_ / 2 ? limit_ : capacity * 2;
    auto* grown = static_cast<char*>(reallocate_(data_, capacity));
    if (!grown) { outOfMemory_ = true; return false; }
    data_ = grown; capacity_ = capacity;
    return true;
  }
};
}  // namespace vibemon
