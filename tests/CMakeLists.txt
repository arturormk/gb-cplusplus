add_executable(gb_container_tests unit/iso_bmff_test.cpp)
target_link_libraries(gb_container_tests PRIVATE gb::container gb_project_options)

add_executable(gb_image_tests unit/jpeg_test.cpp)
target_link_libraries(gb_image_tests PRIVATE gb::image gb_project_options)

add_executable(gb_metadata_tests unit/gpmf_test.cpp)
target_link_libraries(gb_metadata_tests PRIVATE gb::metadata gb_project_options)

add_executable(gb_test_fixture_writer fixture_writer.cpp)
target_link_libraries(gb_test_fixture_writer PRIVATE gb_project_options)

add_executable(gb_telemetry_tests unit/telemetry_test.cpp)
target_link_libraries(gb_telemetry_tests PRIVATE gb::telemetry gb_project_options)

add_test(NAME gb_container_tests COMMAND gb_container_tests)
add_test(NAME gb_image_tests COMMAND gb_image_tests)
add_test(NAME gb_metadata_tests COMMAND gb_metadata_tests)
add_test(NAME gb_inspect_help COMMAND gb_inspect --help)
add_test(NAME gb_inspect_version COMMAND gb_inspect --version)
add_test(NAME gb_metadata_help COMMAND gb_metadata_tool --help)
add_test(NAME gb_metadata_version COMMAND gb_metadata_tool --version)
add_test(NAME gb_telemetry_help COMMAND gb_telemetry_tool --help)
add_test(NAME gb_telemetry_version COMMAND gb_telemetry_tool --version)
add_test(NAME gb_time_help COMMAND gb_time_tool --help)
add_test(NAME gb_time_version COMMAND gb_time_tool --version)

set(GB_CLI_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures")
add_test(NAME gb_cli_fixture_setup
  COMMAND gb_test_fixture_writer "${GB_CLI_FIXTURE_DIR}")
set_tests_properties(gb_cli_fixture_setup PROPERTIES FIXTURES_SETUP gb_cli_media)

add_test(NAME gb_telemetry_tests
  COMMAND gb_telemetry_tests "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_telemetry_tests PROPERTIES FIXTURES_REQUIRED gb_cli_media)

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

add_test(NAME gb_telemetry_json
  COMMAND gb_telemetry_tool --format json "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_telemetry_json PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "\"schema\": \"gb.telemetry/v1\".*\"utc\": \"2026-01-01T12:00:00.000Z\".*\"validity\": \"valid\"")

add_test(NAME gb_telemetry_csv
  COMMAND gb_telemetry_tool --format csv "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_telemetry_csv PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "latitude_deg,longitude_deg.*40.1234567,-3.1234567,721.304")

add_test(NAME gb_telemetry_gpx
  COMMAND gb_telemetry_tool --format gpx "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_telemetry_gpx PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "<gpx.*<trkpt lat=\"40.1234567\" lon=\"-3.1234567\".*<time>2026-01-01T12:00:00.000Z</time>")

add_test(NAME gb_telemetry_geojson
  COMMAND gb_telemetry_tool --format geojson "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_telemetry_geojson PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "gb.telemetry.geojson/v1.*coordinates.*-3.1234567,40.1234567,721.304")

add_test(NAME gb_telemetry_no_gps
  COMMAND gb_telemetry_tool --format json "${GB_CLI_FIXTURE_DIR}/metadata-no-gps.mp4")
set_tests_properties(gb_telemetry_no_gps PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "\"points\": \[\].*\"reason\": \"no_gps_records\".*\"warnings\": \[\"no GPS5 telemetry found\"\]")

add_test(NAME gb_telemetry_invalid_gps
  COMMAND gb_telemetry_tool --format json "${GB_CLI_FIXTURE_DIR}/metadata-invalid-gps.mp4")
set_tests_properties(gb_telemetry_invalid_gps PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "\"validity\": \"invalid\".*\"reason\": \"invalid_gps_fix\"")

add_test(NAME gb_telemetry_invalid_gpx_omitted
  COMMAND gb_telemetry_tool --format gpx "${GB_CLI_FIXTURE_DIR}/metadata-invalid-gps.mp4")
set_tests_properties(gb_telemetry_invalid_gpx_omitted PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  FAIL_REGULAR_EXPRESSION "<trkpt")

add_test(NAME gb_telemetry_record_timing_jitter
  COMMAND gb_telemetry_tool --format json "${GB_CLI_FIXTURE_DIR}/metadata-jitter.mp4")
set_tests_properties(gb_telemetry_record_timing_jitter PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "\"media_time_ticks\": 333, \"timing_uncertainty_ticks\": 334.*\"media_time_ticks\": 666, \"timing_uncertainty_ticks\": 334")

add_test(NAME gb_time_json
  COMMAND gb_time_tool --json --at 500 "${GB_CLI_FIXTURE_DIR}/metadata.mp4")
set_tests_properties(gb_time_json PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "\"schema\": \"gb.time/v1\".*\"anchors\":.*2026-01-01T12:00:00.000Z.*\"requested_ticks\": 500")

add_test(NAME gb_time_composition_offset
  COMMAND gb_time_tool --json --at 100 "${GB_CLI_FIXTURE_DIR}/metadata-ctts.mp4")
set_tests_properties(gb_time_composition_offset PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION
    "\"composition_offset_sample_count\": 1.*\"media_time_ticks\": 100")

add_test(NAME gb_time_no_metadata
  COMMAND gb_time_tool --json "${GB_CLI_FIXTURE_DIR}/valid.mp4")
set_tests_properties(gb_time_no_metadata PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "\"tracks\": \[\].*\"track_id\": null.*\"anchors\": \[\]")
