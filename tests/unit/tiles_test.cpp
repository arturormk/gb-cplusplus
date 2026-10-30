// Exercises gb::tiles::TileFetcher against a local HTTP server; no external network is used.
#include "gb/geo/tile_cache.hpp"
#include "gb/tiles/tile_fetcher.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

const QByteArray kPng = QByteArray::fromRawData("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);

struct Request {
  QByteArray path;
  std::map<QByteArray, QByteArray> headers;
};

// Minimal HTTP/1.1 server: one request per connection, answered after a short delay so that
// concurrent requests overlap.
class TileServer final : public QObject {
public:
  TileServer() {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = server_.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
    if (!server_.listen(QHostAddress::LocalHost)) throw std::runtime_error("cannot listen");
  }
  [[nodiscard]] quint16 port() const { return server_.serverPort(); }
  std::vector<Request> requests;
  int max_concurrent{};

private:
  void read(QTcpSocket *socket) {
    auto &buffer = buffers_[socket];
    buffer += socket->readAll();
    const auto end = buffer.indexOf("\r\n\r\n");
    if (end < 0) return;
    const auto lines = buffer.left(end).split('\n');
    buffers_.erase(socket);
    Request request;
    request.path = lines.front().split(' ').value(1);
    for (qsizetype index = 1; index < lines.size(); ++index) {
      const auto colon = lines[index].indexOf(':');
      if (colon > 0) request.headers[lines[index].left(colon).trimmed().toLower()] = lines[index].mid(colon + 1).trimmed();
    }
    requests.push_back(request);
    max_concurrent = std::max(max_concurrent, ++open_);
    QTimer::singleShot(40, socket, [this, socket, request] {
      --open_;
      socket->write(respond(request));
      socket->disconnectFromHost();
    });
  }

  static QByteArray respond(const Request &request) {
    const auto reply = [](const QByteArray &status, const QByteArray &headers, const QByteArray &body) {
      return "HTTP/1.1 " + status + "\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) +
             "\r\n" + headers + "\r\n" + body;
    };
    if (request.path == "/9/0/0.png") return reply("200 OK", "Content-Type: text/html\r\n", "<html>error</html>");
    if (request.path == "/10/0/0.png") return reply("429 Too Many Requests", "", "");
    const auto match = request.headers.find("if-none-match");
    if (match != request.headers.end() && match->second == "\"v1\"") {
      return reply("304 Not Modified", "Cache-Control: max-age=7200\r\nETag: \"v1\"\r\n", "");
    }
    return reply("200 OK", "Content-Type: image/png\r\nCache-Control: max-age=3600\r\nETag: \"v1\"\r\n", kPng);
  }

  QTcpServer server_;
  std::map<QTcpSocket *, QByteArray> buffers_;
  int open_{};
};

struct Answers {
  std::vector<gb::geo::TileKey> ready;
  std::vector<std::pair<gb::geo::TileKey, QString>> unavailable;
  [[nodiscard]] std::size_t count() const { return ready.size() + unavailable.size(); }
};

void attach(gb::tiles::TileFetcher &fetcher, Answers &answers) {
  QObject::connect(&fetcher, &gb::tiles::TileFetcher::tileReady, [&answers](int z, int x, int y, const QByteArray &) {
    answers.ready.push_back({z, x, y});
  });
  QObject::connect(&fetcher, &gb::tiles::TileFetcher::tileUnavailable,
                   [&answers](int z, int x, int y, const QString &reason) { answers.unavailable.push_back({{z, x, y}, reason}); });
}

// Runs the event loop until the condition holds or the timeout expires.
bool wait_for(const std::function<bool()> &condition, const int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  return condition();
}

void settle(const int ms) {
  wait_for([] { return false; }, ms);
}

std::vector<gb::geo::TileKey> row(const int zoom, const int y, const int count) {
  std::vector<gb::geo::TileKey> keys;
  for (int x = 0; x < count; ++x) keys.push_back({zoom, x, y});
  return keys;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  const auto root = std::filesystem::temp_directory_path() /
                    ("gb-tiles-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    TileServer server;
    auto provider = gb::geo::openstreetmap_provider();
    provider.id = "local";
    provider.url_template = "http://127.0.0.1:" + std::to_string(server.port()) + "/{z}/{x}/{y}.png";
    gb::tiles::FetcherOptions options{true, root, QStringLiteral("gb-test/1 (unit test)"), 5000};
    const gb::geo::TileCache cache(root);

    // 1. Downloads go through the cache, identify the application and respect the limit.
    {
      gb::tiles::TileFetcher fetcher(provider, options);
      Answers answers;
      attach(fetcher, answers);
      const auto keys = row(3, 1, 6);
      fetcher.request(keys);
      fetcher.request(keys);  // duplicates are ignored while pending
      check(wait_for([&] { return answers.count() >= 6U; }), "six tiles downloaded");
      settle(100);
      check(answers.ready.size() == 6U && answers.unavailable.empty(), "every tile answered once");
      check(server.requests.size() == 6U, "one request per tile");
      check(server.max_concurrent >= 1 && server.max_concurrent <= provider.max_concurrent_requests,
            "concurrency limit respected");
      for (const auto &request : server.requests) {
        check(request.headers.count("user-agent") == 1U && request.headers.at("user-agent") == "gb-test/1 (unit test)",
              "application User-Agent sent");
        check(request.headers.count("cache-control") == 0U && request.headers.count("pragma") == 0U,
              "no cache-busting headers");
        check(request.headers.count("if-none-match") == 0U, "no validators on first download");
      }
      const auto stored = cache.load(provider, {3, 0, 1});
      check(stored.has_value() && stored->metadata.etag == "\"v1\"", "tile and ETag cached");
      const auto now = QDateTime::currentSecsSinceEpoch();
      check(stored.has_value() && stored->metadata.expires_unix > now + 3500 && stored->metadata.expires_unix <= now + 3600,
            "server max-age honoured");
      check(fetcher.statistics().downloaded == 6U && fetcher.statistics().network_requests == 6U, "download statistics");
    }

    // 2. A new session serves fresh tiles from disk without any request.
    {
      gb::tiles::TileFetcher fetcher(provider, options);
      Answers answers;
      attach(fetcher, answers);
      fetcher.request(row(3, 1, 6));
      check(wait_for([&] { return answers.ready.size() >= 6U; }), "fresh tiles served from disk");
      settle(100);
      check(server.requests.size() == 6U && fetcher.statistics().cache_fresh == 6U, "no request for fresh tiles");
    }

    // 3. An expired tile is shown at once and revalidated with a conditional request.
    {
      auto expired = cache.load(provider, {3, 2, 1});
      expired->metadata.expires_unix = 1;
      cache.store_metadata(provider, {3, 2, 1}, expired->metadata);
      gb::tiles::TileFetcher fetcher(provider, options);
      Answers answers;
      attach(fetcher, answers);
      fetcher.request({{3, 2, 1}});
      check(wait_for([&] { return fetcher.statistics().not_modified == 1U; }), "conditional revalidation");
      settle(100);
      check(answers.ready.size() == 1U, "stale tile shown once; 304 does not resend it");
      check(server.requests.size() == 7U && server.requests.back().headers.count("if-none-match") == 1U &&
                server.requests.back().headers.at("if-none-match") == "\"v1\"",
            "If-None-Match sent");
      const auto revalidated = cache.load(provider, {3, 2, 1});
      check(revalidated.has_value() && revalidated->fresh(QDateTime::currentSecsSinceEpoch() + 7000),
            "304 refreshes the expiry from its headers");
    }

    // 4. With the network disabled a missing tile is reported, never requested.
    {
      auto offline = options;
      offline.network = false;
      gb::tiles::TileFetcher fetcher(provider, offline);
      Answers answers;
      attach(fetcher, answers);
      fetcher.request({{4, 0, 0}, {3, 0, 1}});
      check(wait_for([&] { return answers.count() >= 2U; }), "offline answers");
      check(answers.ready.size() == 1U && answers.unavailable.size() == 1U &&
                answers.unavailable.front().second.contains(QStringLiteral("network disabled")),
            "cached tile shown, missing tile unavailable");
      check(server.requests.size() == 7U, "no request while offline");
    }

    // 5. Error pages are never cached; 6. refusal stops all further requests.
    {
      gb::tiles::TileFetcher fetcher(provider, options);
      Answers answers;
      attach(fetcher, answers);
      fetcher.request({{9, 0, 0}});
      check(wait_for([&] { return answers.count() >= 1U; }), "HTML response answered");
      check(answers.unavailable.size() == 1U && answers.unavailable.front().second.contains(QStringLiteral("not a tile")),
            "HTML response rejected");
      check(!cache.load(provider, {9, 0, 0}).has_value(), "HTML response not cached");

      fetcher.request({{10, 0, 0}});
      check(wait_for([&] { return answers.count() >= 2U; }), "429 answered");
      check(fetcher.statistics().blocked, "429 blocks the session");
      const auto before = server.requests.size();
      fetcher.request({{11, 0, 0}});
      check(wait_for([&] { return answers.count() >= 3U; }), "blocked request answered");
      settle(100);
      check(server.requests.size() == before, "no request after refusal");
    }

    // 7. Tiles that leave the view are dropped from the queue and never answered.
    {
      gb::tiles::TileFetcher fetcher(provider, options);
      Answers answers;
      attach(fetcher, answers);
      const auto keys = row(5, 3, 8);
      fetcher.request(keys);
      const auto dropped = fetcher.retain({keys.front()});
      check(dropped.size() == 6U, "queued tiles outside the view are dropped");
      check(wait_for([&] { return answers.count() >= 2U; }), "in-flight tiles still answered");
      settle(200);
      check(answers.count() == 2U && fetcher.statistics().queued == 0, "dropped tiles are not answered");
    }
  } catch (const std::exception &error) {
    std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
    ++failures;
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  if (failures > 0) return 1;
  std::cout << "gb_tiles_tests: all checks passed\n";
  return 0;
}
