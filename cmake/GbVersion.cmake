# Derives the project version from the top-level VERSION file and Git (ADR-0010).
#
# VERSION holds MAJOR.MINOR for odd (development) minors and MAJOR.MINOR[.PATCH] for even
# (release) minors:
#   - odd minor:  PATCH is the number of commits reachable from HEAD, e.g. 0.1 -> 0.1.13;
#   - even minor: the file is used verbatim and never modified automatically, e.g. 1.0 or 1.0.2.
#
# Outputs: GB_VERSION, GB_VERSION_MAJOR, GB_VERSION_MINOR, GB_VERSION_PATCH (empty when an even
# version has no patch) and GB_VERSION_IS_RELEASE (TRUE for even minors).
#
# Builds without Git history (source archives, distribution builds) may pass
# -DGB_VERSION_PATCH_OVERRIDE=N. In script mode (`cmake -P cmake/GbVersion.cmake`) the version
# is printed to stdout.

get_filename_component(_gb_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_gb_version_file "${_gb_source_dir}/VERSION")

file(STRINGS "${_gb_version_file}" _gb_declared LIMIT_COUNT 1)
string(STRIP "${_gb_declared}" _gb_declared)
if(NOT _gb_declared MATCHES "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?$")
  message(FATAL_ERROR "VERSION must contain MAJOR.MINOR[.PATCH], found '${_gb_declared}'")
endif()
set(GB_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(GB_VERSION_MINOR "${CMAKE_MATCH_2}")
set(_gb_declared_patch "${CMAKE_MATCH_4}")
math(EXPR _gb_minor_parity "${GB_VERSION_MINOR} % 2")

if(_gb_minor_parity EQUAL 0)
  set(GB_VERSION_IS_RELEASE TRUE)
  set(GB_VERSION_PATCH "${_gb_declared_patch}")
  set(GB_VERSION "${_gb_declared}")
else()
  set(GB_VERSION_IS_RELEASE FALSE)
  if(NOT _gb_declared_patch STREQUAL "")
    message(FATAL_ERROR "VERSION '${_gb_declared}' has an odd minor; its patch is derived from "
      "Git, so VERSION must contain only MAJOR.MINOR")
  endif()

  if(DEFINED GB_VERSION_PATCH_OVERRIDE AND NOT GB_VERSION_PATCH_OVERRIDE STREQUAL "")
    set(GB_VERSION_PATCH "${GB_VERSION_PATCH_OVERRIDE}")
  else()
    find_program(GIT_EXECUTABLE git)
    set(GB_VERSION_PATCH 0)
    if(GIT_EXECUTABLE)
      execute_process(COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
        WORKING_DIRECTORY "${_gb_source_dir}"
        OUTPUT_VARIABLE _gb_count OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _gb_count_result ERROR_QUIET)
      execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --is-shallow-repository
        WORKING_DIRECTORY "${_gb_source_dir}"
        OUTPUT_VARIABLE _gb_shallow OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
        WORKING_DIRECTORY "${_gb_source_dir}"
        OUTPUT_VARIABLE _gb_git_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(GIT_EXECUTABLE AND _gb_count_result EQUAL 0)
      set(GB_VERSION_PATCH "${_gb_count}")
      if(_gb_shallow STREQUAL "true")
        message(WARNING "Shallow Git clone: the version patch (${_gb_count}) undercounts commits. "
          "Fetch full history or pass -DGB_VERSION_PATCH_OVERRIDE=N.")
      endif()
      # Every commit, checkout or reset appends to logs/HEAD; reconfigure so the patch follows.
      if(NOT CMAKE_SCRIPT_MODE_FILE AND EXISTS "${_gb_git_dir}/logs/HEAD")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gb_git_dir}/logs/HEAD")
      endif()
    else()
      message(WARNING "No Git history: the version patch defaults to 0. "
        "Pass -DGB_VERSION_PATCH_OVERRIDE=N to set it.")
    endif()
  endif()
  set(GB_VERSION "${GB_VERSION_MAJOR}.${GB_VERSION_MINOR}.${GB_VERSION_PATCH}")
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gb_version_file}")
elseif(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${GB_VERSION}")
endif()
