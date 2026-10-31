#include "gb/media/position.hpp"

#include "gb/media/chapters.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace gb::media {
namespace {

// Ten days of milliseconds: far beyond any recording, small enough to never overflow.
constexpr std::int64_t kMaximumMilliseconds = 10LL * 24 * 3600 * 1000;

bool all_digits(const std::string &text) {
  if (text.empty())
    return false;
  for (const auto c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0)
      return false;
  }
  return true;
}

// Digits with an optional fraction: "31", "31.4", "331.400".
std::optional<long double> decimal(const std::string &text) {
  const auto dot = text.find('.');
  const auto whole = text.substr(0, dot);
  if (!all_digits(whole) || whole.size() > 9U)
    return std::nullopt;
  long double value = std::stold(whole);
  if (dot != std::string::npos) {
    const auto fraction = text.substr(dot + 1U);
    if (!all_digits(fraction) || fraction.size() > 9U)
      return std::nullopt;
    value += std::stold(fraction) / std::pow(10.0L, static_cast<long double>(fraction.size()));
  }
  return value;
}

std::optional<std::int64_t> parse_time(const std::string &text) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const auto colon = text.find(':', start);
    fields.push_back(text.substr(start, colon - start));
    if (colon == std::string::npos)
      break;
    start = colon + 1U;
  }
  if (fields.empty() || fields.size() > 3U)
    return std::nullopt;
  const auto seconds = decimal(fields.back());
  if (!seconds)
    return std::nullopt;
  long double total = *seconds;
  if (fields.size() >= 2U) {
    const auto &minutes = fields[fields.size() - 2U];
    if (*seconds >= 60.0L || !all_digits(minutes) || minutes.size() > 6U)
      return std::nullopt;
    const auto value = std::stold(minutes);
    if (fields.size() == 3U && value >= 60.0L)
      return std::nullopt;
    total += value * 60.0L;
  }
  if (fields.size() == 3U) {
    if (!all_digits(fields[0]) || fields[0].size() > 4U)
      return std::nullopt;
    total += std::stold(fields[0]) * 3600.0L;
  }
  const auto milliseconds = std::llround(total * 1000.0L);
  if (milliseconds > kMaximumMilliseconds)
    return std::nullopt;
  return static_cast<std::int64_t>(milliseconds);
}

} // namespace

std::optional<Position> parse_position(const std::string &text) {
  if (text.empty() || text.size() > 64U)
    return std::nullopt;
  Position position;
  std::string time = text;
  if ((text[0] == 'c' || text[0] == 'C') && text.find(':') != std::string::npos) {
    const auto colon = text.find(':');
    const auto number = text.substr(1, colon - 1U);
    if (!all_digits(number) || number.size() > 4U)
      return std::nullopt;
    const auto chapter = std::stoi(number);
    if (chapter < 1)
      return std::nullopt;
    position.chapter = chapter;
    time = text.substr(colon + 1U);
  } else if (const auto colon = text.find(':');
             colon != std::string::npos && parse_chapter_name(text.substr(0, colon))) {
    position.file = text.substr(0, colon);
    time = text.substr(colon + 1U);
  }
  const auto milliseconds = parse_time(time);
  if (!milliseconds)
    return std::nullopt;
  position.milliseconds = *milliseconds;
  return position;
}

std::string format_clock_milliseconds(const std::int64_t milliseconds) {
  if (milliseconds < 0)
    return "--:--:--.---";
  std::array<char, 40> text{};
  std::snprintf(text.data(), text.size(), "%02lld:%02lld:%02lld.%03lld",
                static_cast<long long>(milliseconds / 3600000),
                static_cast<long long>((milliseconds / 60000) % 60),
                static_cast<long long>((milliseconds / 1000) % 60),
                static_cast<long long>(milliseconds % 1000));
  return text.data();
}

std::string format_clock(const long double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0L ||
      seconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max() / 1000))
    return "--:--:--.---";
  return format_clock_milliseconds(static_cast<std::int64_t>(std::floor(seconds * 1000.0L)));
}

std::string format_position(const std::optional<int> chapter, const std::int64_t milliseconds) {
  auto clock = format_clock_milliseconds(milliseconds);
  return chapter ? "c" + std::to_string(*chapter) + ":" + clock : clock;
}

std::string format_position_for_file(const std::optional<int> chapter,
                                     const std::int64_t milliseconds) {
  const auto value = std::max<std::int64_t>(milliseconds, 0);
  std::array<char, 48> text{};
  const auto hours = value / 3600000;
  if (hours > 0) {
    std::snprintf(text.data(), text.size(), "%lldh%02lldm%02llds%03lld",
                  static_cast<long long>(hours), static_cast<long long>((value / 60000) % 60),
                  static_cast<long long>((value / 1000) % 60),
                  static_cast<long long>(value % 1000));
  } else {
    std::snprintf(text.data(), text.size(), "%02lldm%02llds%03lld",
                  static_cast<long long>((value / 60000) % 60),
                  static_cast<long long>((value / 1000) % 60),
                  static_cast<long long>(value % 1000));
  }
  return chapter ? "c" + std::to_string(*chapter) + "-" + text.data() : std::string(text.data());
}

} // namespace gb::media
