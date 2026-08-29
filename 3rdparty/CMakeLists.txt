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
        set_target_properties(libclang PROPERTIES
            IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/LLVM/bin/macOS/libclang.dylib"
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
