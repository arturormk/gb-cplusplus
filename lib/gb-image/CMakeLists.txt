add_library(gb_image
  src/jpeg.cpp
)

add_library(gb::image ALIAS gb_image)

target_include_directories(gb_image
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(gb_image PUBLIC gb_project_options)
