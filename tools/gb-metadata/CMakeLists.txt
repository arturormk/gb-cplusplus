add_executable(gb_metadata_tool main.cpp)
set_target_properties(gb_metadata_tool PROPERTIES OUTPUT_NAME gb-metadata)
target_link_libraries(gb_metadata_tool PRIVATE gb::container gb::metadata gb_project_options)
target_compile_definitions(gb_metadata_tool PRIVATE GB_METADATA_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_metadata_tool RUNTIME DESTINATION bin)
