add_executable(gb_frames main.cpp)
set_target_properties(gb_frames PROPERTIES OUTPUT_NAME gb-frames)
target_link_libraries(gb_frames PRIVATE gb::media gb_project_options)
target_compile_definitions(gb_frames PRIVATE GB_FRAMES_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_frames RUNTIME DESTINATION bin)
