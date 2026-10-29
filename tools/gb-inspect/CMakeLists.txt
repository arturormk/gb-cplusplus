add_executable(gb_inspect main.cpp)
set_target_properties(gb_inspect PROPERTIES OUTPUT_NAME gb-inspect)
target_link_libraries(gb_inspect PRIVATE gb::container gb_project_options)

install(TARGETS gb_inspect RUNTIME DESTINATION bin)
