#pragma once

#include "stats.hpp"

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

  // fmix64 over the two key halves
  // https://encode.su/threads/1747-Extremely-fast-hash#post_message_34156
  static constexpr uint64_t hash(uint64_t s0, uint64_t s1) {
    uint64_t h = s0 ^ (s1 * fnv_prime_);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
  }

  Stats &at(uint64_t s0, uint64_t s1) {
    size_t i = hash(s0, s1) & mask_;
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
  static constexpr size_t capacity_ = 1 << 15; // 32K elems
  static constexpr size_t mask_ = capacity_ - 1;
  static constexpr uint64_t fnv_prime_ = 1099511628211ull;
  std::vector<Slot> tbl_;
};
