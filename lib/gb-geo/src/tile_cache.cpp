#include "gb/geo/tile_cache.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace gb::geo {

namespace {

constexpr std::size_t kMaximumMetadataBytes = 4096;
constexpr std::size_t kMaximumValidatorLength = 512;

std::filesystem::path metadata_path(std::filesystem::path tile) {
  tile += ".meta";
  return tile;
}

std::string lowercase(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

// Validators are stored one per line, so they must not contain line breaks or other controls.
std::string sanitized_validator(const std::string &value) {
  if (value.size() > kMaximumValidatorLength) return {};
  for (const auto c : value) {
    if (static_cast<unsigned char>(c) < 0x20U || c == 0x7f) return {};
  }
  return value;
}

std::optional<std::int64_t> parse_integer(std::string_view text) {
  if (text.empty() || text.size() > 18) return std::nullopt;
  std::int64_t value{};
  for (const auto c : text) {
    if (c < '0' || c > '9') return std::nullopt;
    value = value * 10 + (c - '0');
  }
  return value;
}

std::optional<std::vector<std::uint8_t>> read_bounded(const std::filesystem::path &path, const std::size_t limit) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0U || size > limit) return std::nullopt;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) return std::nullopt;
  return bytes;
}

std::optional<TileMetadata> read_metadata(const std::filesystem::path &path) {
  const auto bytes = read_bounded(path, kMaximumMetadataBytes);
  if (!bytes.has_value()) return std::nullopt;
  TileMetadata metadata;
  bool has_fetched{};
  bool has_expires{};
  std::istringstream lines(std::string(bytes->begin(), bytes->end()));
  std::string line;
  while (std::getline(lines, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) continue;
    const auto key = std::string_view(line).substr(0, separator);
    const auto value = line.substr(separator + 1);
    if (key == "fetched") {
      const auto parsed = parse_integer(value);
      if (!parsed.has_value()) return std::nullopt;
      metadata.fetched_unix = *parsed;
      has_fetched = true;
    } else if (key == "expires") {
      const auto parsed = parse_integer(value);
      if (!parsed.has_value()) return std::nullopt;
      metadata.expires_unix = *parsed;
      has_expires = true;
    } else if (key == "etag") {
      metadata.etag = sanitized_validator(value);
    } else if (key == "last_modified") {
      metadata.last_modified = sanitized_validator(value);
    }
  }
  if (!has_fetched || !has_expires) return std::nullopt;
  return metadata;
}

std::int64_t unix_time(const std::filesystem::file_time_type time) {
  const auto system = std::chrono::time_point_cast<std::chrono::seconds>(
      time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  return system.time_since_epoch().count();
}

void write_atomically(const std::filesystem::path &path, const void *data, const std::size_t size) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) throw std::runtime_error("cannot create cache directory " + path.parent_path().string() + ": " + error.message());
  static thread_local std::mt19937_64 random{std::random_device{}()};
  auto temporary = path;
  temporary += ".tmp-" + std::to_string(random());
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    stream.close();
    if (!stream) {
      std::filesystem::remove(temporary, error);
      throw std::runtime_error("cannot write cache file " + temporary.string());
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    throw std::runtime_error("cannot replace cache file " + path.string());
  }
}

} // namespace

std::optional<std::filesystem::path> default_tile_cache_root() {
  const auto *xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg != nullptr && std::filesystem::path(xdg).is_absolute()) {
    return std::filesystem::path(xdg) / "gb" / "tiles";
  }
  const auto *home = std::getenv("HOME");
  if (home != nullptr && std::filesystem::path(home).is_absolute()) {
    return std::filesystem::path(home) / ".cache" / "gb" / "tiles";
  }
  return std::nullopt;
}

