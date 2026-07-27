#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <sys/mman.h>
#include <thread>
#include <vector>

const char *FILE_NAME = "input.txt";

namespace fs = std::filesystem;

std::size_t next_line_start(const unsigned char *data, std::size_t end,
                            std::size_t size) {
  while (end < size && data[end] != '\n') {
    ++end;
  }
  return end < size ? end + 1 : size;
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

  const unsigned char *chr = static_cast<const unsigned char *>(f);

  const std::size_t num_threads =
      std::max(1u, std::thread::hardware_concurrency());
  const std::size_t estimated_chunk_size = size / num_threads;

  std::size_t begin = 0;
  for (std::size_t i = 0; i < num_threads && begin < size; ++i) {
    // statically calculate the bounds of each worker thread
    std::size_t end =
        (i + 1 == num_threads)
            ? size
            : next_line_start(chr, std::min(begin + estimated_chunk_size, size),
                              size);
    // worker_threads.emplace_back(process, chr, begin, end); // [begin, end)
    begin = end;
  }

  for (std::thread &t : worker_threads) {
    t.join();
  }

  munmap(f, size);
  fclose(file);
  return 0;
}
