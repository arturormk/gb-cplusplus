add_executable(gb_container_tests unit/iso_bmff_test.cpp)
target_link_libraries(gb_container_tests PRIVATE gb::container gb_project_options)

add_executable(gb_image_tests unit/jpeg_test.cpp)
target_link_libraries(gb_image_tests PRIVATE gb::image gb_project_options)

add_executable(gb_photo_tests unit/photo_test.cpp)
target_link_libraries(gb_photo_tests PRIVATE gb::image gb_project_options)

add_executable(gb_exif_writer_tests unit/exif_writer_test.cpp)
target_link_libraries(gb_exif_writer_tests PRIVATE gb::image gb_project_options)

add_executable(gb_metadata_tests unit/gpmf_test.cpp)
target_link_libraries(gb_metadata_tests PRIVATE gb::metadata gb_project_options)

add_executable(gb_test_fixture_writer fixture_writer.cpp)
target_link_libraries(gb_test_fixture_writer PRIVATE gb_project_options)

add_executable(gb_telemetry_tests unit/telemetry_test.cpp)
target_link_libraries(gb_telemetry_tests PRIVATE gb::telemetry gb_project_options)

add_executable(gb_media_tests unit/media_test.cpp)
target_link_libraries(gb_media_tests PRIVATE gb::media gb_project_options)

add_executable(gb_chapters_tests unit/chapters_test.cpp)
target_link_libraries(gb_chapters_tests PRIVATE gb::media gb_project_options)

add_executable(gb_position_tests unit/position_test.cpp)
target_link_libraries(gb_position_tests PRIVATE gb::media gb_project_options)

add_executable(gb_playback_tests unit/playback_test.cpp)
target_link_libraries(gb_playback_tests PRIVATE gb::media gb_project_options)

add_executable(gb_export_tests unit/export_test.cpp)
target_link_libraries(gb_export_tests PRIVATE gb::exporting gb_project_options)

find_package(Qt6 6.2 REQUIRED COMPONENTS Gui)
add_executable(gb_render_tests unit/render_test.cpp)
target_link_libraries(gb_render_tests PRIVATE gb::render Qt6::Gui gb_project_options)

add_executable(gb_geo_tests unit/geo_test.cpp)
target_link_libraries(gb_geo_tests PRIVATE gb::geo gb_project_options)

add_executable(gb_tiles_tests unit/tiles_test.cpp)
target_link_libraries(gb_tiles_tests PRIVATE gb::tiles gb_project_options)

add_test(NAME gb_container_tests COMMAND gb_container_tests)
add_test(NAME gb_geo_tests COMMAND gb_geo_tests)
add_test(NAME gb_export_tests COMMAND gb_export_tests)
# Needs an OpenGL 3.3 / ES 3.0 context (a GPU or Mesa llvmpipe); reported as skipped without one.
add_test(NAME gb_render_tests COMMAND gb_render_tests)
set_tests_properties(gb_render_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  SKIP_RETURN_CODE 77
  TIMEOUT 60)
