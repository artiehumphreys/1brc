#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

inline constexpr size_t SCAN_WIDTH = 32;

#if defined(__AVX2__)
#include <immintrin.h>

inline size_t find_delim(const char *c, const char delim) {
  // 16 byte name + semicolon -> 256 bit register
  // NOTE: not generalized for > 32 chars
  const __m256i chunk = _mm256_loadu_si256((const __m256i *)c);
  const __m256i needle = _mm256_set1_epi8(delim);

  const __m256i cmp = _mm256_cmpeq_epi8(chunk, needle);

  const uint32_t mask = _mm256_movemask_epi8(cmp);
  return static_cast<size_t>(std::countr_zero(mask));
}

#else
inline size_t find_delim(const char *c, const char delim) {
  size_t i = 0;
  while (i < SCAN_WIDTH && c[i] != delim)
    ++i;
  return i;
}
#endif

// NOTE: this relies on the fact that '0' to '9' set the 0x10 bit, but
// '-' and '.' do not
inline constexpr uint64_t DIGIT_BIT = 0x10;
inline constexpr int DIGIT_BIT_POS = 4;

inline constexpr uint64_t DOT_MASK =
    DIGIT_BIT << 8 | DIGIT_BIT << 16 |
    DIGIT_BIT << 24; // '.' can only appear in byte 2, 3, or 4 (1-indexed)

// NOTE: anything refered to as 'aligned' means the number is of the form
// 0XY.Z000

inline constexpr int ALIGNED_DOT_POS = 3 * 8 + DIGIT_BIT_POS; // bit position

inline constexpr uint64_t ALIGNED_DIGITS_MASK =
    0x0Full << 8 | 0x0Full << 16 | 0x0Full << 32; // normalized bytes 2, 3, 5

inline constexpr uint64_t PLACE_VALUES =
    100ull << 24 | 10ull << 16 | 1ull; // one multiply sums place values

struct Reading {
  int16_t tenths;
  size_t length; // bytes consumed, including '\n'
};

inline Reading parse_temperature(const char *p) {
  // [-99.9, 99.9] -> one 8 byte load for reading
  uint64_t word;
  std::memcpy(&word, p, sizeof(word));

  const int64_t sign =
      static_cast<int64_t>(~word << (63 - DIGIT_BIT_POS)) >> 63;
  const uint64_t keep = ~static_cast<uint64_t>(
      sign & 0xFF); // zero-out sign if present (lower byte)

  const int dot_pos = std::countr_zero(~word & DOT_MASK);

  const uint64_t sign_removed = word & keep;
  const uint64_t digits =
      (sign_removed << (ALIGNED_DOT_POS - dot_pos)) & ALIGNED_DIGITS_MASK;

  const int64_t abs_value =
      static_cast<int64_t>((digits * PLACE_VALUES) >> 32) & 0x3FF;

  int16_t temp = static_cast<int16_t>((abs_value ^ sign) - sign);
  size_t next_line_start = (dot_pos >> 3) + 3;

  return {temp, next_line_start};
}
