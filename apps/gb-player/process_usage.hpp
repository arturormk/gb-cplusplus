#pragma once

#include <cstdint>

// Linux process resource sampling for performance diagnostics.
struct ProcessUsage {
  double cpu_seconds{};
  std::uint64_t resident_bytes{};
  std::uint64_t peak_resident_bytes{};
};

[[nodiscard]] ProcessUsage sample_process_usage();
