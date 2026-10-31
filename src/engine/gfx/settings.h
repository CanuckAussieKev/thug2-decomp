#pragma once

#include <cstdint>

// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

// The graphics options on PC, from [Graphics] in thug2-decomp.ini in place of the original's settings loader
// (0x005F34D0, the registry): AntiAliasing, HQShadows, DistanceClipping, ClippingDistance (1-100), Fog, and
// DisableBlur (on by default: SetScreenBlur keeps the motion blur off), and (NOT ORIGINAL) OcclusionCulling (on by
// default, as the original: engine/gfx/occlusion.h).
namespace render {

// Read them (at start-up, before os_layer::init_input writes them).
void load_graphics_settings();
// Put them where the engine reads its options (0x007D6434...): os_layer::init_input.
void write_graphics_settings();

// The DisableBlur setting (see SetScreenBlur).
bool& blur_disabled();

extern bool g_fog_setting;
extern bool g_distance_clipping;

extern uint8_t g_hq_shadows;
} // namespace render
