add_library(gb_container
  src/iso_bmff.cpp
)

add_library(gb::container ALIAS gb_container)

target_include_directories(gb_container
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(gb_container PUBLIC gb_project_options)
