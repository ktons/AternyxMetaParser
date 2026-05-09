# Test targets - mirrors source/ structure

set(TEST_TARGET AternyxParserTests)

# Try system GTest first (fast local builds), fall back to FetchContent
find_package(GTest QUIET)

if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

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
# add_test(NAME AternyxParserTests COMMAND ${TEST_TARGET})
include(GoogleTest)
gtest_discover_tests(${TEST_TARGET}
  DISCOVERY_TIMEOUT 60
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}   # 让测试在仓库根目录执行，能找到 example/Template
  PROPERTIES
    TIMEOUT 10
)
