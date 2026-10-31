#include "gb/media/position.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void expect(const std::string &text, const std::optional<int> chapter, const std::int64_t ms) {
  const auto position = gb::media::parse_position(text);
  require(position.has_value(), "does not parse: " + text);
  require(position->chapter == chapter && position->milliseconds == ms && !position->file,
          "wrong value: " + text);
}

void reject(const std::string &text) {
  require(!gb::media::parse_position(text).has_value(), "should be rejected: " + text);
}

} // namespace

int main() {
  try {
    expect("c2:00:05:31.400", 2, 331400);
    expect("C2:00:05:31.400", 2, 331400);
    expect("c2:5:31.4", 2, 331400);
    expect("c2:331.4", 2, 331400);
    expect("c1:0", 1, 0);
    expect("c12:01:00:00", 12, 3600000);
    expect("00:05:31.400", std::nullopt, 331400);
    expect("331.4", std::nullopt, 331400);
    expect("90:00", std::nullopt, 5400000); // minutes may exceed 59 without hours
    expect("0.0006", std::nullopt, 1);      // rounded to the millisecond

    const auto file = gb::media::parse_position("GH020073.MP4:00:05:31.400");
    require(file && file->file == std::optional<std::string>("GH020073.MP4") && !file->chapter &&
                file->milliseconds == 331400,
            "file-name form");

    reject("");
    reject("c0:00:01");
    reject("c:00:01");
    reject("cx:00:01");
    reject("c2");
    reject("-1");
    reject("00:60:00");
    reject("01:00:60");
    reject("1:2:3:4");
    reject("1e3");
    reject("12.");
    reject(".5");
    reject("nan");
    reject("notafile.mp4:00:01");
    reject("999999999");

    require(gb::media::format_position(2, 331400) == "c2:00:05:31.400", "format with chapter");
    require(gb::media::format_position(std::nullopt, 331400) == "00:05:31.400", "format bare");
    require(gb::media::format_clock(331.4009L) == "00:05:31.400", "clock floors");
    require(gb::media::format_clock(-1.0L) == "--:--:--.---", "negative clock");
    require(gb::media::format_position_for_file(2, 331400) == "c2-05m31s400", "file form");
    require(gb::media::format_position_for_file(std::nullopt, 3931400) == "1h05m31s400",
            "file form with hours");
    for (const std::int64_t ms : {0LL, 1LL, 59999LL, 3600000LL, 86399999LL}) {
      const auto text = gb::media::format_position(3, ms);
      const auto back = gb::media::parse_position(text);
      require(back && back->chapter == 3 && back->milliseconds == ms, "round trip " + text);
    }
  } catch (const std::exception &error) {
    std::cerr << "gb_position_tests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "gb_position_tests: all tests passed\n";
  return 0;
}
