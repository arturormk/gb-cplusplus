add_library(gb_telemetry
  src/export.cpp
  src/telemetry.cpp
)

add_library(gb::telemetry ALIAS gb_telemetry)

target_include_directories(gb_telemetry
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(gb_telemetry PUBLIC gb::container gb::metadata gb_project_options)
