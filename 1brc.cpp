#include "include/open_address_table.cpp"
#include "include/stats.hpp"
#include "include/utils.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
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
    const Delims d = find_delims(begin);

    // NOTE: window can overrun into the next worker's chunk
    // keep only this chunk's bytes
    const size_t remaining = static_cast<size_t>(end - begin);
    const uint64_t in_chunk =
        remaining >= SCAN_WIDTH ? ~uint64_t{0} : (uint64_t{1} << remaining) - 1;

    uint64_t semis = d.semicolons & in_chunk;
    uint64_t newlines = d.newlines & in_chunk;
    const size_t advance = SCAN_WIDTH - std::countl_zero(newlines);

    size_t curr_line_start = 0;
    while (newlines) {
      const size_t semi = std::countr_zero(semis);
      const size_t newline = std::countr_zero(newlines);

      semis &= semis - 1;
      newlines &= newlines - 1;

      uint64_t s0, s1;
      // branchless store name (assumes little endian layout)
      std::memcpy(&s0, begin + curr_line_start, sizeof(s0));
      std::memcpy(&s1, begin + curr_line_start + sizeof(s0), sizeof(s1));

      const StationNameMask station_name_mask =
          station_masks[semi - curr_line_start];
      s0 &= station_name_mask.low_bytes;
      s1 &= station_name_mask.high_bytes;

      const int16_t temp = parse_temperature(begin + semi + 1);
      Stats &s = ts.at(s0, s1);
      s.sum += temp;
      s.count += 1;

      s.min = std::min(s.min, temp);
      s.max = std::max(s.max, temp);

      curr_line_start = newline + 1;
    }

    begin += advance;
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

  const size_t PADDING = SCAN_WIDTH;

  void *f =
      mmap(NULL, size + PADDING, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
           0); // pad the file mapping, then overlay the file on the front
               // ensures leftover tail is anonymous zero pages
  if (f == MAP_FAILED) {
    std::perror("mmap");
    fclose(file);
    return 1;
  }
  if (mmap(f, size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) == MAP_FAILED) {
    std::perror("mmap");
    munmap(f, size + PADDING);
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

  munmap(f, size + PADDING);
  fclose(file);
  return 0;
}
