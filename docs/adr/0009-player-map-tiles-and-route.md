# ADR-0009: Player map, tile providers and tile cache

Status: Accepted for M5 (2026-10-30)

## Context

M5 requires a live OpenStreetMap-compatible map in the player, showing the recorded route and the
current position, with click-to-seek. The map must stay aligned with the video and telemetry
during play, pause and random seeks. Missing GPS and ambiguous repeated coordinates must be
handled. The blueprint named MapLibre Native Qt as the first candidate. M6 will need the same
tiles headlessly, in `gb-map-render`.

The supported baseline is Ubuntu 22.04 with Qt 6.2.4. Two candidates are not usable there.
MapLibre Native Qt is not packaged, and its current releases require Qt 6.5 or newer. It would
have to be vendored and built from source. Qt Location returned only in Qt 6.5 and is not
packaged for 6.2. Qt Network is part of `qt6-base-dev`, which is already required.

The [OSM tile usage policy](https://operations.osmfoundation.org/policies/tiles/) (reviewed
2026-10-30) requires:

- exactly `https://tile.openstreetmap.org/{z}/{x}/{y}.png`;
- a stable User-Agent that names the application;
- no `Cache-Control: no-cache` or `Pragma` request headers;
- honouring `Cache-Control`, `Expires` and `ETag`, with 7 days as the minimum when those headers
  cannot be read;
- conditional requests for expired tiles;
- no bulk downloading, prefetching or offline packs; and
- visible "© OpenStreetMap contributors" attribution linking to the copyright page.

## Decision

1. **Own raster slippy map, no new dependency.** The map is a `QQuickPaintedItem` (`MapView`) that
   draws 256-pixel raster tiles, the route and the marker with `QPainter`. It works under both
   the OpenGL and the software scene graph, so the offscreen smoke test covers it.
2. **`gb_geo` (dependency-free C++20)** holds everything that M6 and tests need without Qt:
   - Web Mercator projection, tile keys and haversine distance;
   - the `TileProvider` description;
   - the on-disk `TileCache`;
   - `Cache-Control`/expiry rules and the image signature check; and
   - route runs, bounds and click-to-seek candidates.
3. **Providers are data.** A `TileProvider` records the provider's id, URL template (with
   optional `{s}` subdomains), zoom range, tile size and extension. It also records the provider's
   policy: attribution text and link, default cache lifetime, request concurrency, and whether
   bulk download is allowed. `validate()` rejects unsafe descriptions, for example ids with path
   characters, non-HTTP URLs or missing attribution. The built-in registry contains only `osm`,
   with the policy values above and `bulk_download_allowed = false`. `gb-player --map-provider ID`
   selects from the registry. Adding a provider means adding a description; no code path is
   specific to OSM.
4. **Cache layout and freshness.** Tiles are stored at
   `<root>/<provider>/<z>/<x>/<y>.<ext>`, with a `.meta` sidecar holding the fetch time, expiry,
   ETag and Last-Modified. Writes go through a temporary file that is renamed into place. The
   expiry comes from `Cache-Control: max-age`, then `Expires`, then the provider default (7 days).
   `no-cache` and `no-store` make a tile immediately stale. The default root is
   `$XDG_CACHE_HOME/gb/tiles`, falling back to `~/.cache/gb/tiles`; `--map-cache` overrides it.
5. **`gb_tiles` (Qt Core + Network)** provides `TileFetcher`. It serves fresh tiles from disk with
   no request. It shows stale tiles at once and revalidates them with `If-None-Match` and
   `If-Modified-Since`. It downloads missing tiles, at most `max_concurrent_requests` at a time
   (2 for OSM), and HTTP/2 is allowed. Only images with a PNG, JPEG or WebP signature are cached.
   It never sets `CacheLoadControl`, because Qt's `AlwaysNetwork` adds `Cache-Control: no-cache`.
   A 403, 418 or 429 response stops all network use for the session. The view asks only for the
   tiles it covers, nearest to the centre first. Queued tiles that leave the view are dropped.
   Cached ancestors are scaled up as placeholders; they are never fetched for that purpose.
6. **Network is on by default in `gb-player`.** The user asked for online OpenStreetMap.
   `--map-no-network` shows cached tiles only. Each tile request reveals which area is being
   viewed to the provider, but never the recording or its GPS track. The help text and README say
   so. No request is made for a recording without a valid fix. The smoke test forces network off
   and a memory-only cache. Library and command-line tools make no requests.
7. **User-Agent** is `<application>/<version> (gb-cplusplus[; contact])`, for example
   `gb-player/0.1.N (gb-cplusplus)`. The optional contact comes from the `GB_MAP_CONTACT` CMake
   cache variable, so the product name stays stable. `gb::tiles::application_fetcher_options()`
   builds this User-Agent and resolves the cache root (`--map-cache` or the default), so every
   tool that fetches tiles shares the cache and identifies itself the same way.
8. **Route and marker policy follows the telemetry panel.**
   - The route is one point per `GPS5` record, and only valid fixes are drawn. Each run of valid
     records is a separate polyline, so an invalid stretch is a visible break rather than a
     straight line across missing data. The route is split into travelled and remaining parts at
     the current record.
   - The marker is the record that `gb::telemetry::nearest_point` selects for the displayed
     frame. It is never interpolated. Its arrow is the direction of travel
     (`motion_azimuth_degrees`), not camera heading.
   - When the nearest record is invalid, no marker is drawn and the map says why. Inside a
     reported gap, a hollow dashed ring marks the nearest recorded fix, with its time offset.
9. **Click-to-seek resolves repeated coordinates by pass.** `route_candidates` returns one
   candidate per separate pass of the route within a 10-pixel tolerance (at least 5 m): the
   recorded point of that pass nearest the click. A pass ends when the route moves beyond twice
   the tolerance (hysteresis against GPS jitter) or reaches an invalid fix. One candidate seeks
   directly. Several open a list of times, marking the one nearest to the current playback time.
10. **The gate is measured, not asserted.** `gb-player --benchmark` adds a `map` section to
    `gb.player.benchmark/v1` (an additive field). It compares the marker that `MapView` actually
    painted with the record nearest to the displayed PTS, computed independently. The comparison
    runs every ~100 ms during playback (a lag of up to two frames is tolerated), after the pause
    and after every seek (where an exact match is required). A mismatch fails the run.

## Consequences

- No new package is required.
- The map is raster only: no vector styles, rotation or labels in other languages. On HiDPI
  screens tiles are scaled up and can look soft.
- Painting is CPU-side. During `.360` playback it adds no measurable frame loss on the
  development machine (see the M5 finding).
- The cache has no size limit or eviction. It is safe to delete at any time. Policy-compliant
  pruning is future work.
- Routes that cross the antimeridian are not drawn across it.
- OSM availability is best-effort and access can be withdrawn. A refusal is shown in the map
  status, and cached tiles stay usable.
- M6 export may reuse `gb_geo` and `gb_tiles`. With OSM it must use only tiles already cached
  from interactive viewing, or a provider whose `bulk_download_allowed` is true, and report any
  missing coverage.
- Replacing the renderer with MapLibre later (for vector tiles) would not change the cache,
  provider or route contracts.
