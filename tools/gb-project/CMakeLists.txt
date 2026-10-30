find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Qml Quick Widgets ShaderTools)

qt_add_executable(gb_project
  main.cpp
  project_controller.cpp
  project_controller.hpp)
set_target_properties(gb_project PROPERTIES
  OUTPUT_NAME gb-project
  AUTOMOC ON)

qt_add_resources(gb_project "gb_project_resources"
  PREFIX "/"
  FILES
    qml/DarkButton.qml
    qml/DarkComboBox.qml
    qml/Main.qml
    qml/ViewPane.qml)

qt_add_shaders(gb_project "gb_project_shaders"
  PREFIX "/shaders"
  BASE "shaders"
  FILES shaders/projection.frag)

target_link_libraries(gb_project PRIVATE
  gb::media gb_project_options
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets)
target_compile_definitions(gb_project PRIVATE GB_PROJECT_VERSION="${PROJECT_VERSION}")

install(TARGETS gb_project RUNTIME DESTINATION bin)
