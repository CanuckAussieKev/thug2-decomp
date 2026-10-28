cmake_minimum_required(VERSION 3.25)
project(thug2_qb_js LANGUAGES C CXX)
include(FetchContent)
# Official upstream source, not vendored. No quickjs-libc, filesystem or OS modules.
FetchContent_Declare(quickjs
  URL https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz
  URL_HASH SHA256=b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(quickjs)
add_library(quickjs_core STATIC
  ${quickjs_SOURCE_DIR}/quickjs.c ${quickjs_SOURCE_DIR}/dtoa.c
  ${quickjs_SOURCE_DIR}/libregexp.c ${quickjs_SOURCE_DIR}/libunicode.c
  ${quickjs_SOURCE_DIR}/cutils.c)
# Do not put the source root on C++ include paths: its VERSION file shadows
# the standard <version> header on case-insensitive filesystems (macOS).
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/quickjs-include)
configure_file(${quickjs_SOURCE_DIR}/quickjs.h quickjs-include/quickjs.h COPYONLY)
target_include_directories(quickjs_core SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/quickjs-include)
target_compile_definitions(quickjs_core PRIVATE _GNU_SOURCE CONFIG_VERSION="2026-06-04")
set_target_properties(quickjs_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
# Match the official Makefile: engine arithmetic assumes wrapping signed ints.
target_compile_options(quickjs_core PRIVATE -fwrapv)
if(WIN32)
  target_compile_definitions(quickjs_core PRIVATE __USE_MINGW_ANSI_STDIO=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(quickjs_core PUBLIC Threads::Threads)
if(UNIX)
  target_link_libraries(quickjs_core PUBLIC m)
endif()
if(MSVC)
  message(FATAL_ERROR "Official QuickJS needs GCC/Clang extensions; use MinGW/Clang for this optional target")
endif()
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/runtime.js QB_RUNTIME_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS runtime.js)
configure_file(runtime_source.hpp.in runtime_source.hpp @ONLY)
add_library(thug2_js src/runtime.cpp)
target_compile_features(thug2_js PUBLIC cxx_std_20)
target_include_directories(thug2_js PUBLIC include PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(thug2_js PRIVATE quickjs_core)
target_compile_options(thug2_js PRIVATE -Wall -Wextra)
include(CTest)
if(BUILD_TESTING)
  find_program(NODE_EXECUTABLE node REQUIRED)
  execute_process(COMMAND ${NODE_EXECUTABLE} --version OUTPUT_VARIABLE NODE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  string(REGEX REPLACE "^v" "" NODE_VERSION "${NODE_VERSION}")
  if(NODE_VERSION VERSION_LESS 24)
    message(FATAL_ERROR "QB translator development requires Node 24 or newer")
  endif()
  set(FIXTURES ${CMAKE_CURRENT_BINARY_DIR}/fixtures.js)
  add_custom_command(OUTPUT ${FIXTURES}
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/qbjs/fixtures.ts ${FIXTURES}
    DEPENDS ../tools/qbjs/translate.ts ../tools/qbjs/fixtures.ts VERBATIM)
  add_executable(qb-js-tests tests/runtime_tests.cpp ${FIXTURES})
  target_link_libraries(qb-js-tests PRIVATE thug2_js)
  add_test(NAME qb-js-quickjs COMMAND qb-js-tests ${FIXTURES})
  add_test(NAME qb-js-translator COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_CURRENT_SOURCE_DIR}/../tools/qbjs/translate.test.ts)
endif()
