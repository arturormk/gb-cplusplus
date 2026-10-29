add_executable(gb_container_tests unit/iso_bmff_test.cpp)
target_link_libraries(gb_container_tests PRIVATE gb::container gb_project_options)

add_executable(gb_test_fixture_writer fixture_writer.cpp)
target_link_libraries(gb_test_fixture_writer PRIVATE gb_project_options)

add_test(NAME gb_container_tests COMMAND gb_container_tests)
add_test(NAME gb_inspect_help COMMAND gb_inspect --help)
add_test(NAME gb_inspect_version COMMAND gb_inspect --version)

set(GB_CLI_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures")
add_test(NAME gb_cli_fixture_setup
  COMMAND gb_test_fixture_writer "${GB_CLI_FIXTURE_DIR}")
set_tests_properties(gb_cli_fixture_setup PROPERTIES FIXTURES_SETUP gb_cli_media)

add_test(NAME gb_inspect_json
  COMMAND gb_inspect --json "${GB_CLI_FIXTURE_DIR}/valid.mp4")
set_tests_properties(gb_inspect_json PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "\"schema\": \"gb.inspect/v1\".*\"type\": \"ftyp\"")

add_test(NAME gb_inspect_corrupt
  COMMAND gb_inspect "${GB_CLI_FIXTURE_DIR}/corrupt.mp4")
set_tests_properties(gb_inspect_corrupt PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  WILL_FAIL TRUE)
