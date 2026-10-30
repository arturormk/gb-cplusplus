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
  main.cpp
  playback_engine.cpp
  playback_engine.hpp
  player_controller.cpp
  player_controller.hpp
  process_usage.cpp
  process_usage.hpp
  video_view.cpp
  video_view.hpp)
set_target_properties(gb_player PROPERTIES
  OUTPUT_NAME gb-player
  AUTOMOC ON)

qt_add_resources(gb_player "gb_player_resources"
  PREFIX "/"
  FILES
    qml/DarkButton.qml
    qml/DarkComboBox.qml
    qml/InfoTable.qml
    qml/Main.qml
    qml/Timeline.qml
    shaders/player.frag
    shaders/player.vert)

target_link_libraries(gb_player PRIVATE
  gb::media gb::telemetry gb::container gb::metadata gb_project_options
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets PkgConfig::GB_SDL2)
target_compile_definitions(gb_player PRIVATE
  GB_PLAYER_VERSION="${PROJECT_VERSION}"
  GB_PLAYER_DISPLAY_NAME="${GB_PLAYER_DISPLAY_NAME}")

install(TARGETS gb_player RUNTIME DESTINATION bin)
