#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/entity.h"

namespace MenuSystem {
class CUiTree;
} // namespace Front
namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace MenuSystem {

// 0x0041E2A0: the screen element manager (the singleton 0x00701440).
CUiTree* __cdecl GetScreenElementManager();

} // namespace Front

namespace GameObjects {

// The floating text component (type 0xEADA1621 - its name isn't in THUG2's scripts -, vtable 0x00646018, 0x6C bytes;
// made by 0x0041E3C0): a line of text over its object - a 3D screen element (id: its object's id - 0x1525E9DF unless
// given) made by the script 0x6A060CF0 and then kept at the object's position (raised by y_offset) with the text
// "\c<color><string>" while the object's model is visible; for `duration` seconds when one is given. The element is
// destroyed (script 0x2575B406) with the component.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 ConfigureFromData, 3 Finalize, 12 InvokeMethodByName; the rest
// the base's / shared (4 / 9 0x005480A0, 5 0x004E3F90 - ConfigureFromData through the vtable -, 6 0x0048DCD0, 7
// 0x0045D760, 8 / 10 0x005BAB90, 11 0x0045D720).
class CFloatingTextComponent : public CObjectModule {
public:
    static constexpr uint32_t kType = 0xEADA1621;

    // 0x0041E3C0 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined: the
    // base's, its vtable and type, no text, colour 2, y_offset 120, no id, no end; 0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x0041E5E0: the element destroyed (the script 0x2575B406 with id=), then the base's body.
    void DestroyBody();
    // 0x0041E6C0 (vtable slot 0)
    CFloatingTextComponent* Teardown(unsigned int flags);
    // 0x0041E2B0 (vtable slot 2): the id (the object's id - 0x1525E9DF the first time), string (copied, at most 64
    // characters - strncpy: not terminated when longer; an empty one ends the text now), color, y_offset, the id
    // (0x727F9552), duration (seconds: the end time).
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x0041E3A0 (vtable slot 3): the object's model component.
    void Seal();
    // 0x0041E450 (vtable slot 1): while the model is drawn (its model's +0x44: 2 set, 4 not) and there's text: past the
    // end time the text is emptied (once); the element's properties - id, text "\c<color><string>", pos3D (the object's
    // position, y + y_offset) - given to it (its vtable slot 1) or, with no element yet, to the script 0x6A060CF0.
    // (The original formats the text into 64 bytes on its stack: a string of more than about 60 characters overran
    // them and ended the game at its stack cookie check. Ours cuts it short instead.)
    void TickFrame();
    // 0x0041E270 (vtable slot 12): 0xE357E3CB ConfigureFromData again (through the vtable), 1; anything else 2.
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    char m_text[0x40];          // +0x18 string (0 first: none)
    int m_tint;                // +0x58 color (a font colour index)
    float m_yOffset;            // +0x5C y_offset
    uint32_t m_ident;              // +0x60 the element's id
    uint32_t m_endTime;         // +0x64 the game clock's time to end the text (0: none)
    CObjectModule* m_meshInstance;   // +0x68 the model component (its +0x208: the model)
};
static_assert(offsetof(CFloatingTextComponent, m_text) == 0x18 && offsetof(CFloatingTextComponent, m_tint) == 0x58 &&
                  offsetof(CFloatingTextComponent, m_ident) == 0x60 && offsetof(CFloatingTextComponent, m_meshInstance) == 0x68 &&
                  sizeof(CFloatingTextComponent) == 0x6C,
              "layout must match the original");

} // namespace Obj
