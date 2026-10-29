add_library(gb_metadata
  src/gpmf.cpp
)

add_library(gb::metadata ALIAS gb_metadata)

target_include_directories(gb_metadata
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(gb_metadata PUBLIC gb_project_options)
