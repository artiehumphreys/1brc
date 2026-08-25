#include "include/open_address_table.cpp"
#include "include/stats.hpp"
#include "include/utils.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <iterator>
#include <map>
#include <print>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <vector>

const char *INPUT_FILE = "input.txt";
const char *OUTPUT_FILE = "output.txt";

namespace fs = std::filesystem;

using Table = FlatHashMap;

struct StationNameMask { // store the <= 16 byte station name in 2 8-byte ints
  uint64_t low_bytes, high_bytes;
};

void write_results(std::map<std::string_view, Stats> mp) {
  FILE *file = std::fopen(OUTPUT_FILE, "w");
  if (file == nullptr) {
    std::perror("Could not open output file");
    return;
  }

  for (const auto &[station, s] : mp) {
    const double min = s.min / 10.0;
    const double avg = static_cast<double>(s.sum) / s.count / 10.0;
    const double max = s.max / 10.0;

    std::string line =
        std::format("{};{:.1f};{:.1f};{:.1f}\n", station, min, avg, max);

    std::fwrite(line.data(), sizeof(char), line.size(), file);
  }

  std::fclose(file);
}

std::size_t next_line_start(std::span<const char> data, std::size_t from) {
  // invariant: every line ends in newline character
  const auto newline =
      std::ranges::find(data.begin() + from, std::unreachable_sentinel, '\n');
  return newline == data.end() ? data.size() : ((newline - data.begin()) + 1);
}

static constexpr std::array<StationNameMask, 17> station_masks = []() {
  std::array<StationNameMask, 17> masks{};
  for (size_t i = 1; i < size_t{17}; ++i) {
    masks[i].low_bytes = i < 8 ? (masks[i - 1].low_bytes << 8) | 0xFF
                               : ~uint64_t{0}; // masks at byte granularity
    masks[i].high_bytes = i > 8 ? (masks[i - 1].high_bytes << 8) | 0xFF : 0;
  }
  return masks;
}();

Table process(std::span<const char> chunk) {
  auto start = std::chrono::steady_clock::now();
  Table ts{};

  const char *begin = chunk.data();
  const char *end = begin + chunk.size();
  while (begin < end) {
    const size_t station_length = find_delim(begin, ';');

    uint64_t s0, s1;
    // NOTE: 16 byte load on final line less than 16 bytes can SIGSEGV
    std::memcpy(&s0, begin,
                sizeof(uint64_t)); // store the name in two 8-byte integers
                                   // (station name guaranteed <= 16 characters)
    std::memcpy(&s1, begin + sizeof(uint64_t), sizeof(uint64_t));

    const StationNameMask station_name_mask = station_masks[station_length];
    // branchless store name (assumes little endian layout)
    s0 &= station_name_mask.low_bytes;
    s1 &= station_name_mask.high_bytes;

    begin += station_length + 1;

    const Reading r = parse_temperature(begin);
    Stats &s = ts.at(s0, s1);
    s.sum += r.tenths;
    s.count += 1;

    s.min = std::min(s.min, r.tenths);
    s.max = std::max(s.max, r.tenths);

    begin += r.length;
  }

  std::println("chunk of {}MB took {}", chunk.size_bytes() >> 20,
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start));
  return ts; // moved, not copied
}

int main() {
  auto start = std::chrono::steady_clock::now();

  FILE *file = fopen(INPUT_FILE, "r");
  if (file == nullptr) {
    std::perror("Could not open input file");
    return 1;
  }

  int fd = fileno(file);
  std::size_t size = fs::file_size(fs::path(INPUT_FILE));
  if (size == 0) {
    fclose(file);
    return 0;
  }

  void *f = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (f == MAP_FAILED) {
    std::perror("mmap");
    fclose(file);
    return 1;
  }

  madvise(f, size, MADV_SEQUENTIAL);

#if defined(__linux__)
  madvise(f, size, MADV_HUGEPAGE);
#endif

  const char *chr = static_cast<const char *>(f);

  const std::size_t num_threads =
      std::max(1u, std::thread::hardware_concurrency());
  const std::size_t estimated_chunk_size = size / num_threads;

  std::span<const char> data(chr, size);
  std::vector<std::future<Table>> workers;

  std::size_t begin = 0;

  for (std::size_t i = 0; i < num_threads && begin < size; ++i) {
    // statically calculate the bounds of each worker thread
    std::size_t end =
        (i + 1 == num_threads)
            ? size
            : next_line_start(data,
                              std::min(begin + estimated_chunk_size, size));
    workers.push_back(
        std::async(std::launch::async, process,
                   data.subspan(begin, end - begin))); // [begin, end)
    begin = end;
  }

  std::vector<Table> tables;
  tables.reserve(workers.size());
  for (auto &fut : workers) {
    tables.push_back(fut.get());
  }

  std::map<std::string_view, Stats> merged;
  for (const Table &t : tables) {
    for (const auto &[begin, s] : t.slots()) {
      const auto *end = std::ranges::find(begin, '\0');
      if (begin == end) // empty slot
        continue;
      Stats &g = merged[std::string_view{begin, end}];
      g.sum += s.sum;
      g.count += s.count;
      g.min = std::min(g.min, s.min);
      g.max = std::max(g.max, s.max);
    }
  }

  write_results(merged);

  auto end = std::chrono::steady_clock::now();

  std::println(
      "Processed all 1B rows in {}",
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start));

  munmap(f, size);
  fclose(file);
  return 0;
}
