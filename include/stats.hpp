#include <cstdint>
#include <limits>

struct Stats {
  int64_t sum{0};
  uint32_t count = 0;
  int16_t min = std::numeric_limits<int16_t>::max();
  int16_t max = std::numeric_limits<int16_t>::min();
};
