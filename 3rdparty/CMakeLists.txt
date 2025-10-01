# 3rdparty/CMakeLists.txt

# libclang
add_library(libclang SHARED IMPORTED GLOBAL)
set_target_properties(libclang PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_LIST_DIR}/LLVM/lib/x64/libclang.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/LLVM/include"
)

# Mustache (header-only)
if (NOT TARGET mustache)
    add_library(mustache INTERFACE)
    target_include_directories(mustache INTERFACE "${CMAKE_CURRENT_LIST_DIR}/Mustache")
endif ()