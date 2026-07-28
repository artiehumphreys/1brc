#include "include/open_address_table.cpp"
#include "include/stats.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <future>
#include <iterator>
#include <map>
#include <print>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <utility>
#include <vector>

const char *INPUT_FILE = "input.txt";
const char *OUTPUT_FILE = "output.txt";

namespace fs = std::filesystem;

using Table = FlatHashMap;

void write_results(std::map<std::string_view, Stats> mp) {
  FILE *file = std::fopen(OUTPUT_FILE, "w");
  if (file == nullptr) {
    std::perror("Could not open output file");
    return;
  }

  std::vector<const std::pair<const std::string_view, Stats> *> arr;
  arr.reserve(mp.size());

  for (const auto &pair : mp) {
    arr.push_back(&pair);
  }

  std::ranges::sort(
      arr, [](const auto *a, const auto *b) { return a->first < b->first; });

  for (const auto *pair : arr) {
    const auto &[station, s] = *pair;
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

int16_t parse_integer_tenths(std::string_view s) {
  auto it = s.begin();
  int sign = 1;

  auto get_digit_at = [](auto &it) {
    return *it++ - '0'; // get numeric value
  };

  if (*it == '-') {
    sign = -1;
    ++it;
  }

  int val = get_digit_at(it);
  if (*it != '.')
      [[likely]] // ~90 % of readings will have 2 digits preceeding the decimal
    val = val * 10 + get_digit_at(it);
  ++it;

  int final =
      sign * (val * 10 +
              (get_digit_at(it))); // actual value * 10, avoid float convesion
  return static_cast<int16_t>(final);
}

Table process(std::span<const char> chunk) {
  auto start = std::chrono::steady_clock::now();
  Table ts{};

  auto begin = chunk.begin();
  while (begin != chunk.end()) {
    const auto semicolon = std::ranges::find(
        begin, std::unreachable_sentinel,
        ';'); // semicolon always present, no need for bounds check
    std::string_view station = {begin, semicolon};

    begin = semicolon + 1;

    const auto newline = std::ranges::find(begin, std::unreachable_sentinel,
                                           '\n'); // TODO: too many ops?
    std::string_view value = {begin, newline};

    int16_t reading = parse_integer_tenths(value);
    Stats &s = ts[station];
    s.sum += reading;
    s.count += 1;

    s.min = std::min(s.min, reading);
    s.max = std::min(s.max, reading);

    begin = newline + 1;
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

  std::map<std::string_view, Stats> merged;
  for (auto &fut : workers) {
    for (const auto &[station, s] : fut.get().slots()) {
      Stats &g = merged[station];
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