add_test(NAME gb_tiles_tests COMMAND gb_tiles_tests)
set_tests_properties(gb_tiles_tests PROPERTIES TIMEOUT 60)
add_test(NAME gb_image_tests COMMAND gb_image_tests)
add_test(NAME gb_photo_tests COMMAND gb_photo_tests)
add_test(NAME gb_exif_writer_tests COMMAND gb_exif_writer_tests)
add_test(NAME gb_metadata_tests COMMAND gb_metadata_tests)
add_test(NAME gb_media_tests COMMAND gb_media_tests)
add_test(NAME gb_chapters_tests COMMAND gb_chapters_tests)
add_test(NAME gb_position_tests COMMAND gb_position_tests)
add_test(NAME gb_inspect_help COMMAND gb_inspect --help)
add_test(NAME gb_inspect_version COMMAND gb_inspect --version)
add_test(NAME gb_metadata_help COMMAND gb_metadata_tool --help)
add_test(NAME gb_metadata_version COMMAND gb_metadata_tool --version)
add_test(NAME gb_telemetry_help COMMAND gb_telemetry_tool --help)
add_test(NAME gb_telemetry_version COMMAND gb_telemetry_tool --version)
add_test(NAME gb_time_help COMMAND gb_time_tool --help)
add_test(NAME gb_time_version COMMAND gb_time_tool --version)
add_test(NAME gb_frames_help COMMAND gb_frames --help)
add_test(NAME gb_frames_version COMMAND gb_frames --version)
add_test(NAME gb_project_help COMMAND gb_project --help)
add_test(NAME gb_project_version COMMAND gb_project --version)
add_test(NAME gb_project_qml_smoke COMMAND gb_project --smoke-test)
set_tests_properties(gb_project_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME gb_render_help COMMAND gb_render_tool --help)
add_test(NAME gb_render_version COMMAND gb_render_tool --version)
add_test(NAME gb_player_help COMMAND gb_player --help)
add_test(NAME gb_player_version COMMAND gb_player --version)
add_test(NAME gb_player_qml_smoke COMMAND gb_player --smoke-test)
add_test(NAME gb_player_rejects_unknown_view COMMAND gb_player --view sideways)
set_tests_properties(gb_player_rejects_unknown_view PROPERTIES WILL_FAIL TRUE)
add_test(NAME gb_player_rejects_unknown_map_provider COMMAND gb_player --map-provider nowhere)
set_tests_properties(gb_player_rejects_unknown_map_provider PROPERTIES WILL_FAIL TRUE)
set_tests_properties(gb_player_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Every executable reports the version derived by cmake/GbVersion.cmake (ADR-0010).
string(REPLACE "." "\\." gb_version_regex "${GB_VERSION}")
foreach(gb_tool IN ITEMS inspect metadata telemetry time frames project player render)
  set_tests_properties(gb_${gb_tool}_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^gb-${gb_tool} ${gb_version_regex}\n$")
endforeach()

set(GB_CLI_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/cli-fixtures")
add_test(NAME gb_cli_fixture_setup
  COMMAND gb_test_fixture_writer "${GB_CLI_FIXTURE_DIR}")
set_tests_properties(gb_cli_fixture_setup PROPERTIES FIXTURES_SETUP gb_cli_media)

find_program(GB_FFMPEG_EXECUTABLE ffmpeg REQUIRED)
add_test(NAME gb_frames_fixture_setup
  COMMAND "${GB_FFMPEG_EXECUTABLE}" -v error
    -f lavfi -i color=c=red:s=104x32:r=2:d=1
    -f lavfi -i color=c=blue:s=104x32:r=2:d=1
    -map 0:v -map 1:v -c:v mpeg4 -y "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_fixture_setup PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  FIXTURES_SETUP gb_frames_media)

add_test(NAME gb_playback_fixture_setup
  COMMAND "${GB_FFMPEG_EXECUTABLE}" -v error
    -f lavfi -i color=c=red:s=104x32:r=10:d=2
    -f lavfi -i color=c=blue:s=104x32:r=10:d=2
    -f lavfi -i sine=frequency=440:sample_rate=48000:duration=2
    -map 0:v -map 1:v -map 2:a -c:v mpeg4 -g 5 -bf 0 -c:a aac -y "${GB_CLI_FIXTURE_DIR}/playback.mp4")
set_tests_properties(gb_playback_fixture_setup PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  FIXTURES_SETUP gb_playback_media)

# Mono PCM ramp: sample n holds ((n mod 1000) - 500) / 1000, so content identifies position.
add_test(NAME gb_audio_fixture_setup
  COMMAND "${GB_FFMPEG_EXECUTABLE}" -v error
    -f lavfi -i "aevalsrc=exprs=(mod(n\\,1000)-500)/1000:s=48000:d=2"
    -c:a pcm_s16le -y "${GB_CLI_FIXTURE_DIR}/ramp.wav")
set_tests_properties(gb_audio_fixture_setup PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  FIXTURES_SETUP gb_playback_media)

add_test(NAME gb_playback_tests
  COMMAND gb_playback_tests "${GB_CLI_FIXTURE_DIR}/playback.mp4" "${GB_CLI_FIXTURE_DIR}/ramp.wav")
set_tests_properties(gb_playback_tests PROPERTIES FIXTURES_REQUIRED gb_playback_media)

add_test(NAME gb_audio_gating_fixture_setup
  COMMAND "${GB_FFMPEG_EXECUTABLE}" -v error
    -f lavfi -i testsrc=s=160x90:r=25:d=10
    -f lavfi -i sine=frequency=440:sample_rate=48000:duration=10
    -map 0:v -map 1:a -c:v mpeg4 -g 25 -bf 0 -c:a aac -y "${GB_CLI_FIXTURE_DIR}/audio-gating.mp4")
set_tests_properties(gb_audio_gating_fixture_setup PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  FIXTURES_SETUP gb_audio_gating_media)

# Real playback with SDL's dummy audio device: audio must be heard during stable playback and
# stay silent while scrubbing, during a burst of seeks while playing, and while paused.
add_test(NAME gb_player_audio_gating
  COMMAND gb_player --benchmark 2 --seeks 2 "${GB_CLI_FIXTURE_DIR}/audio-gating.mp4")
set_tests_properties(gb_player_audio_gating PROPERTIES
  FIXTURES_REQUIRED gb_audio_gating_media
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy"
  TIMEOUT 90
  PASS_REGULAR_EXPRESSION "\"gating_ok\": true")

# GUI-thread stall while playing (docs/incidents/2026-10-31-video-lag-after-screen-lock.md): the
# decode worker must keep decoding and dropping stale sets in step with the clock on its own.
add_test(NAME gb_player_gui_stall
  COMMAND gb_player --benchmark 1 --seeks 0 --stall 3 "${GB_CLI_FIXTURE_DIR}/audio-gating.mp4")
set_tests_properties(gb_player_gui_stall PROPERTIES
  FIXTURES_REQUIRED gb_audio_gating_media
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy"
  TIMEOUT 90
  PASS_REGULAR_EXPRESSION "\"stall_ok\": true")

add_test(NAME gb_player_playback_smoke
  COMMAND gb_player --smoke-test --time 1.05 "${GB_CLI_FIXTURE_DIR}/playback.mp4")
set_tests_properties(gb_player_playback_smoke PROPERTIES
  FIXTURES_REQUIRED gb_playback_media
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 60
  PASS_REGULAR_EXPRESSION "gb-player smoke: layout=max_dual_eac streams=2 pts=[1-9][0-9]* telemetry=none audio=[a-z]+ map=none")

# Capture mode on the fixture: enter it, shrink and move the rectangle, resize the window (the
# rectangle must keep its place), capture through the offscreen renderer and print the settings.
add_test(NAME gb_player_capture
  COMMAND gb_player --smoke-test --capture-test "${GB_CLI_FIXTURE_DIR}/capture.png"
    "${GB_CLI_FIXTURE_DIR}/playback.mp4")
set_tests_properties(gb_player_capture PROPERTIES
  FIXTURES_REQUIRED gb_playback_media
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  SKIP_RETURN_CODE 77
  TIMEOUT 60
  PASS_REGULAR_EXPRESSION "gb-player capture: 1920x1080 gps=no .*schema = gb.export/1.*\\[range\\].*start = 00:00:00.000.*\\[view\\].*mode   = rectilinear.*\\[frame\\].*aspect = 16:9.*width  = 1920")

# The same with a right-hand panel. The fixture has no GPS: the panel has telemetry and no map
# scale, and no tile is requested.
add_test(NAME gb_player_capture_panel
  COMMAND gb_player --smoke-test --map-no-network --capture-panel right
    --capture-test "${GB_CLI_FIXTURE_DIR}/capture-panel.jpg" "${GB_CLI_FIXTURE_DIR}/playback.mp4")
set_tests_properties(gb_player_capture_panel PROPERTIES
  FIXTURES_REQUIRED gb_playback_media
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  SKIP_RETURN_CODE 77
  TIMEOUT 60
  PASS_REGULAR_EXPRESSION "gb-player capture: 1920x1080 gps=no .*\\[layout\\].*placement  = right.*\\[units\\].*\\[telemetry\\].*fields = utc, latitude, longitude, altitude, speed"
  FAIL_REGULAR_EXPRESSION "\\[map\\]|map tile")

add_test(NAME gb_frames_list_json
  COMMAND gb_frames --list --json "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_list_json PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  PASS_REGULAR_EXPRESSION
    "gb.frames.streams/v1.*video_streams.*index.*0.*index.*1")

add_test(NAME gb_frames_extract_json
  COMMAND gb_frames --json --stream 1 --time 00:00:00.5 --force
    --output "${GB_CLI_FIXTURE_DIR}/stream-1.png"
    "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_extract_json PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  PASS_REGULAR_EXPRESSION
    "gb.frames/v1.*stream_index.*1.*pixel_format.*rgb24")

add_test(NAME gb_frames_hud_json
  COMMAND gb_frames --json --hud full --stream 0 --time 0.5 --force
    --output "${GB_CLI_FIXTURE_DIR}/stream-0-hud.png"
    "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_hud_json PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  PASS_REGULAR_EXPRESSION
    "gb.frames/v1.*hud.*enabled.*true.*mode.*full.*metadata_status.*no timed GPMF track.*field_count.*0")

add_test(NAME gb_frames_hud_basic_json
  COMMAND gb_frames --json --hud basic --stream 0 --time 0.5 --force
    --output "${GB_CLI_FIXTURE_DIR}/stream-0-basic-hud.png"
    "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_hud_basic_json PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  PASS_REGULAR_EXPRESSION
    "gb.frames/v1.*hud.*enabled.*true.*mode.*basic.*metadata_status.*unavailable.*field_count.*8")

add_test(NAME gb_frames_hud_requires_mode
  COMMAND gb_frames --hud --stream 0 --force
    --output "${GB_CLI_FIXTURE_DIR}/invalid-hud.png"
    "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_hud_requires_mode PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  WILL_FAIL TRUE)

add_test(NAME gb_frames_reconstruct_json
  COMMAND gb_frames --json --reconstruct --time 0.5 --width 64 --height 32 --overlap 8
    --force --output "${GB_CLI_FIXTURE_DIR}/panorama.ppm"
    "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_reconstruct_json PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  PASS_REGULAR_EXPRESSION
    "max_eac_to_equirectangular_cpu_reference.*stream_index.*0.*stream_index.*1.*equirectangular")

add_test(NAME gb_frames_refuses_source_overwrite
  COMMAND gb_frames --force --stream 0
    --output "${GB_CLI_FIXTURE_DIR}/two-video.mp4"
    "${GB_CLI_FIXTURE_DIR}/two-video.mp4")
set_tests_properties(gb_frames_refuses_source_overwrite PROPERTIES
  FIXTURES_REQUIRED gb_frames_media
  WILL_FAIL TRUE)

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

# gb-render: settings, error paths and a short export of a generated recording.
add_test(NAME gb_render_example_settings
  COMMAND gb_render_tool --example-settings --print-settings)
set_tests_properties(gb_render_example_settings PROPERTIES
  PASS_REGULAR_EXPRESSION "schema = gb.export/1.*\\[map\\].*shape   = circle")
add_test(NAME gb_render_settings_file
  COMMAND sh -c "\"$<TARGET_FILE:gb_render_tool>\" --example-settings > \"${GB_CLI_FIXTURE_DIR}/example.ini\" && \"$<TARGET_FILE:gb_render_tool>\" --settings \"${GB_CLI_FIXTURE_DIR}/example.ini\" --set map.width=2km --placement top --print-settings")
set_tests_properties(gb_render_settings_file PROPERTIES
  FIXTURES_REQUIRED gb_cli_media
  PASS_REGULAR_EXPRESSION "placement  = top.*\\[field-speed\\].*width   = 2000m")
add_test(NAME gb_render_rejects_unknown_key
  COMMAND gb_render_tool --set map.widht=2km --print-settings)
set_tests_properties(gb_render_rejects_unknown_key PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[map\\] widht: unknown key")
add_test(NAME gb_render_rejects_unknown_key_status
  COMMAND gb_render_tool --set map.widht=2km --print-settings)
set_tests_properties(gb_render_rejects_unknown_key_status PROPERTIES WILL_FAIL TRUE)

add_test(NAME gb_render_export
  COMMAND gb_render_tool "${GB_CLI_FIXTURE_DIR}/audio-gating.mp4" --start 1 --duration 2
    --width 320 --placement bottom --set telemetry.fields=speed,time --map-width 1km
    --map-no-network --encoder-preset ultrafast --overwrite -o "${GB_CLI_FIXTURE_DIR}/export.mp4")
set_tests_properties(gb_render_export PROPERTIES
  FIXTURES_REQUIRED gb_audio_gating_media
  FIXTURES_SETUP gb_render_output
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 120
  SKIP_REGULAR_EXPRESSION "offscreen rendering is unavailable"
  PASS_REGULAR_EXPRESSION "wrote .*export.mp4 .*50 frames, libx264 320.+180, aac 48000 Hz, mp4")
# The export is read back: 50 frames at 25 fps from 0, two seconds of audio, no source metadata.
add_test(NAME gb_render_export_readback
  COMMAND gb_frames --list --json "${GB_CLI_FIXTURE_DIR}/export.mp4")
set_tests_properties(gb_render_export_readback PROPERTIES
  FIXTURES_REQUIRED gb_render_output
  PASS_REGULAR_EXPRESSION "\"width\": 320.*\"height\": 180")
add_test(NAME gb_render_refuses_existing_output
  COMMAND gb_render_tool "${GB_CLI_FIXTURE_DIR}/audio-gating.mp4" --duration 1
    -o "${GB_CLI_FIXTURE_DIR}/export.mp4")
set_tests_properties(gb_render_refuses_existing_output PROPERTIES
  FIXTURES_REQUIRED gb_render_output
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  PASS_REGULAR_EXPRESSION "exists; use --overwrite")
add_test(NAME gb_render_rejects_bad_range
  COMMAND gb_render_tool "${GB_CLI_FIXTURE_DIR}/audio-gating.mp4" --start c2:00:00:01 --dry-run)
set_tests_properties(gb_render_rejects_bad_range PROPERTIES
  FIXTURES_REQUIRED gb_audio_gating_media
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  PASS_REGULAR_EXPRESSION "chapter 2 does not exist")
