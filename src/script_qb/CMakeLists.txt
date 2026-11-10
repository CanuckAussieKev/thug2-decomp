# The QB script interpreter (src/script_qb): a running script's operations on its record - made and torn down, bound
# to a script, its Update and the statements it runs, its waits, call stack, interrupts and event handlers - and the
# list of all scripts with the manager's walks (the spawned scripts' frame, spawning, launching, stopping). It
# implements script::Host (engine/script/host.h); app/ installs it (app/main.cpp). It includes the engine and the
# layers below it (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without
# the interpreter - the script data (structures, arrays, symbols, loading .qb files) is the engine's.
#
# NOT ORIGINAL - TypeScript scripts (ts_bridge.cpp, cscript_ts.cpp; docs/qb-ts.md "In the game"): the interpreter runs a
# translated script's levels in QuickJS. The runtime's JavaScript (scripting/*.js) is embedded as byte arrays.
set(THUG2_TS_JS numeric random values runtime qb_ts)
set(THUG2_TS_SOURCES_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ts_sources.h")
set(THUG2_TS_JS_FILES "")
foreach(name IN LISTS THUG2_TS_JS)
    list(APPEND THUG2_TS_JS_FILES "${PROJECT_SOURCE_DIR}/scripting/${name}.js")
endforeach()
add_custom_command(OUTPUT "${THUG2_TS_SOURCES_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${THUG2_TS_SOURCES_HEADER} -DINPUT_DIR=${PROJECT_SOURCE_DIR}/scripting
            "-DNAMES=${THUG2_TS_JS}" -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_js.cmake"
    DEPENDS ${THUG2_TS_JS_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/embed_js.cmake"
    VERBATIM)

add_library(thug2_script_qb STATIC
    cscript_manager.cpp
    cscript_wait.cpp
    cscript_exec.cpp
    cscript_core.cpp
    cscript_ts.cpp
    script_check.cpp
    ts_bridge.cpp
    qb_host.cpp
    "${THUG2_TS_SOURCES_HEADER}"
)
target_include_directories(thug2_script_qb PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(thug2_script_qb PUBLIC thug2_engine PRIVATE qjs)
