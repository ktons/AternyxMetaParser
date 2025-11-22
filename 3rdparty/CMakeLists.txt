# 3rdparty/CMakeLists.txt

# libclang
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
else()
    message(FATAL_ERROR "Unsupported platform for imported libclang.\nPlease configure LLVM paths in '3rdparty/CMakeLists.txt' or add platform-specific handling for your OS.")
endif()

# Mustache (header-only)
if(NOT TARGET mustache)
    add_library(mustache INTERFACE)
    target_include_directories(mustache INTERFACE "${CMAKE_CURRENT_LIST_DIR}/Mustache")
endif()
