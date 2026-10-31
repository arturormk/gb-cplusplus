#pragma once

#include "gb/geo/tiles.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gb::geo {

// HTTP validators and freshness recorded beside each cached tile.
struct TileMetadata {
  std::int64_t fetched_unix{};
  std::int64_t expires_unix{};
  std::string etag;
  std::string last_modified;
};

struct CachedTile {
  std::vector<std::uint8_t> bytes;
  TileMetadata metadata;
  [[nodiscard]] bool fresh(std::int64_t now_unix) const { return now_unix < metadata.expires_unix; }
};

inline constexpr std::size_t kMaximumTileBytes = 4U * 1024U * 1024U;

// $XDG_CACHE_HOME/gb/tiles when XDG_CACHE_HOME is absolute, otherwise $HOME/.cache/gb/tiles.
// Returns nullopt when neither is available.
[[nodiscard]] std::optional<std::filesystem::path> default_tile_cache_root();

// On-disk tile cache: <root>/<provider id>/<z>/<x>/<y>.<extension> with a ".meta" sidecar holding
// TileMetadata. Writes go to a temporary file that is renamed into place, so a concurrent reader
// sees either the old or the new tile. The cache has no size limit; it may be deleted at any time.
class TileCache {
public:
  explicit TileCache(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path &root() const { return root_; }
  [[nodiscard]] std::filesystem::path tile_path(const TileProvider &provider, TileKey key) const;
  // Returns nullopt for a missing, oversized or unreadable tile. A tile without a readable sidecar
  // is treated as fetched at its modification time and kept for the provider's default lifetime.
  [[nodiscard]] std::optional<CachedTile> load(const TileProvider &provider, TileKey key) const;
  // Throws std::runtime_error when the tile cannot be written.
  void store(const TileProvider &provider, TileKey key, std::span<const std::uint8_t> bytes,
             const TileMetadata &metadata) const;
  void store_metadata(const TileProvider &provider, TileKey key,
                      const TileMetadata &metadata) const;

private:
  std::filesystem::path root_;
};

// Parsed freshness directives from a Cache-Control header.
struct CacheControl {
  std::optional<std::int64_t> max_age_seconds;
  bool no_store{};
  bool no_cache{};
};
[[nodiscard]] CacheControl parse_cache_control(std::string_view header);

// Expiry from response headers: Cache-Control max-age, then Expires, then the provider default.
// no-cache and no-store make the tile immediately stale, so it is revalidated before reuse.
[[nodiscard]] std::int64_t tile_expiry(const TileProvider &provider, std::int64_t now_unix,
                                       const CacheControl &cache_control,
                                       std::optional<std::int64_t> expires_unix);

// Checks the signature of a PNG, JPEG or WebP payload so error pages are never cached as tiles.
[[nodiscard]] bool looks_like_tile_image(std::span<const std::uint8_t> bytes);

} // namespace gb::geo
