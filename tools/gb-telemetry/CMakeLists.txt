add_executable(gb_telemetry_tool main.cpp)
set_target_properties(gb_telemetry_tool PROPERTIES OUTPUT_NAME gb-telemetry)
target_link_libraries(gb_telemetry_tool PRIVATE gb::telemetry gb_project_options)
target_compile_definitions(gb_telemetry_tool PRIVATE GB_TELEMETRY_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_telemetry_tool RUNTIME DESTINATION bin)
