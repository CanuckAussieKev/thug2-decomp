// The PC model: see pc_model.h.

#include "engine/gfx/pc_model.h"

#include <cmath>
#include <gsl/assert>

#include "engine/gfx/engine_geom.h"
#include "engine/gfx/model.h"

namespace render {

constexpr float kHalf = 0.5F;

namespace {

void* geom_of(const ModelGeomNode* node)
{
    Expects(node != nullptr);
    return node->value->geom;
}

// The engine's CGeom calls (jumps through the geom's vtable).
maths::Vec4* get_geom_bounding_sphere(void* geom, maths::Vec4* out)
{
    return static_cast<Geom*>(geom)->fetch_bound_sphere(out);
}
void set_geom_bounding_sphere(void* geom, const maths::Vec4* sphere)
{
    static_cast<Geom*>(geom)->change_bound_sphere(sphere); // (through the jump 0x0048DEB0)
}

} // namespace

// 0x004BADF0
void PcModel::plat_set_bounding_sphere(const maths::Vec4* sphere)
{
    const ModelGeomNode* head = geom_ring().head();
    for (const ModelGeomNode* node = head ? head->next : nullptr; node != head; node = node->next)
        set_geom_bounding_sphere(geom_of(node), sphere);
}

// 0x004BAE30
maths::Vec4* PcModel::plat_get_bounding_sphere(maths::Vec4* out)
{
    if (geom_total() == 0) {
        *out = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        return out;
    }
    const ModelGeomNode* head = geom_ring().head();
    const ModelGeomNode* node = head ? head->next : nullptr;
    maths::Vec4 scratch;
    maths::Vec4 sphere = *get_geom_bounding_sphere(geom_of(node), &scratch);
    for (node = node->next; node != head; node = node->next) {
        const maths::Vec4 other = *get_geom_bounding_sphere(geom_of(node), &scratch);
        auto dx = static_cast<float>(static_cast<double>(other.x) - sphere.x);
        auto dy = static_cast<float>(static_cast<double>(other.y) - sphere.y);
        auto dz = static_cast<float>(static_cast<double>(other.z) - sphere.z);
        auto dw = static_cast<float>(static_cast<double>(other.w) - sphere.w);
        auto distance = static_cast<float>(std::sqrt((static_cast<double>(dz) * dz) + (static_cast<double>(dy) * dy) +
                                                     (static_cast<double>(dx) * dx)));
        if (static_cast<double>(other.w) + distance <= sphere.w)
            continue; // (inside the one so far)
        if (static_cast<double>(sphere.w) + distance <= other.w) {
            sphere = other; // (the one so far inside it)
            continue;
        }
        // Both: the sphere around the two - its centre half way between their far sides.
        const double t = static_cast<double>(dw) / distance;
        const double sum_x = static_cast<double>(other.x) + sphere.x;
        const double sum_y = static_cast<double>(other.y) + sphere.y;
        auto sum_z = static_cast<float>(static_cast<double>(other.z) + sphere.z);
        auto sum_w = static_cast<float>(static_cast<double>(other.w) + sphere.w);
        auto offset_y = static_cast<float>(dy * t);
        auto offset_z = static_cast<float>(dz * t);
        auto x = static_cast<float>((dx * t) + sum_x);
        auto y = static_cast<float>(offset_y + sum_y);
        sphere.z = static_cast<float>((static_cast<double>(offset_z) + sum_z) * kHalf);
        sphere.x = static_cast<float>(static_cast<double>(x) * kHalf);
        sphere.y = static_cast<float>(static_cast<double>(y) * kHalf);
        sphere.w = static_cast<float>((static_cast<double>(sum_w) + distance) * kHalf);
    }
    out->x = sphere.x;
    out->y = sphere.y;
    out->z = sphere.z;
    out->w = sphere.w + sphere.w;
    return out;
}

} // namespace render
