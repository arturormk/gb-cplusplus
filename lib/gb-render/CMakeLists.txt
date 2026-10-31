# The renderer gb-player and exports share (docs/plans/export/pipeline.md): the player's shader,
# offscreen rendering and the still writer. Qt GUI only, no Qt Quick.
find_package(Qt6 6.2 REQUIRED COMPONENTS Gui)

# The shader sources are compiled into the library, so offscreen users need no Qt resources.
set(gb_render_shader_header "${CMAKE_CURRENT_BINARY_DIR}/generated/gb_render_shaders.hpp")
file(READ shaders/frame.vert GB_FRAME_VERTEX_SHADER)
file(READ shaders/frame.frag GB_FRAME_FRAGMENT_SHADER)
configure_file(src/gb_render_shaders.hpp.in "${gb_render_shader_header}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  shaders/frame.vert shaders/frame.frag)

add_library(gb_render
  src/frame_renderer.cpp
  src/map_painter.cpp
  src/panel_painter.cpp
  src/still.cpp
  "${gb_render_shader_header}")
add_library(gb::render ALIAS gb_render)

target_include_directories(gb_render
  PUBLIC include
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(gb_render
  PUBLIC gb::media gb::image gb::exporting gb::geo Qt6::Gui gb_project_options)
