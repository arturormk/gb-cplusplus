find_package(PkgConfig REQUIRED)
pkg_check_modules(GB_FFMPEG REQUIRED IMPORTED_TARGET
  libavformat>=58 libavcodec>=58 libavutil>=56 libswscale>=5)

add_library(gb_media
  src/ffmpeg_support.cpp
  src/hud.cpp
  src/media.cpp
  src/playback.cpp
  src/projection.cpp)
add_library(gb::media ALIAS gb_media)

target_include_directories(gb_media PUBLIC include)
target_link_libraries(gb_media PUBLIC gb_project_options PRIVATE PkgConfig::GB_FFMPEG)
