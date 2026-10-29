add_executable(gb_inspect main.cpp)
set_target_properties(gb_inspect PROPERTIES OUTPUT_NAME gb-inspect)
target_link_libraries(gb_inspect PRIVATE gb::container gb::image gb_project_options)
target_compile_definitions(gb_inspect PRIVATE GB_INSPECT_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_inspect RUNTIME DESTINATION bin)
