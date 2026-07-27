#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unordered_map>
#include <vector>

const char *FILE_NAME = "input.txt";

namespace fs = std::filesystem;

struct Stats {
  // store sum, count, min, max
  float sum{0};
  std::uint32_t count = 0;
  float min = std::numeric_limits<float>::max();
  float max = std::numeric_limits<float>::max();
};

class GlobalState {
public:
  GlobalState() = default;

  void add_measurement(std::string_view station, float val) {
    std::lock_guard<std::mutex> lk(mtx_);
    Stats &s = mp_[station];

    s.sum += val;
    ++s.count;
    s.min = std::min(s.min, val);
    s.max = std::max(s.max, val);
  }

private:
  std::mutex mtx_;
  std::unordered_map<std::string_view, Stats> mp_;
};

GlobalState gs{};

std::size_t next_line_start(std::span<const char> data, std::size_t from) {
  // invariant: every line ends in newline character
  const auto newline =
      std::ranges::find(data.begin() + from, std::unreachable_sentinel, '\n');
  return newline == data.end() ? data.size() : ((newline - data.begin()) + 1);
}

int main() {
  std::vector<std::thread> worker_threads;

  FILE *file = fopen(FILE_NAME, "r");
  if (file == nullptr) {
    std::perror("Could not open file");
    return 1;
  }

  int fd = fileno(file);
  std::size_t size = fs::file_size(fs::path(FILE_NAME));
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

  const char *chr = static_cast<const char *>(f);

  const std::size_t num_threads =
      std::max(1u, std::thread::hardware_concurrency());
  const std::size_t estimated_chunk_size = size / num_threads;

  std::span<const char> data(chr, size);

  std::size_t begin = 0;

  for (std::size_t i = 0; i < num_threads && begin < size; ++i) {
    // statically calculate the bounds of each worker thread
    std::size_t end =
        (i + 1 == num_threads)
            ? size
            : next_line_start(data,
                              std::min(begin + estimated_chunk_size, size));
    worker_threads.emplace_back(
        process, data.subspan(begin, end - begin)); // [begin, end)
    begin = end;
  }

  for (std::thread &t : worker_threads) {
    t.join();
  }

  munmap(f, size);
  fclose(file);
  return 0;
}
