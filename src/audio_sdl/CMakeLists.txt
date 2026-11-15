# The macOS (arm64) port's sound backend (src/audio_sdl): the output made and released, and the sound effects' voices
# mixed in SDL3's audio callback, in place of src/audio_dsound's DirectSound calls. It implements audio::Device
# (engine/audio/audio_device.h); app/ installs it (THUG2_PORTABLE). It includes the engine and the layers below it
# (tools/layers.ts checks it).
add_library(thug2_audio_sdl STATIC
    sdl_audio_device.cpp
)
target_link_libraries(thug2_audio_sdl PUBLIC thug2_engine PRIVATE sdl3)
