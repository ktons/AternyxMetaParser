# CMake integration for AternyxMetaParser.
#
# aternyx_target_codegen(<target>
#     PARSER <AternyxParser executable>
#     TEMPLATE_DIR <dir>
#     [OUTPUT_DIR <dir>]        # default: ${CMAKE_BINARY_DIR}/generated
#     [PROJECT_ROOT <dir>]      # extra header scan root for parse_headers mode
#     [CONFIG <toml>]           # TOML with project-independent settings:
#                               #   gen_path_style = "snake_case"|"camel_case"
#                               #   parse_headers = true|false
#                               #   header_markers = ["CLASS(", ...]
#     [EXTRA_INCLUDE_PATHS <dir>...])
#
# The generated headers are compiled by <target> itself: this function runs
# the parser as part of the build (stamp based, re-runs when the target's
# sources or the compile database change) and adds OUTPUT_DIR to the target's
# include directories. The output path and the include path are therefore one
# decision, made in one place — this is what makes the `#include` lines
# inside generated files resolve by construction.
#
# Whether the parser consumes the target's .cpp files or its annotated
# headers (parse_headers mode) is a project-wide setting: set
# `parse_headers = true` in the TOML passed via CONFIG. Headers must be
# self-contained and must not include generated files. Annotated headers
# outside the target's source directories are found when PROJECT_ROOT (or a
# directory reachable from it) contains them.
#
# Notes:
#  - Requires a generator that exports compile_commands.json (Ninja; the
#    function enables CMAKE_EXPORT_COMPILE_COMMANDS if needed).
#  - Give each target its own OUTPUT_DIR: each run rewrites the per-category
#    all_include.gen.h aggregators for the target it generated.
#  - INTERFACE/umbrella targets produce no compile_commands.json entries and
#    cannot be processed.

function(aternyx_target_codegen TARGET_NAME)
  cmake_parse_arguments(ATC "" "PARSER;OUTPUT_DIR;TEMPLATE_DIR;PROJECT_ROOT;CONFIG"
                        "EXTRA_INCLUDE_PATHS" ${ARGN})

  if(NOT TARGET ${TARGET_NAME})
    message(FATAL_ERROR "aternyx_target_codegen: '${TARGET_NAME}' is not a target")
  endif()
  if(NOT ATC_PARSER)
    message(FATAL_ERROR "aternyx_target_codegen: PARSER is required (path to the built AternyxParser executable)")
  endif()
  if(NOT EXISTS "${ATC_PARSER}")
    message(FATAL_ERROR
            "aternyx_target_codegen: AternyxParser not found at '${ATC_PARSER}' (build AternyxMetaParser first)")
  endif()
  if(NOT ATC_TEMPLATE_DIR)
    message(FATAL_ERROR "aternyx_target_codegen: TEMPLATE_DIR is required")
  endif()

  if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON PARENT_SCOPE)
    message(STATUS "aternyx_target_codegen: enabled CMAKE_EXPORT_COMPILE_COMMANDS")
  endif()

  if(NOT ATC_OUTPUT_DIR)
    set(ATC_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated")
  endif()

  get_target_property(_codegen_sources ${TARGET_NAME} SOURCES)
  if(NOT _codegen_sources)
    message(WARNING
            "aternyx_target_codegen: target '${TARGET_NAME}' has no sources; it will produce no "
            "compile_commands.json entries, so generation cannot find it (INTERFACE targets are not supported)")
  endif()

  set(ATC_STAMP "${ATC_OUTPUT_DIR}/.aternyx_codegen_${TARGET_NAME}.stamp")

  set(_codegen_args
      "${CMAKE_BINARY_DIR}/compile_commands.json"
      --target "${TARGET_NAME}"
      -o "${ATC_OUTPUT_DIR}"
      -t "${ATC_TEMPLATE_DIR}")
  if(ATC_PROJECT_ROOT)
    list(APPEND _codegen_args -p "${ATC_PROJECT_ROOT}")
  endif()
  if(ATC_CONFIG)
    list(APPEND _codegen_args --toml "${ATC_CONFIG}")
  endif()
  foreach(_dir IN LISTS ATC_EXTRA_INCLUDE_PATHS)
    list(APPEND _codegen_args -i "${_dir}")
  endforeach()

  add_custom_command(
    OUTPUT "${ATC_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ATC_OUTPUT_DIR}"
    COMMAND "${ATC_PARSER}" ${_codegen_args}
    COMMAND ${CMAKE_COMMAND} -E touch "${ATC_STAMP}"
    DEPENDS "${ATC_PARSER}" "${CMAKE_BINARY_DIR}/compile_commands.json" ${_codegen_sources}
    COMMENT "AternyxMetaParser: generating reflection code for target '${TARGET_NAME}'"
    VERBATIM)

  add_custom_target(aternyx_codegen_${TARGET_NAME} DEPENDS "${ATC_STAMP}")
  add_dependencies(${TARGET_NAME} aternyx_codegen_${TARGET_NAME})

  # Single point of truth: the directory we generate into is also the
  # directory the target resolves generated includes from.
  target_include_directories(${TARGET_NAME} PRIVATE "${ATC_OUTPUT_DIR}")
endfunction()
