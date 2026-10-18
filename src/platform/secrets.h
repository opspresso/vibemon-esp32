#pragma once
#include <cstddef>
#include <string>

namespace vibemon {
// Overwrites secret bytes before their memory returns to the heap.
// Library-internal copies, such as HTTP header buffers, are out of reach.
inline void wipe(char* data, std::size_t size) {
  volatile char* byte = data;
  while (size--) *byte++ = 0;
}
inline void wipe(std::string& value) {
  wipe(value.data(), value.size());
  value.clear();
}
}  // namespace vibemon
