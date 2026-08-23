#pragma once
#include <cstddef>

#if defined(__AVX2__)
#include <immintrin.h>

inline size_t find_delim(const char *c, const char delim) {
  // 16 byte name + semicolon -> 256 bit register
  // NOTE: not generalized for > 32 chars
  const __m256i chunk = _mm256_loadu_si256((const __m256i *)c);
  const __m256i needle = _mm256_set1_epi8(delim);

  const __m256i cmp = _mm256_cmpeq_epi8(chunk, needle);

  const uint32_t mask = _mm256_movemask_epi8(cmp);
  return static_cast<size_t>(_tzcnt_u32(mask)); // tzcnt(0) == 32
}

#else
inline size_t find_delim(const char *c, const char delim) {
  size_t i = 0;
  while (i < 32 && c[i] != delim)
    ++i;
  return i;
}
#endif
