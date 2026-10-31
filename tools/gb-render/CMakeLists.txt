find_package(Qt6 6.2 REQUIRED COMPONENTS Gui)

# The target is gb_render_tool: gb_render is the library it shares with gb-player.
add_executable(gb_render_tool main.cpp)
set_target_properties(gb_render_tool PROPERTIES OUTPUT_NAME gb-render)
target_link_libraries(gb_render_tool PRIVATE
  gb::render gb::tiles gb::media gb::telemetry gb::exporting Qt6::Gui gb_project_options)
target_compile_definitions(gb_render_tool PRIVATE GB_RENDER_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_render_tool RUNTIME DESTINATION bin)
