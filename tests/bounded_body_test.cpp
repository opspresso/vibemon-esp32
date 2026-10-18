#include "adapters/bounded_body.h"
#include "fakes.h"
#include <cstdlib>

namespace {
std::size_t largestAllocation = 0;
unsigned released = 0;
bool allocationFails = false;
void* allocate(void* pointer, std::size_t bytes) {
  largestAllocation = std::max(largestAllocation, bytes);
  return allocationFails ? nullptr : std::realloc(pointer, bytes);
}
void release(void* pointer) { ++released; std::free(pointer); }
}

int main() {
  try {
    {
      BoundedBody body(2048, allocate, release);
      CHECK(body.body().empty() && body.append(nullptr, 0));
      const std::string chunk(1024, 'a');
      CHECK(body.append(chunk.data(), chunk.size()));
      CHECK(body.append(chunk.data(), chunk.size()));
      CHECK(body.body() == chunk + chunk && largestAllocation == 2048);
      CHECK(!body.append("x", 1) && body.oversized() && !body.outOfMemory());
      CHECK(!body.append(nullptr, 0) && largestAllocation == 2048);
    }
    CHECK(released == 1);
    {
      largestAllocation = 0;
      BoundedBody body(2048, allocate, release);
      const std::string oversized(2049, 'a');
      CHECK(!body.append(oversized.data(), oversized.size()) && body.oversized());
      CHECK(largestAllocation == 0 && body.body().empty());
    }
    CHECK(released == 1);
    {
      BoundedBody body(32769, allocate, release);
      const std::string chunk(16384, 'b');
      CHECK(body.append(chunk.data(), chunk.size()));
      CHECK(body.append(chunk.data(), chunk.size()));
      CHECK(body.append("x", 1));
      CHECK(body.body().size() == 32769 && largestAllocation == 32769);
    }
    CHECK(released == 2);
    {
      BoundedBody body(32768, allocate, release);
      CHECK(body.append("kept", 4));
      allocationFails = true;
      const std::string larger(20000, 'b');
      CHECK(!body.append(larger.data(), larger.size()) && body.outOfMemory());
      CHECK(body.body() == "kept" && !body.oversized());
      allocationFails = false;
      CHECK(!body.append("x", 1));
    }
    CHECK(released == 3);
    {
      BoundedBody body(0, allocate, release);
      CHECK(body.append(nullptr, 0) && body.body().empty());
      CHECK(!body.append("x", 1) && body.oversized());
    }
    CHECK(released == 3);
    std::cout << "PASS bounded body: fragmented limits, bounded growth, allocation failure, empty input and released buffers\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