TileCache::TileCache(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path TileCache::tile_path(const TileProvider &provider, const TileKey key) const {
  validate(provider);
  if (!is_valid(key)) throw std::invalid_argument("invalid tile key");
  return root_ / provider.id / std::to_string(key.zoom) / std::to_string(key.x) /
         (std::to_string(key.y) + "." + provider.file_extension);
}

std::optional<CachedTile> TileCache::load(const TileProvider &provider, const TileKey key) const {
  const auto path = tile_path(provider, key);
  auto bytes = read_bounded(path, kMaximumTileBytes);
  if (!bytes.has_value()) return std::nullopt;
  CachedTile tile{std::move(*bytes), {}};
  if (auto metadata = read_metadata(metadata_path(path)); metadata.has_value()) {
    tile.metadata = std::move(*metadata);
  } else {
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    tile.metadata.fetched_unix = error ? 0 : unix_time(modified);
    tile.metadata.expires_unix = tile.metadata.fetched_unix + provider.default_ttl_seconds;
  }
  return tile;
}

void TileCache::store(const TileProvider &provider, const TileKey key, const std::span<const std::uint8_t> bytes,
                      const TileMetadata &metadata) const {
  if (bytes.empty() || bytes.size() > kMaximumTileBytes) throw std::runtime_error("tile size out of range");
  const auto path = tile_path(provider, key);
  write_atomically(path, bytes.data(), bytes.size());
  store_metadata(provider, key, metadata);
}

void TileCache::store_metadata(const TileProvider &provider, const TileKey key, const TileMetadata &metadata) const {
  std::ostringstream text;
  text << "fetched=" << std::max<std::int64_t>(0, metadata.fetched_unix) << '\n'
       << "expires=" << std::max<std::int64_t>(0, metadata.expires_unix) << '\n';
  if (const auto etag = sanitized_validator(metadata.etag); !etag.empty()) text << "etag=" << etag << '\n';
  if (const auto modified = sanitized_validator(metadata.last_modified); !modified.empty()) {
    text << "last_modified=" << modified << '\n';
  }
  const auto content = text.str();
  write_atomically(metadata_path(tile_path(provider, key)), content.data(), content.size());
}

CacheControl parse_cache_control(const std::string_view header) {
  CacheControl result;
  std::size_t start = 0;
  while (start <= header.size()) {
    const auto end = std::min(header.find(',', start), header.size());
    const auto directive = trim(header.substr(start, end - start));
    const auto equals = directive.find('=');
    const auto name = lowercase(trim(directive.substr(0, equals)));
    if (name == "no-store") {
      result.no_store = true;
    } else if (name == "no-cache") {
      result.no_cache = true;
    } else if (name == "max-age" && equals != std::string_view::npos) {
      auto value = trim(directive.substr(equals + 1));
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
      // An overlong delta is "infinitely" fresh; RFC 9111 caps it at 2^31 seconds.
      if (const auto parsed = parse_integer(value); parsed.has_value()) {
        result.max_age_seconds = std::min<std::int64_t>(*parsed, std::numeric_limits<std::int32_t>::max());
      } else if (!value.empty() && std::all_of(value.begin(), value.end(), [](const char c) { return c >= '0' && c <= '9'; })) {
        result.max_age_seconds = std::numeric_limits<std::int32_t>::max();
      }
    }
    start = end + 1;
  }
  return result;
}

std::int64_t tile_expiry(const TileProvider &provider, const std::int64_t now_unix, const CacheControl &cache_control,
                         const std::optional<std::int64_t> expires_unix) {
  if (cache_control.no_cache || cache_control.no_store) return now_unix;
  if (cache_control.max_age_seconds.has_value()) return now_unix + *cache_control.max_age_seconds;
  if (expires_unix.has_value()) return *expires_unix;
  return now_unix + provider.default_ttl_seconds;
}

bool looks_like_tile_image(const std::span<const std::uint8_t> bytes) {
  const auto starts_with = [&bytes](std::initializer_list<std::uint8_t> signature, const std::size_t offset = 0) {
    return bytes.size() >= offset + signature.size() && std::equal(signature.begin(), signature.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
  };
  return starts_with({0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}) || starts_with({0xff, 0xd8, 0xff}) ||
         (starts_with({'R', 'I', 'F', 'F'}) && starts_with({'W', 'E', 'B', 'P'}, 8));
}

} // namespace gb::geo
