set(TARGET_NAME AternyxParser)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_OPTIMIZE OFF)

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS "*.h" )
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "*.cpp")

add_executable(${TARGET_NAME} ${HEADER_FILES} ${SOURCE_FILES})
EnableUtf8(${TARGET_NAME})

target_link_libraries(${TARGET_NAME} PRIVATE mustache)

target_link_libraries(${TARGET_NAME} PRIVATE libclang)

target_compile_definitions(${TARGET_NAME} PUBLIC __META_TEMP_PATH__="${CMAKE_SOURCE_DIR}/Template")
target_compile_definitions(${TARGET_NAME} PUBLIC __SOURCE_PATH__="${CMAKE_SOURCE_DIR}/source")
target_compile_definitions(${TARGET_NAME} PUBLIC __GENERATED_CODE_PATH__="${CMAKE_SOURCE_DIR}/_Generated")

target_include_directories(${TARGET_NAME}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_BINARY_DIR}
)