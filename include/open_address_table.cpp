#pragma once

#include "stats.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

struct Slot {
  std::string_view key;
  Stats s;
};

class FlatHashMap {
public:
  FlatHashMap() : tbl_(capacity_) {}

  // http://www.isthe.com/chongo/tech/comp/fnv/#FNV-1a
  // 64-bit fnv-1a hash (constexpr)
  static constexpr uint64_t hash(std::string_view sv) {
    uint64_t h = fnv_offset_basis_;
    for (unsigned char c : sv) {
      h ^= c;
      h *= fnv_prime_;
    }

    h ^= h >> 32; // avalanche
    return h;
  }

  Stats &operator[](std::string_view sv) {
    size_t i = hash(sv) & mask_;
    while (!tbl_[i].key.empty()) {
      if (tbl_[i].key == sv)
        return tbl_[i].s;
      i = (i + 1) & mask_; // linear probing
    }
    tbl_[i].key = sv;
    return tbl_[i].s;
  }

  const std::vector<Slot> &slots() const { return tbl_; } // merge: skip empties

private:
  static constexpr size_t capacity_ = 1 << 15; // 32K elems
  static constexpr size_t mask_ = capacity_ - 1;
  static constexpr uint64_t fnv_prime_ = 1099511628211ull;
  static constexpr uint64_t fnv_offset_basis_ = 14695981039346656037ull;
  std::vector<Slot> tbl_;
};
