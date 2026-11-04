# The DirectSound sound backend (src/audio_dsound): the output made and released, its devices listed, and the sound
# effects' voices - their buffers made, filled, played, stopped and set, the 3D listener. It implements audio::Device
# (engine/audio/audio_device.h); app/ installs it (app/main.cpp). It includes the engine and the layers below it
# (tools/layers.ts checks it); nothing below app/ includes or links it, so the engine builds without DirectSound.
add_library(thug2_audio_dsound STATIC
    dsound_device.cpp
)
# dsound: DirectSoundCreate8, DirectSoundEnumerateA
target_link_libraries(thug2_audio_dsound PUBLIC thug2_engine PRIVATE dsound)
