# The QB script interpreter (src/script_qb): a running script's operations on its record - made and torn down, bound
# to a script, its Update and the statements it runs, its waits, call stack, interrupts and event handlers - and the
# list of all scripts with the manager's walks (the spawned scripts' frame, spawning, launching, stopping). It
# implements script::Host (engine/script/host.h); app/ installs it (app/main.cpp). It includes the engine and the
# layers below it (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without
# the interpreter - the script data (structures, arrays, symbols, loading .qb files) is the engine's.
add_library(thug2_script_qb STATIC
    cscript_manager.cpp
    cscript_wait.cpp
    cscript_exec.cpp
    cscript_core.cpp
    qb_host.cpp
)
target_link_libraries(thug2_script_qb PUBLIC thug2_engine)
