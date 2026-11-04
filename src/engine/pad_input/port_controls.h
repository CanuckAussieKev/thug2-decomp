#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The original's Win32 platform layer's input tables (0x007D6770, 0x007D42F8, 0x007D6434): each port's controls
// (keys, joystick, mouse - from the registry; PartyMod writes port 0's keys from thug2-decomp.ini), the keyboard's
// key names, the graphics options. The DirectInput side that read the devices with them is
// input_dinput/direct_input.h, reached through the input seam (engine/pad_input/input_source.h).
namespace os_layer {

constexpr int kPorts = 2;
constexpr int kBindings = 24;
// The "distance" option (1-100): setting n clips at kDrawDistanceBase + kDrawDistanceStep * n.
constexpr float kDrawDistanceStep = 5.0F;
constexpr float kDrawDistanceBase = 95.0F;
// The actions' slots in a port's binding tables (the slots 12 and 19 are not named here).
constexpr int kBindUp = 0;
constexpr int kBindDown = 1;
constexpr int kBindLeft = 2;
constexpr int kBindRight = 3;
constexpr int kBindCameraUp = 4;
constexpr int kBindCameraDown = 5;
constexpr int kBindCameraLeft = 6;
constexpr int kBindCameraRight = 7;
constexpr int kBindOllie = 8;
constexpr int kBindGrab = 9;
constexpr int kBindKick = 10;
constexpr int kBindGrind = 11;
constexpr int kBindCameraToggle = 13;
constexpr int kBindLeftSpin = 14;
constexpr int kBindRightSpin = 15;
constexpr int kBindNollie = 16;
constexpr int kBindSwitchRevert = 17;
constexpr int kBindFocus = 18;
constexpr int kBindItemUp = 20;
constexpr int kBindItemDown = 21;
constexpr int kBindItemLeft = 22;
constexpr int kBindItemRight = 23;

// A port's controls (0x130 bytes, 0x007D6770 + 0x130 * port; -1 = none). init_input sets them all to -1.
struct PortControls {
    std::array<int32_t, kBindings> keys;    // +0x00 the keyboard's key (DIK code) for each action: registry "k<port>_<n>"
    GUID joystick;              // +0x60 the joystick playing as this port: registry "pad<port>"
    std::array<int32_t, kBindings> gamepad; // +0x70 the joystick's control for each action: registry "gp<port>_<n>"
    std::array<int32_t, kBindings> mouse;   // +0xD0 the mouse's control for each action (nothing here sets them)
};
constexpr std::size_t kPortControlsJoystickOffset = 0x60;
constexpr std::size_t kPortControlsGamepadOffset = 0x70;
constexpr std::size_t kPortControlsMouseOffset = 0xD0;
constexpr std::size_t kPortControlsBytes = 0x130;

static_assert(offsetof(PortControls, joystick) == kPortControlsJoystickOffset && offsetof(PortControls, gamepad) == kPortControlsGamepadOffset &&
                  offsetof(PortControls, mouse) == kPortControlsMouseOffset && sizeof(PortControls) == kPortControlsBytes,
              "layout must match the original");
inline std::array<PortControls, kPorts>& get_controls();

// The graphics options (0x007D6434; the registry's "options", "distance" and "resolution" - PartyMod's
// [Graphics] in thug2-decomp.ini, engine/gfx/settings.h).
struct Options {
    uint8_t antialiasing;       // 0x007D6434 options & 0x20
    uint8_t hq_shadows_;        // 0x007D6435 options & 2
    uint8_t fog;                // 0x007D6436 options & 1
    uint8_t option37;           // 0x007D6437 options & 0x10
    uint8_t option38;           // 0x007D6438 options & 4
    uint8_t option39;           // 0x007D6439 options & 8
    uint8_t distance_clipping_; // 0x007D643A options & 0x100
    uint8_t pad3b_;
    uint32_t resolution;        // 0x007D643C 0-3
    uint32_t distance;          // 0x007D6440 1-100 (0: none)
    int32_t clipping_distance_; // 0x007D6444 distance * 5 + 95
};
constexpr std::size_t kOptionsBytes = 0x14;

static_assert(sizeof(Options) == kOptionsBytes, "layout must match the original");

// 0x005F2F80 (cdecl): whether the port has any keyboard controls.
bool has_key_bindings(int port);
// 0x005F2FC0 (cdecl): whether the port has any mouse controls.
bool has_mouse_bindings(int port);
// 0x005F3120 (cdecl): the key's name (15 wide characters per key, 0x007D42F8).
wchar_t* get_key_name(int code);
// 0x005F3470 (cdecl): the hex number at `*cursor` (digits and upper-case A-F), the cursor moved past it.
uint32_t parse_hex(std::string_view& cursor);
// 0x005F34D0 (cdecl): the options and controls from HKEY_CURRENT_USER\<key> (PartyMod: thug2-decomp.ini).
void load_registry_options(const char* key);
// 0x005F3BF0 (cdecl): the key's name set (at most 14 characters). Returns the table's entry.
wchar_t* set_key_name(int code, const wchar_t* name);
// 0x005F3C10 (cdecl): every key's name: "(NONE)", then the keyboard's.
void init_key_names();

} // namespace os_layer
