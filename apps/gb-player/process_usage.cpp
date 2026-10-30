#include "process_usage.hpp"

#include <sys/resource.h>
#include <unistd.h>

#include <fstream>

ProcessUsage sample_process_usage() {
  ProcessUsage usage;
  rusage resources{};
  if (getrusage(RUSAGE_SELF, &resources) == 0) {
    usage.cpu_seconds = static_cast<double>(resources.ru_utime.tv_sec + resources.ru_stime.tv_sec) +
                        static_cast<double>(resources.ru_utime.tv_usec + resources.ru_stime.tv_usec) / 1.0e6;
    usage.peak_resident_bytes = static_cast<std::uint64_t>(resources.ru_maxrss) * 1024U;
  }
  std::ifstream statm("/proc/self/statm");
  std::uint64_t size{};
  std::uint64_t resident{};
  if (statm >> size >> resident) {
    const auto page = sysconf(_SC_PAGESIZE);
    usage.resident_bytes = resident * static_cast<std::uint64_t>(page > 0 ? page : 4096);
  }
  return usage;
}
