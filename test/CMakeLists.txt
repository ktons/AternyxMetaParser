# Test targets - mirrors source/ structure

set(TEST_TARGET AternyxParserTests)

# Fetch GTest from upstream (tarball, cross-platform)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

enable_testing()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(${TEST_TARGET} ${TEST_SOURCES})

target_link_libraries(${TEST_TARGET} PRIVATE AternyxParserLib)
target_link_libraries(${TEST_TARGET} PRIVATE GTest::gtest GTest::gtest_main Threads::Threads)

# UTF-8
if(MSVC)
    target_compile_options(${TEST_TARGET} PRIVATE /utf-8)
else()
    target_compile_options(${TEST_TARGET} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# Register tests
add_test(NAME AternyxParserTests COMMAND ${TEST_TARGET})
