# CMake integration for AternyxMetaParser.
#
# aternyx_target_codegen(<target>
#     PARSER <AternyxParser executable>
#     TEMPLATE_DIR <dir>
#     [OUTPUT_DIR <dir>]        # default: ${CMAKE_BINARY_DIR}/generated
#     [CONFIG <toml>]           # TOML with project-independent settings:
#                               #   gen_path_style = "snake_case"|"CamelCase"
#                               #   parse_headers = true|false
#                               #   header_markers = ["CLASS(", ...]
#     [EXTRA_INCLUDE_PATHS <dir>...])
#
# The generated headers are compiled by <target> itself: this function runs
# the parser as part of the build (stamp based, re-runs when the target's
# sources, the template files, the CONFIG toml or the compile database change)
# and adds OUTPUT_DIR to the target's include directories. The output path and
# the include path are therefore one decision, made in one place — this is what
# makes the `#include` lines inside generated files resolve by construction.
#
# Whether the parser consumes the target's .cpp files or its annotated
# headers (parse_headers mode) is a project-wide setting: set
# `parse_headers = true` in the TOML passed via CONFIG. In that mode this
# function hands the target's own header sources to the parser (--headers-file
# list, one absolute path per line), so annotated headers are discovered
# wherever they live in the target's source tree; the parser falls back to
# scanning the .cpp directories only when the target lists no headers.
# Headers must be self-contained and must not include generated files.
#
# Notes:
#  - Requires a generator that exports compile_commands.json (Ninja; the
#    function enables CMAKE_EXPORT_COMPILE_COMMANDS if needed).
#  - Give each target its own OUTPUT_DIR: each run rewrites the per-category
#    all_include.gen.h aggregators for the target it generated.
#  - INTERFACE/umbrella targets produce no compile_commands.json entries and
#    cannot be processed.
#  - Cascade dependency: annotated headers may be transitively included across
#    link boundaries (a component header including a Resource-layer format
#    header makes the parser emit a second copy of that type's generated
#    header into this target's OUTPUT_DIR). Each mount records its stamp as
#    the ATERNYX_CODEGEN_STAMP target property; a mount whose link closure
#    reaches another mount depends on that mount's stamp, so an upstream
#    re-run cascades a re-parse downstream. Without this, a stale same-name
#    copy in a downstream OUTPUT_DIR desynchronizes read/write field sets
#    (silent codec roundtrip failure).

# Walk TARGET_NAME's link closure collecting codegen mounts: upstream stamp
# file paths into ${OUT_STAMPS}, upstream custom target names into
# ${OUT_TARGETS}. Both LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES are
# followed on every node: a PRIVATE link dep's headers are still parse inputs
# of the depending target. Generator expressions and non-target items are
# skipped; the visited set guards against alias/self loops.
function(_aternyx_codegen_collect_upstream_stamps TARGET_NAME OUT_STAMPS OUT_TARGETS)
  set(_visited "")
  set(_queue "${TARGET_NAME}")
  set(_stamps "")
  set(_targets "")
  while(_queue)
    list(POP_FRONT _queue _current)
    if(_current IN_LIST _visited)
      continue()
    endif()
    list(APPEND _visited "${_current}")
    if(NOT TARGET "${_current}")
      continue()
    endif()
    # ALIAS 目标读透到真身: stamp 属性与依赖名都归真身所有
    get_target_property(_aliased "${_current}" ALIASED_TARGET)
    if(NOT "${_aliased}" MATCHES "NOTFOUND$")
      set(_current "${_aliased}")
      if(_current IN_LIST _visited)
        continue()
      endif()
      list(APPEND _visited "${_current}")
    endif()
    get_target_property(_stamp "${_current}" ATERNYX_CODEGEN_STAMP)
    if(_stamp AND NOT "${_stamp}" MATCHES "NOTFOUND$")
      list(APPEND _stamps "${_stamp}")
      list(APPEND _targets "aternyx_codegen_${_current}")
    endif()
    foreach(_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_libs "${_current}" ${_prop})
      if(_libs AND NOT "${_libs}" MATCHES "NOTFOUND$")
        foreach(_lib IN LISTS _libs)
          if(_lib MATCHES "^\\$<" OR _lib IN_LIST _visited)
            continue()
          endif()
          if(TARGET "${_lib}")
            list(APPEND _queue "${_lib}")
          endif()
        endforeach()
      endif()
    endforeach()
  endwhile()
  if(_stamps)
    list(REMOVE_DUPLICATES _stamps)
  endif()
  if(_targets)
    list(REMOVE_DUPLICATES _targets)
  endif()
  set(${OUT_STAMPS} "${_stamps}" PARENT_SCOPE)
  set(${OUT_TARGETS} "${_targets}" PARENT_SCOPE)
