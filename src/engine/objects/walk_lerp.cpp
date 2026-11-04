// The walk code's interpolation helper (see walk_lerp.h). Floating point follows the original's x87 code: values it
// keeps on the x87 stack are doubles here, values it stores are floats.

#include "engine/objects/walk_lerp.h"

// 0x0043D990
maths::Vec4* walk::get_lerp(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float blend)
{
    const double dx = static_cast<double>(b->x) - a->x;
    const double dy = static_cast<double>(b->y) - a->y;
    auto dz = static_cast<float>(static_cast<double>(b->z) - a->z);
    auto dw = static_cast<float>(static_cast<double>(b->w) - a->w);
    auto sx = static_cast<float>(dx * blend);
    auto sz = static_cast<float>(static_cast<double>(dz) * blend);
    auto sw = static_cast<float>(static_cast<double>(dw) * blend);
    out->w = static_cast<float>(static_cast<double>(sw) + a->w);
    out->x = static_cast<float>(static_cast<double>(sx) + a->x);
    out->y = static_cast<float>((dy * blend) + a->y);
    out->z = static_cast<float>(static_cast<double>(sz) + a->z);
    return out;
}
