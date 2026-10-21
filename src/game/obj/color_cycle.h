#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The colour cycle component (type 0xCDC76A22 - its name and its members' names aren't in THUG2's scripts -, vtable
// 0x00645E00, 0x7C bytes; made by 0x0041B3C0): while on, sets its object's colour (the SetObjectColor helper,
// 0x005A3E80) to each of up to five colours in turn, one every `period` milliseconds; switched off, back to the
// neutral 0x808080.
//
// Vtable: 0 the scalar deleting destructor, 1 Update; the rest the base's / shared (2 / 8 / 10 0x005BAB90, 3 / 4 / 9
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 11 0x0042BB20, 12 0x0051ADC0).
class CColorCycleComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xCDC76A22;
    static constexpr int kMaxColors = 5;

    // 0x0041B3C0 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined:
    // the base's, no colours, off; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041B280 (not a Ghidra function; registered with Create): its member functions' table (0x006845D0; the
    // functions are put in by the static initialiser at 0x00643D60).
    static void __cdecl RegisterMemberFunctions();
    // 0x0041B2A0: the base's body.
    void DestroyBody();
    // 0x0041B2E0 (vtable slot 0)
    CColorCycleComponent* Destroy(unsigned int flags);
    // 0x0041B580 (vtable slot 1): while on, the object's colour set to the current one ((z << 8 + y) << 8 + x, each
    // truncated); the time goes on (milliseconds, slow motion counted); once it reaches the period, the next colour
    // (back to the first after the last), from 0 ms.
    void Update();

    // 0x0041B310: the colours from `colors` (vectors; their x, y, z with w 1), from the first.
    void SetColors(Script::CArray* colors);
    // 0x0041B440: on or off. Switched off: the object's colour back to 0x808080. Switched on: from the first colour, 0
    // ms.
    void SetOn(bool on);

    // --- the member functions scripts call (the table at 0x006845D0) ---
    // 0x0041B560 [0x4AF8B9AE]: on. 1.
    int On(Script::CStruct* params, Script::CScript* script);
    // 0x0041B570 [0x1DE2F35B]: off. 1.
    int Off(Script::CStruct* params, Script::CScript* script);
    // 0x0041B2B0 [0x4A752AAE]: the period from the unnamed float (asserted; the original's slot for it is the params
    // argument: without one, the period is the params pointer's bits). 1.
    int SetPeriodFromScript(Script::CStruct* params, Script::CScript* script);
    // 0x0041B390 [0x80D5F732]: the colours from the unnamed array (asserted; without one, the params pointer is taken
    // for the array, as the original does). 1.
    int SetColorsFromScript(Script::CStruct* params, Script::CScript* script);

    Mth::Vector m_colors[kMaxColors]; // +0x18 (x, y, z: 0-255)
    int m_colorCount;                 // +0x68
    int m_current;                    // +0x6C
    float m_period;                   // +0x70 milliseconds
    float m_time;                     // +0x74 milliseconds on the current colour
    bool m_on;                        // +0x78
    uint8_t pad79[3];
};
static_assert(offsetof(CColorCycleComponent, m_colors) == 0x18 && offsetof(CColorCycleComponent, m_colorCount) == 0x68 &&
                  offsetof(CColorCycleComponent, m_period) == 0x70 && offsetof(CColorCycleComponent, m_on) == 0x78 &&
                  sizeof(CColorCycleComponent) == 0x7C,
              "layout must match the original");

} // namespace Obj
