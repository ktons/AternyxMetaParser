set(LIB_TARGET AternyxParserLib)
set(EXE_TARGET AternyxParser)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_OPTIMIZE OFF)

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS "*.h")
file(GLOB_RECURSE LIB_SOURCE_FILES CONFIGURE_DEPENDS "*.cpp")
list(FILTER LIB_SOURCE_FILES EXCLUDE REGEX "Main\\.cpp$")

add_library(${LIB_TARGET} ${HEADER_FILES} ${LIB_SOURCE_FILES})
EnableUtf8(${LIB_TARGET})

target_include_directories(${LIB_TARGET}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_BINARY_DIR}
)

target_link_libraries(${LIB_TARGET} PUBLIC mustache libclang argparse toml json)

add_executable(${EXE_TARGET} Main.cpp)
target_link_libraries(${EXE_TARGET} PRIVATE ${LIB_TARGET})
