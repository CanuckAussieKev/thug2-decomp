# NOT ORIGINAL - rich by-value QB codec.
# Adds the codec sources to the scripting library and registers its own
# QuickJS test target. Does not change existing target policies.
# The library part must run regardless of BUILD_TESTING; the test part is
# guarded by BUILD_TESTING and is registered only after the parent
# CMakeLists has included CTest (BUILD_TESTING defined).
target_sources(thug2_js PRIVATE
  src/value_codec.cpp)
target_include_directories(thug2_js PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT BUILD_TESTING)
  return()
endif()
add_executable(qb-js-value-codec-tests tests/value_codec_tests.cpp)
target_link_libraries(qb-js-value-codec-tests PRIVATE thug2_js quickjs_core)
target_compile_features(qb-js-value-codec-tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(qb-js-value-codec-tests PRIVATE /W4)
else()
  target_compile_options(qb-js-value-codec-tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME qb-js-value-codec COMMAND qb-js-value-codec-tests)
set_tests_properties(qb-js-value-codec PROPERTIES TIMEOUT 120)
