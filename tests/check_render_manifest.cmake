# The M6 gate on a fixture: every frame gb-render wrote names the GPS record that
# gb::telemetry::nearest_point (through gb-time --at) finds at the frame's source time.
#
#   cmake -DMANIFEST=m.json -DGB_TIME=gb-time -DRECORDING=gps.mp4 -P check_render_manifest.cmake
#
# The recording has one chapter, so a frame's chapter time is its recording time.

foreach(variable MANIFEST GB_TIME RECORDING)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "${variable} is not set")
  endif()
endforeach()

file(READ "${MANIFEST}" manifest)
string(JSON schema GET "${manifest}" schema)
if(NOT schema STREQUAL "gb.render.manifest/v1")
  message(FATAL_ERROR "unexpected manifest schema: ${schema}")
endif()
string(JSON video_numerator GET "${manifest}" time_base numerator)
string(JSON video_denominator GET "${manifest}" time_base denominator)
string(JSON count LENGTH "${manifest}" frames)
if(count EQUAL 0)
  message(FATAL_ERROR "the manifest has no frames")
endif()

math(EXPR last "${count} - 1")
set(checked 0)
foreach(index RANGE ${last})
  string(JSON frame GET "${manifest}" frames ${index})
  string(JSON number GET "${frame}" frame)
  if(NOT number EQUAL index)
    message(FATAL_ERROR "frame ${index} is numbered ${number}")
  endif()
  string(JSON chapter_ticks GET "${frame}" chapter_ticks)
  string(JSON gps_type TYPE "${frame}" gps)
  if(NOT gps_type STREQUAL "OBJECT")
    message(FATAL_ERROR "frame ${index} has no GPS record")
  endif()
  string(JSON record_ticks GET "${frame}" gps media_time_ticks)
  string(JSON telemetry_numerator GET "${frame}" gps time_base numerator)
  string(JSON telemetry_denominator GET "${frame}" gps time_base denominator)
  # The frame's time in the telemetry time base. The fixture's frames fall on whole milliseconds
  # and its telemetry counts milliseconds, so this is exact.
  math(EXPR at "${chapter_ticks} * ${video_numerator} * ${telemetry_denominator} / (${video_denominator} * ${telemetry_numerator})")
  execute_process(
    COMMAND "${GB_TIME}" --json --at ${at} "${RECORDING}"
    OUTPUT_VARIABLE report
    RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "gb-time --at ${at} failed: ${status}")
  endif()
  string(JSON nearest GET "${report}" query nearest media_time_ticks)
  if(NOT nearest EQUAL record_ticks)
    message(FATAL_ERROR
      "frame ${index} at ${at}: gb-render shows the record at ${record_ticks}, nearest_point is ${nearest}")
  endif()
  math(EXPR checked "${checked} + 1")
endforeach()
message(STATUS "manifest gate: ${checked} frames match nearest_point")
