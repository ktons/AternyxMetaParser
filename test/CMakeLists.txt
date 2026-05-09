# Test targets - mirrors source/ structure

set(TEST_TARGET AternyxParserTests)

# GTest
set(GTEST_LIB /usr/lib/x86_64-linux-gnu/libgtest.a)
set(GTEST_MAIN_LIB /usr/lib/x86_64-linux-gnu/libgtest_main.a)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

enable_testing()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(${TEST_TARGET} ${TEST_SOURCES})

target_link_libraries(${TEST_TARGET} PRIVATE AternyxParserLib)
target_link_libraries(${TEST_TARGET} PRIVATE ${GTEST_LIB} ${GTEST_MAIN_LIB} Threads::Threads)

# UTF-8
if(MSVC)
    target_compile_options(${TEST_TARGET} PRIVATE /utf-8)
else()
    target_compile_options(${TEST_TARGET} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# Register tests
add_test(NAME AternyxParserTests COMMAND ${TEST_TARGET})
