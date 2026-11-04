# The DirectInput input backend (src/input_dinput): the original's DirectInput side - its keyboard read once a frame,
# its joysticks and mouse, the pad read with each port's controls, the device set-up and release. It implements
# input::Source (engine/pad_input/input_source.h); app/ adds it (app/main.cpp). It includes the engine and the layers
# below it (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without
# DirectInput.
add_library(thug2_input_dinput STATIC
    direct_input.cpp
    dinput_state.cpp
    dinput_source.cpp
)
# dinput8 / dxguid: DirectInput's data formats and effect GUIDs (direct_input.cpp)
target_link_libraries(thug2_input_dinput PUBLIC thug2_engine PRIVATE dinput8 dxguid)
