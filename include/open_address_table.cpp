#pragma once

#include "stats.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

struct Slot {
  alignas(16) char key[16];
  Stats s;
};

class FlatHashMap {
public:
  FlatHashMap() : tbl_(capacity_) {}

  static constexpr size_t capacity_ = 1 << 15; // 32K elems
  static constexpr int idx_bits_ = std::countr_zero(capacity_);

  // multiply-shift w/ two independent multiplies
  // the table only needs idx_bits_ bits
  static constexpr uint32_t index(uint64_t s0, uint64_t s1) {
    return static_cast<uint32_t>(((s0 * k0_) ^ (s1 * k1_)) >> (64 - idx_bits_));
  }

  void prefetch(uint32_t i) const { __builtin_prefetch(&tbl_[i]); }

  Stats &at(uint32_t i, uint64_t s0, uint64_t s1) {
    for (;;) {
      uint64_t k0, k1;
      std::memcpy(&k0, tbl_[i].key, sizeof(uint64_t));
      std::memcpy(&k1, tbl_[i].key + sizeof(uint64_t), sizeof(uint64_t));

      if (((k0 ^ s0) | (k1 ^ s1)) == 0) // hit
        return tbl_[i].s;

      // names are >= 1 chars, so an all-zero key can only be an unused slot
      if ((k0 | k1) == 0) {
        std::memcpy(tbl_[i].key, &s0, sizeof(uint64_t));
        std::memcpy(tbl_[i].key + sizeof(uint64_t), &s1, sizeof(uint64_t));
        return tbl_[i].s;
      }

      i = (i + 1) & mask_; // linear probing
    }
  }

  const std::vector<Slot> &slots() const { return tbl_; } // merge: skip empties

private:
  static constexpr uint32_t mask_ = capacity_ - 1;

  // xxHash64 PRIME64_1 / PRIME64_2
  // https://github.com/Cyan4973/xxHash/blob/dev/xxhash.h
  static constexpr uint64_t k0_ = 0x9E3779B185EBCA87ull;
  static constexpr uint64_t k1_ = 0xC2B2AE3D27D4EB4Full;
  std::vector<Slot> tbl_;
};
