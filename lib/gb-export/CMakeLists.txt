# Settings, framing geometry and layout for export: no Qt, unit-tested like the format libraries.
add_library(gb_export
  src/framing.cpp
  src/settings.cpp)
add_library(gb::exporting ALIAS gb_export)

target_include_directories(gb_export PUBLIC include)
target_link_libraries(gb_export PUBLIC gb_project_options)
