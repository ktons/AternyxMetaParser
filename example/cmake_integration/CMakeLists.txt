cmake_minimum_required(VERSION 3.23)
project(AternyxCodegenExample CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Path to the AternyxMetaParser repository, built at least once so the
# AternyxParser executable exists:
#   cmake -B build -DATERNYX_META_PARSER_ROOT=<AternyxMetaParser repo>
set(ATERNYX_META_PARSER_ROOT "" CACHE PATH "AternyxMetaParser repository root")
if(NOT EXISTS "${ATERNYX_META_PARSER_ROOT}/cmake/AternyxMetaParser.cmake")
  message(FATAL_ERROR
          "ATERNYX_META_PARSER_ROOT must point at the AternyxMetaParser repository "
          "(cmake/AternyxMetaParser.cmake not found there). Example:\n"
          "  cmake -B build -G Ninja -DATERNYX_META_PARSER_ROOT=F:/Project/AternyxMetaParser")
endif()

find_program(ATERNYX_PARSER_EXE
             NAMES AternyxParser AternyxParser.exe
             HINTS "${ATERNYX_META_PARSER_ROOT}/build/bin"
             REQUIRED
             DOC "AternyxParser executable")

include("${ATERNYX_META_PARSER_ROOT}/cmake/AternyxMetaParser.cmake")

add_executable(example_app src/main.cpp)
target_include_directories(example_app PRIVATE
  "${ATERNYX_META_PARSER_ROOT}"  # for meta/meta_attributes.h
)

# Parse the annotated headers of this target and generate reflection code
# into build/generated as part of the build. parse_headers mode (and other
# project-independent settings) comes from the TOML passed via CONFIG.
aternyx_target_codegen(example_app
  PARSER "${ATERNYX_PARSER_EXE}"
  TEMPLATE_DIR "${ATERNYX_META_PARSER_ROOT}/Template"
  CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/aternyx_parser.toml")
