#pragma once

// NOT ORIGINAL - render::Device: what the engine side asks of the graphics backend. The backend is the original's PC
// platform layer (the engine's s_plat_* calls and what they reach, 0x004B2270-0x004E0A90, with the Direct3D files of
// our own); the engine side is everything above it - the engine's own entry points, the scenes, models, effects and
// lights as the engine sees them, the scripts, the game. Every call the engine side makes into the backend for a
// frame, a resource, a state block or a draw batch goes through the one installed device; app/ makes it and installs
// it at start-up (install_device), a run-time choice - Direct3D 9's (engine/gfx/d3d9_device.h) is the only one so far.
// The interface is shaped so that another backend (Vulkan) can stand in: resources are made explicitly and given
// back, states are set as the engine groups them (the fog, the gamma, the primitives' states), the frame is begun,
// drawn and finished as a whole, and a lost device is reset by the engine's main loop in steps.
//
// The grain is coarse: a call per frame, per resource made or released, per state block, per draw batch (a flash, a
// shatter, a splat set) - a few dozen a frame. Never per vertex or per object: what the engine does per vertex or
// per object (a primitive's vertices, a sphere's visibility) stays a direct call (render_device.cpp's notes).
//
// The methods keep the names and arguments of the functions they stand for (the backend's own, in the files the
// comments name), and each D3D9 method is that function: the same calls to Direct3D, in the same order.
//
// Not installed, a call dereferences null: the device is installed before any of the original's code runs.

namespace render {

class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    // --- the frame (engine_glue.h, draw_frame.h) ---
    // The device made, then the backend's particle manager, weather and screen effects (start_engine).
    virtual void start_engine() = 0;
    // The frame's scene begun, unless the loading screen is up (begin_frame_draw).
    virtual void begin_frame_draw() = 0;
    // The frame's views drawn - every viewport's world, the 2D, the screen effects (draw_world).
    virtual void draw_world() = 0;
    // The frame ended and shown, the draw counts reset, a pending screenshot taken, the next frame's clear
    // (finish_rendering).
    virtual void finish_rendering() = 0;

    // --- a lost device (the main loop's reset_if_lost, in its steps) ---
    // Whether the device can be reset now (not while it is still lost).
    virtual bool can_reset() = 0;
    // The objects a reset drops (render targets' textures, the dynamic buffers, the screen copies), and made again.
    virtual void release_device_objects() = 0;
    virtual void restore_device_objects() = 0;
    // The device reset with its present parameters; false if that failed (it is tried again next frame).
    virtual bool reset() = 0;

protected:
    ~Device() = default;
};

// The installed device (app/ installs it at start-up, once).
Device& device();
void install_device(Device& device);

} // namespace render
