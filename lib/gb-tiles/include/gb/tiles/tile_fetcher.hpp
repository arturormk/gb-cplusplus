#pragma once

#include "gb/geo/tile_cache.hpp"
#include "gb/geo/tiles.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace gb::tiles {

struct FetcherOptions {
  // When false no request is ever made; only cached tiles are shown.
  bool network{true};
  // nullopt keeps tiles in memory only (used by tests and the smoke test).
  std::optional<std::filesystem::path> cache_root;
  // Identifies the application to the tile server. Must name the application, never a library.
  QString user_agent;
  int transfer_timeout_ms{15000};
};

struct FetcherStatistics {
  std::uint64_t cache_fresh{};      // served from disk without a request
  std::uint64_t cache_stale{};      // served from disk, then revalidated
  std::uint64_t network_requests{}; // requests sent
  std::uint64_t downloaded{};       // 200 responses stored
  std::uint64_t not_modified{};     // 304 responses
  std::uint64_t downloaded_bytes{};
  std::uint64_t failures{};
  std::uint64_t cache_write_failures{};
  int queued{};
  int in_flight{};
  bool blocked{};
  QString last_error;
};

// Fetches raster tiles for an interactive view from one provider, through an on-disk cache.
//
// It only fetches what the caller asks for, which must be tiles in the user's current view; it
// never prefetches. It honours Cache-Control, Expires and ETag/Last-Modified: fresh tiles are
// served from disk with no request, expired ones are shown immediately and revalidated with a
// conditional request. At most provider.max_concurrent_requests requests run at once. A 403, 418
// or 429 response stops all network use for the rest of the session.
class TileFetcher final : public QObject {
  Q_OBJECT

public:
  TileFetcher(geo::TileProvider provider, FetcherOptions options, QObject *parent = nullptr);
  ~TileFetcher() override;

  [[nodiscard]] const geo::TileProvider &provider() const { return provider_; }
  [[nodiscard]] bool networkEnabled() const { return options_.network; }
  [[nodiscard]] bool diskCacheEnabled() const { return cache_.has_value(); }
  [[nodiscard]] QString cacheDirectory() const;
  [[nodiscard]] FetcherStatistics statistics() const;

  // Requests tiles in priority order. Each key is answered once, asynchronously, by tileReady or
  // tileUnavailable; keys already pending are not requested again. A stale cached tile is answered
  // with tileReady and may be followed by a second tileReady if the server sends a newer image.
  void request(const std::vector<geo::TileKey> &keys);
  // Drops queued requests for keys not in `wanted` and returns them; they will not be answered.
  // Requests already sent complete and are cached, but are still answered.
  std::vector<geo::TileKey> retain(const std::vector<geo::TileKey> &wanted);

signals:
  void tileReady(int zoom, int x, int y, const QByteArray &data);
  void tileUnavailable(int zoom, int x, int y, const QString &reason);
  void statisticsChanged();

private:
  struct Pending {
    geo::TileKey key;
    bool revalidate{};
    geo::TileMetadata metadata;
  };

  void schedule(Pending pending);
  void pump();
  void finished(QNetworkReply *reply, const Pending &pending);
  void answerReady(geo::TileKey key, const QByteArray &data);
  void answerUnavailable(geo::TileKey key, const QString &reason);
  void block(const QString &reason);

  geo::TileProvider provider_;
  FetcherOptions options_;
  std::optional<geo::TileCache> cache_;
  QNetworkAccessManager *network_{};
  std::deque<Pending> queue_;
  std::map<geo::TileKey, QNetworkReply *> in_flight_;
  // Keys asked for and not yet answered (queued, in flight or answering from disk).
  std::set<geo::TileKey> awaiting_;
  FetcherStatistics statistics_;
};

} // namespace gb::tiles
