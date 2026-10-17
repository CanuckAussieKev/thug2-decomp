#pragma once

#include <cstdint>

#include "game/obj/smtptr.h"

// The front end's screen elements (menus, text, sprites...). Only what the code rewritten so far uses.
namespace Front {

class CScreenElement : public Obj::CRefCounted {
public:
    enum : uint32_t { kHidden = 0x400000 };

    // 0x004A8F40: hidden itself, or inside a hidden parent.
    bool IsHidden() const;

    // 0x004A87A0 / 0x004A87B0: the element's width / height on the 640x480 layout (+0x160 times +0x144, +0x164
    // times +0x148; returned unrounded in ST0, as the original's x87 product - hence double).
    double GetLayoutWidth() const;
    double GetLayoutHeight() const;

    uint32_t m_flags; // +0x08
    uint8_t m_unknown0C[0x4C - 0x0C];
    CScreenElement* mp_parent; // +0x4C
    uint8_t m_unknown50[0x114 - 0x50];
    float m_alpha; // +0x114 the alpha it's drawn with (parents' included; Element3d hides its models below 0.0001)
    uint8_t m_unknown118[0x134 - 0x118];
    // Where it is drawn, after justification and the parents' (Element3d: its top left), and its scale.
    float m_x, m_y;           // +0x134
    float m_scaleX, m_scaleY; // +0x13C
    // (with +0x160 / +0x164: the size on the layout - see GetLayoutWidth; which is the scale and which the base
    // size isn't known yet)
    float m_unknown144, m_unknown148; // +0x144
    uint8_t m_unknown14C[0x160 - 0x14C];
    float m_unknown160, m_unknown164; // +0x160
    uint8_t m_unknown168[0x178 - 0x168];
};

using ElementPtr = Obj::CSmtPtr<CScreenElement>;

// The element with this id (the screen element manager's lookup, 0x004AAE20 - not rewritten yet), into `out`
// (which must be empty).
void GetElement(ElementPtr* out, uint32_t id);

} // namespace Front
