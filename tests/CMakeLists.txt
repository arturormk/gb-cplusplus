add_executable(gb_container_tests unit/iso_bmff_test.cpp)
target_link_libraries(gb_container_tests PRIVATE gb::container gb_project_options)

add_executable(gb_image_tests unit/jpeg_test.cpp)
target_link_libraries(gb_image_tests PRIVATE gb::image gb_project_options)

add_executable(gb_metadata_tests unit/gpmf_test.cpp)
target_link_libraries(gb_metadata_tests PRIVATE gb::metadata gb_project_options)

add_executable(gb_test_fixture_writer fixture_writer.cpp)
target_link_libraries(gb_test_fixture_writer PRIVATE gb_project_options)

add_test(NAME gb_container_tests COMMAND gb_container_tests)
add_test(NAME gb_image_tests COMMAND gb_image_tests)
add_test(NAME gb_metadata_tests COMMAND gb_metadata_tests)
add_test(NAME gb_inspect_help COMMAND gb_inspect --help)
add_test(NAME gb_inspect_version COMMAND gb_inspect --version)
add_test(NAME gb_metadata_help COMMAND gb_metadata_tool --help)
add_test(NAME gb_metadata_version COMMAND gb_metadata_tool --version)

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

add_test(NAME gb_inspect_jpeg_json
  COMMAND gb_inspect --json "${GB_CLI_FIXTURE_DIR}/valid-image.bin")
set_tests_properties(gb_inspect_jpeg_json PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "\"schema\": \"gb.inspect.jpeg/v1\".*\"image_count\": 1")

add_test(NAME gb_inspect_jpeg_corrupt
  COMMAND gb_inspect "${GB_CLI_FIXTURE_DIR}/corrupt-image.bin")
set_tests_properties(gb_inspect_jpeg_corrupt PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  WILL_FAIL TRUE)

add_test(NAME gb_metadata_json
  COMMAND gb_metadata_tool --json "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_metadata_json PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "\"schema\": \"gb.metadata/v1\".*\"key\": \"ZZZZ\".*\"known\": false")

add_test(NAME gb_metadata_sample_json
  COMMAND gb_metadata_tool --json --sample 0 "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_metadata_sample_json PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "\"schema\": \"gb.metadata.sample/v1\".*\"normalized_values\": .\"1\", \"2\", \"3\", \"4\", \"5\", \"6\".")

add_test(NAME gb_metadata_sample_text
  COMMAND gb_metadata_tool --sample 0 --key GPS5 "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_metadata_sample_text PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "GPS5 .*GPS position and speed.*latitude=40.1234567 deg, longitude=-3.1234567 deg, altitude=721.304 m")

add_test(NAME gb_metadata_unknown_text
  COMMAND gb_metadata_tool --sample 0 --key ZZZZ "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_metadata_unknown_text PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "ZZZZ .*UNKNOWN KEY: storage decoded, semantics unknown.*raw values: .170, 187..*raw bytes: aabb")

add_test(NAME gb_metadata_values_text
  COMMAND gb_metadata_tool --values --key GPS5 "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_metadata_values_text PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "sample.0. decode_time_ticks=0.*latitude=40.1234567 deg")
