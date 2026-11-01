add_library(gb_geo
  src/mercator.cpp
  src/route.cpp
  src/scale_curve.cpp
  src/tile_cache.cpp
  src/tiles.cpp)
add_library(gb::geo ALIAS gb_geo)

target_include_directories(gb_geo PUBLIC include)
target_link_libraries(gb_geo PUBLIC gb_project_options)
