# 3rdparty/CMakeLists.txt

# libclang
if(UNIX AND NOT APPLE)
    add_library(libclang SHARED IMPORTED GLOBAL)
    set_target_properties(libclang PROPERTIES
        IMPORTED_LOCATION "/usr/lib/llvm-18/lib/libclang-18.so"
        INTERFACE_INCLUDE_DIRECTORIES "/usr/lib/llvm-18/include"
    )
else()
    add_library(libclang SHARED IMPORTED GLOBAL)
    if(WIN32)
        set_target_properties(libclang PROPERTIES
            IMPORTED_IMPLIB "${CMAKE_CURRENT_LIST_DIR}/LLVM/lib/x64/libclang.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/LLVM/include"
        )
    elseif(APPLE)
        # vendored 的 LLVM dylib 缺少配套 resource 目录（builtin 头文件），解析时会报
        # 'stdarg.h' file not found；系统工具链的 libclang 自带 resource 目录，
        # 且其驱动能自动探测 macOS SDK，无需额外 -isysroot
        find_library(ATERNYX_SYSTEM_LIBCLANG libclang.dylib
            HINTS "/Library/Developer/CommandLineTools/usr/lib"
                  "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib")
        if(ATERNYX_SYSTEM_LIBCLANG)
            set(MACOS_LIBCLANG "${ATERNYX_SYSTEM_LIBCLANG}")
        else()
            set(MACOS_LIBCLANG "${CMAKE_CURRENT_LIST_DIR}/LLVM/bin/macOS/libclang.dylib")
        endif()
        set_target_properties(libclang PROPERTIES
            IMPORTED_LOCATION "${MACOS_LIBCLANG}"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/LLVM/include"
        )
    endif()
endif()

# Mustache (header-only)
if(NOT TARGET mustache)
    add_library(mustache INTERFACE)
    target_include_directories(mustache INTERFACE "${CMAKE_CURRENT_LIST_DIR}/Mustache")
endif()

# argparse (header-only)
if(NOT TARGET argparse)
    add_library(argparse INTERFACE)
    target_include_directories(argparse INTERFACE "${CMAKE_CURRENT_LIST_DIR}/argparse")
endif()

# toml (header-only)
if(NOT TARGET toml)
    add_library(toml INTERFACE)
    target_include_directories(toml INTERFACE "${CMAKE_CURRENT_LIST_DIR}/tomlplusplus")
endif()

# nlohmann/json (header-only)
if(NOT TARGET json)
    add_library(json INTERFACE)
    target_include_directories(json INTERFACE "${CMAKE_CURRENT_LIST_DIR}/JSON")
endif()
