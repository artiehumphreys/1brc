#pragma once

#include <cstdint>
#include <limits>

struct Stats {
  static constexpr int count_bits = 24;
  static constexpr uint64_t count_mask = (uint64_t{1} << count_bits) - 1;

  uint64_t packed{0};
  int16_t min = std::numeric_limits<int16_t>::max();
  int16_t max = std::numeric_limits<int16_t>::min();

  constexpr void add(int16_t tenths) {
    packed += (static_cast<uint64_t>(tenths) << count_bits) + 1;
  }

  constexpr uint32_t count() const {
    return static_cast<uint32_t>(packed & count_mask);
  }
  // arithmetic shift sign-extends the 40-bit sum, whose sign bit is bit 63
  constexpr int64_t sum() const {
    return static_cast<int64_t>(packed) >> count_bits;
  }
};
