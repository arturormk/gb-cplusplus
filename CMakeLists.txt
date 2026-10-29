cmake_minimum_required(VERSION 3.22)

project(gb-cplusplus VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(gb_project_options INTERFACE)
target_compile_features(gb_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(gb_project_options INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
  if(GB_WARNINGS_AS_ERRORS)
    target_compile_options(gb_project_options INTERFACE -Werror)
  endif()
endif()

add_subdirectory(lib/gb-container)
add_subdirectory(tools/gb-inspect)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
