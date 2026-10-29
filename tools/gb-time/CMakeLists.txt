add_executable(gb_time_tool main.cpp)
set_target_properties(gb_time_tool PROPERTIES OUTPUT_NAME gb-time)
target_link_libraries(gb_time_tool PRIVATE gb::container gb::telemetry gb_project_options)
target_compile_definitions(gb_time_tool PRIVATE GB_TIME_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_time_tool RUNTIME DESTINATION bin)