endfunction()

function(aternyx_target_codegen TARGET_NAME)
  cmake_parse_arguments(ATC "" "PARSER;OUTPUT_DIR;TEMPLATE_DIR;CONFIG"
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

  # file(GLOB) 的相对模式会把相对 target 定义目录的路径写进 SOURCES: 统一还原
  # 为绝对路径, 供依赖列表与头文件清单使用
  get_target_property(_codegen_target_source_dir ${TARGET_NAME} SOURCE_DIR)
  set(_codegen_abs_sources "")
  foreach(_src IN LISTS _codegen_sources)
    if(IS_ABSOLUTE "${_src}")
      list(APPEND _codegen_abs_sources "${_src}")
    else()
      list(APPEND _codegen_abs_sources "${_codegen_target_source_dir}/${_src}")
    endif()
  endforeach()

  # parse_headers 模式下把 target 自己的头文件清单显式交给解析器: 注解头位于
  # 源码树任何层级都能被发现, 不再依赖 ".cpp 所在目录" 的扫描启发式。
  # 清单为空则不传, 解析器回落到目录扫描。
  set(_codegen_headers "")
  foreach(_src IN LISTS _codegen_abs_sources)
    get_filename_component(_ext "${_src}" EXT)
    string(TOLOWER "${_ext}" _ext)
    if(_ext MATCHES "^(\\.h|\\.hpp|\\.hxx|\\.hh)$")
      list(APPEND _codegen_headers "${_src}")
    endif()
  endforeach()

  set(ATC_STAMP "${ATC_OUTPUT_DIR}/.aternyx_codegen_${TARGET_NAME}.stamp")

  # 模板与样式配置是每次生成的输入: 必须显式列为依赖, 否则只改模板/toml 时
  # ninja 不会重跑解析器, 生成产物将停留在旧模板的结果 (CONFIGURE_DEPENDS
  # 保证新增/删除模板文件时触发重新 configure)
  file(GLOB ATC_TEMPLATE_FILES CONFIGURE_DEPENDS "${ATC_TEMPLATE_DIR}/*")

  # macOS: libclang 的驱动在进程内不会执行 xcrun 的 SDK 探测（/usr/bin/clang 能找到
  # SDK 是因为 xcrun 垫片设置了 SDKROOT），解析标准库头前需显式提供 SDK 路径
  set(_codegen_sdkroot_env "")
  if(APPLE)
    if(NOT DEFINED ATERNYX_MACOS_SDKROOT)
      execute_process(COMMAND xcrun --show-sdk-path
                      RESULT_VARIABLE _sdkprobe_result
                      OUTPUT_VARIABLE ATERNYX_MACOS_SDKROOT
                      OUTPUT_STRIP_TRAILING_WHITESPACE
                      ERROR_QUIET)
      if(NOT _sdkprobe_result EQUAL 0 OR NOT ATERNYX_MACOS_SDKROOT)
        set(ATERNYX_MACOS_SDKROOT "")
      endif()
    endif()
    if(ATERNYX_MACOS_SDKROOT)
      set(_codegen_sdkroot_env ${CMAKE_COMMAND} -E env SDKROOT=${ATERNYX_MACOS_SDKROOT})
    endif()
  endif()

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

  set(_codegen_headers_file "")
  if(_codegen_headers)
    set(_codegen_headers_file "${CMAKE_CURRENT_BINARY_DIR}/aternyx_codegen_${TARGET_NAME}.headers.txt")
    file(WRITE "${_codegen_headers_file}" "")
    foreach(_src IN LISTS _codegen_headers)
      file(APPEND "${_codegen_headers_file}" "${_src}\n")
    endforeach()
    list(APPEND _codegen_args --headers-file "${_codegen_headers_file}")
  endif()

  set(_codegen_command)
  if(_codegen_sdkroot_env)
    list(APPEND _codegen_command ${_codegen_sdkroot_env})
  endif()
  list(APPEND _codegen_command "${ATC_PARSER}" ${_codegen_args})

  set(_codegen_deps "${ATC_PARSER}" "${CMAKE_BINARY_DIR}/compile_commands.json"
          ${ATC_TEMPLATE_FILES} ${ATC_CONFIG} ${_codegen_abs_sources})
  if(_codegen_headers_file)
    list(APPEND _codegen_deps "${_codegen_headers_file}")
  endif()

  # 级联依赖: 链接闭包内上游挂载点重跑必然重写其 stamp (新于本 stamp 即触发本侧
  # 重解析), 传递包含的外部注解头变更不再需要手动删 stamp; stamp 文件依赖同时给
  # ninja 排出先上游后下游的执行序
  _aternyx_codegen_collect_upstream_stamps("${TARGET_NAME}" _codegen_upstream_stamps
                                           _codegen_upstream_targets)
  if(_codegen_upstream_stamps)
    list(APPEND _codegen_deps ${_codegen_upstream_stamps})
  endif()

  add_custom_command(
    OUTPUT "${ATC_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ATC_OUTPUT_DIR}"
    COMMAND ${_codegen_command}
    COMMAND ${CMAKE_COMMAND} -E touch "${ATC_STAMP}"
    DEPENDS ${_codegen_deps}
    COMMENT "AternyxMetaParser: generating reflection code for target '${TARGET_NAME}'"
    VERBATIM)

  add_custom_target(aternyx_codegen_${TARGET_NAME} DEPENDS "${ATC_STAMP}")
  # 目标级依赖兜底上游执行序 (跨目录 custom command 输出的可移植保证)
  if(_codegen_upstream_targets)
    add_dependencies(aternyx_codegen_${TARGET_NAME} ${_codegen_upstream_targets})
  endif()
  add_dependencies(${TARGET_NAME} aternyx_codegen_${TARGET_NAME})

  # 下游挂载点经链接闭包发现本挂载点 (ATERNYX_CODEGEN_STAMP), 见文件头级联依赖说明
  set_property(TARGET ${TARGET_NAME} PROPERTY ATERNYX_CODEGEN_STAMP "${ATC_STAMP}")

  # Single point of truth: the directory we generate into is also the
  # directory the target resolves generated includes from.
  target_include_directories(${TARGET_NAME} PRIVATE "${ATC_OUTPUT_DIR}")
endfunction()

# aternyx_add_parser_tool(<name> SRCS <src>... [OUTPUT_NAME <file>])
#
# Builds a custom parser tool on top of AternyxParserLib — the C++ half of
# the extension model. The tool's sources construct Aternyx::CodegenHooks
# (and/or a custom registry in TargetCodegenOptions) and call
# Aternyx::CMake::RunTargetCodegen; the resulting executable can replace
# PARSER in aternyx_target_codegen. See docs/MetaParser.md, "自定义 Codegen".
#
# Requires this repository to be part of the consuming build
# (add_subdirectory / FetchContent), because the AternyxParserLib target is
# linked into the tool.
function(aternyx_add_parser_tool TOOL_NAME)
  cmake_parse_arguments(APT "" "OUTPUT_NAME" "SRCS" ${ARGN})

  if(NOT APT_SRCS)
    message(FATAL_ERROR "aternyx_add_parser_tool: SRCS is required (at least one source file with a main())")
  endif()
  if(NOT TARGET AternyxParserLib)
    message(FATAL_ERROR
            "aternyx_add_parser_tool: target AternyxParserLib not found; add the AternyxMetaParser repository "
            "to your build first (add_subdirectory or FetchContent_MakeAvailable)")
  endif()

  add_executable(${TOOL_NAME} ${APT_SRCS})
  EnableUtf8(${TOOL_NAME})
  target_link_libraries(${TOOL_NAME} PRIVATE AternyxParserLib)
  if(APT_OUTPUT_NAME)
    set_target_properties(${TOOL_NAME} PROPERTIES OUTPUT_NAME "${APT_OUTPUT_NAME}")
  endif()
endfunction()
