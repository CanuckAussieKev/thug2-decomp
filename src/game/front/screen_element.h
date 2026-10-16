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

    uint32_t m_flags; // +0x08
    uint8_t m_unknown0C[0x4C - 0x0C];
    CScreenElement* mp_parent; // +0x4C
};

using ElementPtr = Obj::CSmtPtr<CScreenElement>;

// The element with this id (the screen element manager's lookup, 0x004AAE20 - not rewritten yet), into `out`
// (which must be empty).
void GetElement(ElementPtr* out, uint32_t id);

} // namespace Front
