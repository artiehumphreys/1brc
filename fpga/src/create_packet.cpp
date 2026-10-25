#include "packet.hpp"

#include <_stdio.h>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>
#include <sys/mman.h>

const char *INPUT_FILE = "input.txt";

namespace fs = std::filesystem;

class FileManager {
public:
  FileManager() = default;

  std::span<const char> open_file() {
    FILE *file = fopen(INPUT_FILE, "r");
    if (file == nullptr) {
      std::perror("Failed to open input file");
    }

    int fd = fileno(file);
    std::size_t size = fs::file_size(fs::path(INPUT_FILE));
    if (size == 0) {
      fclose(file);
    }

    void *data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
      std::perror("mmap");
      fclose(file);
    }

    return {static_cast<const char *>(data), size};
  }

private:
};

class PacketBuilder {
public:
  PacketBuilder(size_t packet_size = DEFAULT_PACKET_BYTES)
      : packet_size_(packet_size) {}

private:
  size_t packet_size_;
};
