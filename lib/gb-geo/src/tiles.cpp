#include "gb/geo/tiles.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gb::geo {

bool is_valid(const TileKey key) {
  if (key.zoom < 0 || key.zoom > kMaximumTileZoom)
    return false;
  const auto count = std::int64_t{1} << key.zoom;
  return key.x >= 0 && key.y >= 0 && key.x < count && key.y < count;
}

TileKey tile_containing(const WorldPoint point, const int zoom) {
  const auto level = std::clamp(zoom, 0, kMaximumTileZoom);
  const auto count = std::int64_t{1} << level;
  const auto index = [count](const double value) {
    const auto scaled = static_cast<std::int64_t>(
        std::floor(std::clamp(value, 0.0, 1.0) * static_cast<double>(count)));
    return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, count - 1));
  };
  return {level, index(point.x), index(point.y)};
}

const TileProvider &openstreetmap_provider() {
  static const TileProvider provider{
      .id = "osm",
      .name = "OpenStreetMap standard tile layer",
      // The policy asks for exactly this URL, without subdomains and never over plain HTTP.
      .url_template = "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
      .subdomains = {},
      .min_zoom = 0,
      .max_zoom = 19,
      .tile_size = 256,
      .file_extension = "png",
      .attribution = "© OpenStreetMap contributors",
      .attribution_url = "https://www.openstreetmap.org/copyright",
      .default_ttl_seconds = 7 * 24 * 3600,
      .max_concurrent_requests = 2,
      .bulk_download_allowed = false};
  return provider;
}

const std::vector<TileProvider> &builtin_providers() {
  static const std::vector<TileProvider> providers{openstreetmap_provider()};
  return providers;
}

const TileProvider *find_provider(const std::string_view id) {
  for (const auto &provider : builtin_providers()) {
    if (provider.id == id)
      return &provider;
  }
  return nullptr;
}

void validate(const TileProvider &provider) {
  const auto fail = [&provider](const std::string &message) {
    throw std::invalid_argument("tile provider '" + provider.id + "': " + message);
  };
  if (provider.id.empty() || provider.id.size() > 64 ||
      !std::all_of(provider.id.begin(), provider.id.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      })) {
    fail("id must be 1-64 characters of [a-z0-9_-]");
  }
  const auto &url = provider.url_template;
  if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
    fail("URL must be http(s)");
  for (const auto *field : {"{z}", "{x}", "{y}"}) {
    if (url.find(field) == std::string::npos)
      fail(std::string("URL template lacks ") + field);
  }
  if (url.find("{s}") != std::string::npos && provider.subdomains.empty())
    fail("{s} needs subdomains");
  if (provider.min_zoom < 0 || provider.max_zoom > kMaximumTileZoom ||
      provider.min_zoom > provider.max_zoom) {
    fail("invalid zoom range");
  }
  if (provider.tile_size < 64 || provider.tile_size > 1024)
    fail("tile size must be 64..1024");
  if (provider.file_extension.empty() || provider.file_extension.size() > 8 ||
      !std::all_of(provider.file_extension.begin(), provider.file_extension.end(),
                   [](const char c) { return c >= 'a' && c <= 'z'; })) {
    fail("invalid file extension");
  }
  if (provider.attribution.empty())
    fail("attribution is required");
  if (provider.default_ttl_seconds <= 0)
    fail("default cache lifetime must be positive");
  if (provider.max_concurrent_requests < 1 || provider.max_concurrent_requests > 8) {
    fail("concurrent requests must be 1..8");
  }
}

std::string tile_url(const TileProvider &provider, const TileKey key) {
  if (!is_valid(key) || key.zoom < provider.min_zoom || key.zoom > provider.max_zoom) {
    throw std::invalid_argument("tile outside the provider's range");
  }
  std::string url;
  const auto &pattern = provider.url_template;
  for (std::size_t index = 0; index < pattern.size();) {
    const auto replace = [&](const std::string_view field, const std::string &value) {
      if (pattern.compare(index, field.size(), field) != 0)
        return false;
      url += value;
      index += field.size();
      return true;
    };
    if (replace("{z}", std::to_string(key.zoom)) || replace("{x}", std::to_string(key.x)) ||
        replace("{y}", std::to_string(key.y))) {
      continue;
    }
    if (!provider.subdomains.empty() &&
        replace("{s}", provider.subdomains[static_cast<std::size_t>(key.x + key.y) %
                                           provider.subdomains.size()])) {
      continue;
    }
    url += pattern[index++];
  }
  return url;
}

} // namespace gb::geo
