#pragma once

// NOT ORIGINAL - image-quality options ("step 1" of a more modern look), read from [Graphics] in thug2-decomp.ini.
// Every default keeps the original's behaviour exactly:
//   anisotropy=0           (0 = the original: anisotropic minification only on sampler 0, and only at back buffers
//                           up to 480 lines - so never at today's sizes - capped at 3). 2-16: anisotropic
//                           minification on every sampler wherever the original filters linearly, at that many
//                           taps (clamped to the device's MaxAnisotropy; off if it can't filter anisotropically).
//   msaa=0                 (0 = the original: [Graphics] AntiAliasing picks 2 samples or none). 2-16: that many
//                           samples (the most the device has up to it, at its highest quality level), whatever
//                           AntiAliasing says; frames drawn through the screen copies (motion blur, bloom) are
//                           drawn into a multisampled surface and resolved into the copy before it's read.
//   draw_distance_scale=1  the far plane (96000, or the DistanceClipping distance) and the distance clipping's mesh
//                           cull distance, and the cameras' default far clip, multiplied (0.25-8). Fog is left as
//                           the level sets it: the clip line only moves further out behind it.
//   lod_distance_scale=1   objects' LOD distances (lod_dist1: where they stop animating, lod_dist2: where they stop
//                           being drawn) multiplied (0.25-8).
#include <cstdint>

namespace render::graphics_options {

struct Options {
    int anisotropy = 0; // 0, or 2-16
    int msaa = 0;       // 0, or 2-16
    float draw_distance_scale_ = 1.0f;
    float lod_distance_scale_ = 1.0f;
};

// The options as read (defaults until Load runs).
Options& get_pull();
// Read them from [Graphics] (at start-up, with the other graphics settings).
void read_in();

// --- The parsing and choices (no device: the self-tests check these) ---

// An anisotropy setting: below 2 off (0), above 16 16.
int parse_anisotropy(int value);
// A sample count setting: below 2 off (0), above 16 16.
int parse_multisample(int value);
// A scale setting ("1.5"): empty, unreadable, not a number or not above 0 -> 1; else clamped to 0.25-8.
float parse_scale(const char* text);
// The anisotropy to use on a device: 0 (the original's filtering) when the option is off or the device can't
// filter anisotropically, else the option clamped to its MaxAnisotropy (0 if that's below 2).
int device_anisotropy(int option, bool device_can_filter, uint32_t max_anisotropy);
// The minification filter a sampler gets where the original picks linear (`linear`) or point: anisotropic in place
// of linear when `anisotropy` is on. (D3DTEXF_* values.)
uint32_t get_min_filter(bool linear, int anisotropy);
// The sample count to ask for: the most, from the option down to 2, `supported` says yes to; 0 when none (or off).
int pick_samples(int option, bool (*supported)(int samples, void* context), void* context);
// A distance multiplied by a scale - the same float when the scale is 1 (the default: bit for bit the original).
float get_scaled(float distance, float scale);

// --- On the device ---

// The device's anisotropy (device_anisotropy), set from its caps by create_render_targets; 0 = the original's filters.
int& active_anisotropy();
// The sample count and quality chosen for the device (init_direct_3d, by the msaa option); 0 samples = the option off.
int chosen_samples();
uint32_t chosen_quality();

// The multisampled scene surface (with the msaa option, while the device is multisampled): the screen copies' full
// size ones (0x00786D1C / 0x00786D20) are drawn through it. Made / released with the other default-pool objects.
void create_scene_target();
void release_scene_target();

} // namespace render::graphics_options
