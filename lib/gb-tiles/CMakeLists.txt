find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network)

# Qt Network tile fetching over the gb_geo cache. Kept apart from gb_geo so the projection, cache
# and route logic stay dependency-free; used by gb-player and, later, gb-map-render.
add_library(gb_tiles
  include/gb/tiles/tile_fetcher.hpp
  src/tile_fetcher.cpp)
add_library(gb::tiles ALIAS gb_tiles)
set_target_properties(gb_tiles PROPERTIES AUTOMOC ON)

target_include_directories(gb_tiles PUBLIC include)
target_link_libraries(gb_tiles PUBLIC gb::geo gb_project_options Qt6::Core Qt6::Network)
