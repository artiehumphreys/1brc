#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

inline constexpr size_t SCAN_WIDTH = 64;
inline constexpr size_t MAX_LINE_LENGTH = 23;
inline constexpr size_t MIN_LINE_LENGTH = 6;

inline constexpr size_t MAX_LINES_PER_WINDOW = SCAN_WIDTH / MIN_LINE_LENGTH;

struct Delims {
  uint64_t semicolons, newlines;
};

#if defined(__AVX2__)
#include <immintrin.h>

inline Delims find_delims(const char *c) {
  const __m256i first_32 =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(c));
  const __m256i second_32 =
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(c + 32));

  // hoisted by compiler
  const __m256i semicolon = _mm256_set1_epi8(';');
  const __m256i newline = _mm256_set1_epi8('\n');

  // 1. generate binary mask, non-zero for matched byte(s)
  // 2. reduce that mask to 32 bits using the MSB of each elem
  const uint32_t s_first =
      _mm256_movemask_epi8(_mm256_cmpeq_epi8(first_32, semicolon));
  const uint32_t s_second =
      _mm256_movemask_epi8(_mm256_cmpeq_epi8(second_32, semicolon));
  const uint32_t n_first =
      _mm256_movemask_epi8(_mm256_cmpeq_epi8(first_32, newline));
  const uint32_t n_second =
      _mm256_movemask_epi8(_mm256_cmpeq_epi8(second_32, newline));

  uint64_t semicolon_pos = (static_cast<uint64_t>(s_second) << 32) | s_first;
  uint64_t newline_pos = (static_cast<uint64_t>(n_second) << 32) | n_first;

  // return binary masks, ctz for index of each instance
  return {semicolon_pos, newline_pos};
}
#else
inline Delims find_delims(const char *c) {
  Delims delims{0, 0};
  for (size_t i = 0; i < SCAN_WIDTH; ++i) {
    // generate same mask as above
    delims.semicolons |= static_cast<uint64_t>(c[i] == ';') << i;
    delims.newlines |= static_cast<uint64_t>(c[i] == '\n') << i;
  }
  return delims;
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

inline int16_t parse_temperature(const char *p) {
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

  return temp;
}
