cmake_minimum_required(VERSION 3.23)
Project(Aternyx VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

function(EnableUtf8 target)
  if (MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
  else()
    target_compile_options(${target} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
  endif()
endfunction()

add_subdirectory(3rdparty)

add_subdirectory(Source)

add_subdirectory(Example)