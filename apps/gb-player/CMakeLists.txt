find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick Widgets)
find_package(PkgConfig REQUIRED)
# Audio output only; SDL video, input and signal handling are not used.
pkg_check_modules(GB_SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.10)

set(GB_PLAYER_DISPLAY_NAME "GoBro" CACHE STRING
  "Replaceable user-facing display name for gb-player")

qt_add_executable(gb_player
  audio_output.cpp
  audio_output.hpp
  benchmark.cpp
  benchmark.hpp
  capture_controller.cpp
  capture_controller.hpp
  main.cpp
  map_view.cpp
  map_view.hpp
  panel_preview.cpp
  panel_preview.hpp
  playback_engine.cpp
  playback_engine.hpp
  player_controller.cpp
  player_controller.hpp
  process_usage.cpp
  process_usage.hpp
  profile_view.cpp
  profile_view.hpp
  video_view.cpp
  video_view.hpp)
set_target_properties(gb_player PROPERTIES
  OUTPUT_NAME gb-player
  AUTOMOC ON)

qt_add_resources(gb_player "gb_player_resources"
  PREFIX "/"
  FILES
    qml/CaptureOverlay.qml
    qml/CapturePanel.qml
    qml/DarkButton.qml
    qml/DarkComboBox.qml
    qml/InfoTable.qml
    qml/Main.qml
    qml/MapPanel.qml
    qml/Timeline.qml
    qml/ToggleButton.qml)

target_link_libraries(gb_player PRIVATE
  gb::render gb::exporting gb::media gb::telemetry gb::container gb::image gb::metadata gb::geo gb::tiles gb_project_options
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets PkgConfig::GB_SDL2)
target_compile_definitions(gb_player PRIVATE
  GB_PLAYER_VERSION="${PROJECT_VERSION}"
  GB_PLAYER_DISPLAY_NAME="${GB_PLAYER_DISPLAY_NAME}")

install(TARGETS gb_player RUNTIME DESTINATION bin)
