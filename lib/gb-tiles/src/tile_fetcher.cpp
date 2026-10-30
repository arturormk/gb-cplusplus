#include "gb/tiles/tile_fetcher.hpp"

#include <QDateTime>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <exception>
#include <span>
#include <utility>

namespace gb::tiles {

namespace {

std::int64_t now_unix() { return QDateTime::currentSecsSinceEpoch(); }

// HTTP-date in the preferred IMF-fixdate form, e.g. "Sun, 06 Nov 1994 08:49:37 GMT".
std::optional<std::int64_t> parse_http_date(const QByteArray &value) {
  const auto text = QString::fromLatin1(value).trimmed();
  if (text.isEmpty()) return std::nullopt;
  const auto parsed = QLocale::c().toDateTime(text, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
  if (!parsed.isValid()) return std::nullopt;
  return QDateTime(parsed.date(), parsed.time(), Qt::UTC).toSecsSinceEpoch();
}

std::span<const std::uint8_t> bytes_of(const QByteArray &data) {
  return {reinterpret_cast<const std::uint8_t *>(data.constData()), static_cast<std::size_t>(data.size())};
}

} // namespace

TileFetcher::TileFetcher(geo::TileProvider provider, FetcherOptions options, QObject *parent)
    : QObject(parent), provider_(std::move(provider)), options_(std::move(options)) {
  geo::validate(provider_);
  if (options_.user_agent.trimmed().isEmpty()) {
    throw std::invalid_argument("a tile fetcher needs an application User-Agent");
  }
  if (options_.cache_root.has_value()) cache_.emplace(*options_.cache_root);
  network_ = new QNetworkAccessManager(this);
  // Redirects to plain HTTP are refused; the provider's own cache rules are applied, not Qt's.
  network_->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

TileFetcher::~TileFetcher() {
  for (auto &[key, reply] : in_flight_) {
    reply->disconnect(this);
    reply->abort();
  }
}

QString TileFetcher::cacheDirectory() const {
  return cache_.has_value() ? QString::fromStdString((cache_->root() / provider_.id).string()) : QString();
}

FetcherStatistics TileFetcher::statistics() const {
  auto result = statistics_;
  result.queued = static_cast<int>(queue_.size());
  result.in_flight = static_cast<int>(in_flight_.size());
  return result;
}

void TileFetcher::answerReady(const geo::TileKey key, const QByteArray &data) {
  QMetaObject::invokeMethod(
      this,
      [this, key, data] {
        awaiting_.erase(key);
        emit tileReady(key.zoom, key.x, key.y, data);
      },
      Qt::QueuedConnection);
}

void TileFetcher::answerUnavailable(const geo::TileKey key, const QString &reason) {
  QMetaObject::invokeMethod(
      this,
      [this, key, reason] {
        awaiting_.erase(key);
        emit tileUnavailable(key.zoom, key.x, key.y, reason);
      },
      Qt::QueuedConnection);
}

void TileFetcher::request(const std::vector<geo::TileKey> &keys) {
  for (const auto key : keys) {
    if (awaiting_.contains(key)) continue;
    if (!geo::is_valid(key) || key.zoom < provider_.min_zoom || key.zoom > provider_.max_zoom) {
      awaiting_.insert(key);
      answerUnavailable(key, QStringLiteral("outside the provider's zoom range"));
      continue;
    }
    awaiting_.insert(key);
    std::optional<geo::CachedTile> cached;
    if (cache_.has_value()) {
      try {
        cached = cache_->load(provider_, key);
      } catch (const std::exception &) {
        cached.reset();
      }
    }
    const auto queued = std::any_of(queue_.begin(), queue_.end(), [key](const auto &item) { return item.key == key; });
    if (cached.has_value() && geo::looks_like_tile_image(cached->bytes)) {
      const QByteArray data(reinterpret_cast<const char *>(cached->bytes.data()), static_cast<qsizetype>(cached->bytes.size()));
      answerReady(key, data);
      if (cached->fresh(now_unix())) {
        ++statistics_.cache_fresh;
      } else {
        ++statistics_.cache_stale;
        if (options_.network && !statistics_.blocked && !queued && !in_flight_.contains(key)) {
          queue_.push_back({key, true, cached->metadata});
        }
      }
      continue;
    }
    if (in_flight_.contains(key) || queued) continue;  // answered when the download completes
    if (!options_.network) {
      answerUnavailable(key, QStringLiteral("not cached; network disabled"));
    } else if (statistics_.blocked) {
      answerUnavailable(key, QStringLiteral("not cached; tile server refused service: ") + statistics_.last_error);
    } else {
      queue_.push_back({key, false, {}});
    }
  }
  pump();
  emit statisticsChanged();
}

std::vector<geo::TileKey> TileFetcher::retain(const std::vector<geo::TileKey> &wanted) {
  const std::set<geo::TileKey> keep(wanted.begin(), wanted.end());
  std::vector<geo::TileKey> dropped;
  std::erase_if(queue_, [&](const Pending &pending) {
    if (keep.contains(pending.key)) return false;
    if (!pending.revalidate && awaiting_.erase(pending.key) > 0U) dropped.push_back(pending.key);
    return true;
  });
  if (!dropped.empty()) emit statisticsChanged();
  return dropped;
}

void TileFetcher::pump() {
  while (!queue_.empty() && !statistics_.blocked &&
         static_cast<int>(in_flight_.size()) < provider_.max_concurrent_requests) {
    auto pending = std::move(queue_.front());
    queue_.pop_front();
    QNetworkRequest request(QUrl(QString::fromStdString(geo::tile_url(provider_, pending.key))));
    request.setHeader(QNetworkRequest::UserAgentHeader, options_.user_agent);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    // No Qt cache is installed and no CacheLoadControl is set: AlwaysNetwork would add
    // "Cache-Control: no-cache", which tile policies forbid. Freshness is handled by TileCache.
    request.setTransferTimeout(options_.transfer_timeout_ms);
    if (pending.revalidate) {
      if (!pending.metadata.etag.empty()) {
        request.setRawHeader("If-None-Match", QByteArray::fromStdString(pending.metadata.etag));
      }
      if (!pending.metadata.last_modified.empty()) {
        request.setRawHeader("If-Modified-Since", QByteArray::fromStdString(pending.metadata.last_modified));
      }
    }
    auto *reply = network_->get(request);
    in_flight_[pending.key] = reply;
    ++statistics_.network_requests;
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending] { finished(reply, pending); });
  }
}

void TileFetcher::block(const QString &reason) {
  statistics_.blocked = true;
  statistics_.last_error = reason;
  for (const auto &pending : queue_) {
    if (!pending.revalidate && awaiting_.contains(pending.key)) {
      answerUnavailable(pending.key, QStringLiteral("not cached; tile server refused service: ") + reason);
    }
  }
  queue_.clear();
}

void TileFetcher::finished(QNetworkReply *reply, const Pending &pending) {
  reply->deleteLater();
  in_flight_.erase(pending.key);
  const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto key = pending.key;
  const auto waiting = awaiting_.contains(key);
  const auto fail = [&](const QString &reason) {
    ++statistics_.failures;
    statistics_.last_error = reason;
    if (waiting) answerUnavailable(key, reason);
  };

  const auto metadata_from_headers = [&](geo::TileMetadata metadata) {
    const auto now = now_unix();
    metadata.fetched_unix = now;
    metadata.expires_unix = geo::tile_expiry(provider_, now,
                                             geo::parse_cache_control(reply->rawHeader("Cache-Control").toStdString()),
                                             parse_http_date(reply->rawHeader("Expires")));
    if (const auto etag = reply->rawHeader("ETag"); !etag.isEmpty()) metadata.etag = etag.toStdString();
    if (const auto modified = reply->rawHeader("Last-Modified"); !modified.isEmpty()) {
      metadata.last_modified = modified.toStdString();
    }
    return metadata;
  };

  if (status == 304 && pending.revalidate) {
    ++statistics_.not_modified;
    if (cache_.has_value()) {
      try {
        cache_->store_metadata(provider_, key, metadata_from_headers(pending.metadata));
      } catch (const std::exception &error) {
        ++statistics_.cache_write_failures;
        statistics_.last_error = QString::fromUtf8(error.what());
      }
    }
  } else if (status == 403 || status == 418 || status == 429) {
    ++statistics_.failures;
    block(QStringLiteral("HTTP %1 from %2").arg(status).arg(reply->url().host()));
    if (waiting) answerUnavailable(key, QStringLiteral("not cached; tile server refused service: ") + statistics_.last_error);
  } else if (reply->error() != QNetworkReply::NoError) {
    fail(status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
  } else if (status != 200) {
    fail(QStringLiteral("unexpected HTTP %1").arg(status));
  } else {
    const auto data = reply->read(static_cast<qint64>(geo::kMaximumTileBytes) + 1);
    if (data.size() > static_cast<qsizetype>(geo::kMaximumTileBytes) || !geo::looks_like_tile_image(bytes_of(data))) {
      fail(QStringLiteral("response is not a tile image"));
    } else {
      ++statistics_.downloaded;
      statistics_.downloaded_bytes += static_cast<std::uint64_t>(data.size());
      const auto cache_control = geo::parse_cache_control(reply->rawHeader("Cache-Control").toStdString());
      if (cache_.has_value() && !cache_control.no_store) {
        try {
          cache_->store(provider_, key, bytes_of(data), metadata_from_headers({}));
        } catch (const std::exception &error) {
          ++statistics_.cache_write_failures;
          statistics_.last_error = QString::fromUtf8(error.what());
        }
      }
      if (waiting || pending.revalidate) {
        awaiting_.insert(key);
        answerReady(key, data);
      }
    }
  }
  pump();
  emit statisticsChanged();
}

} // namespace gb::tiles
