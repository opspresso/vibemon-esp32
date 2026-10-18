#pragma once
#include <cstdint>

namespace vibemon {
// True when a 32-bit millisecond clock has reached `deadline`. The signed
// difference stays correct across the clock's wrap after about 49.7 days,
// for deadlines less than 24.8 days away.
inline bool due(std::uint32_t now, std::uint32_t deadline) { return static_cast<std::int32_t>(now - deadline) >= 0; }
}  // namespace vibemon
