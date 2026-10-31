// The PC model: see pc_model.h.

#include "engine/gfx/pc_model.h"

#include <cmath>

#include "engine/core/manager.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/model.h"

namespace render {

namespace {

void* geom_of(const ModelGeomNode* node) { return *node->entry; }

// The engine's CGeom calls (jumps through the geom's vtable).
maths::Vec4* get_geom_bounding_sphere(void* geom, maths::Vec4* out)
{
    return static_cast<Geom*>(geom)->fetch_bound_sphere(out);
}
void set_geom_bounding_sphere(void* geom, const maths::Vec4* sphere)
{
    // (slot 16, through the jump 0x0048DEB0)
    reinterpret_cast<void(__thiscall*)(void*, const maths::Vec4*)>((*static_cast<void***>(geom))[16])(geom, sphere);
}

} // namespace

// 0x004BADF0
void PcModel::plat_set_bounding_sphere(const maths::Vec4* sphere)
{
    ModelGeomNode* head = geoms_;
    for (ModelGeomNode* node = head ? head->next : nullptr; node != head; node = node->next)
        set_geom_bounding_sphere(geom_of(node), sphere);
}

// 0x004BAE30
maths::Vec4* PcModel::plat_get_bounding_sphere(maths::Vec4* out)
{
    if (geom_total_ == 0) {
        *out = {0.0f, 0.0f, 0.0f, 0.0f};
        return out;
    }
    ModelGeomNode* head = geoms_;
    ModelGeomNode* node = head ? head->next : nullptr;
    maths::Vec4 scratch;
    maths::Vec4 sphere = *get_geom_bounding_sphere(geom_of(node), &scratch);
    for (node = node->next; node != head; node = node->next) {
        maths::Vec4 other = *get_geom_bounding_sphere(geom_of(node), &scratch);
        float dx = static_cast<float>(static_cast<double>(other.x) - sphere.x);
        float dy = static_cast<float>(static_cast<double>(other.y) - sphere.y);
        float dz = static_cast<float>(static_cast<double>(other.z) - sphere.z);
        float dw = static_cast<float>(static_cast<double>(other.w) - sphere.w);
        float distance = static_cast<float>(
            std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx));
        if (static_cast<double>(other.w) + distance <= sphere.w)
            continue; // (inside the one so far)
        if (static_cast<double>(sphere.w) + distance <= other.w) {
            sphere = other; // (the one so far inside it)
            continue;
        }
        // Both: the sphere around the two - its centre half way between their far sides.
        double t = static_cast<double>(dw) / distance;
        double sum_x = static_cast<double>(other.x) + sphere.x;
        double sum_y = static_cast<double>(other.y) + sphere.y;
        float sum_z = static_cast<float>(static_cast<double>(other.z) + sphere.z);
        float sum_w = static_cast<float>(static_cast<double>(other.w) + sphere.w);
        float offset_y = static_cast<float>(dy * t);
        float offset_z = static_cast<float>(dz * t);
        float x = static_cast<float>(dx * t + sum_x);
        float y = static_cast<float>(offset_y + sum_y);
        sphere.z = static_cast<float>((static_cast<double>(offset_z) + sum_z) * 0.5f);
        sphere.x = static_cast<float>(static_cast<double>(x) * 0.5f);
        sphere.y = static_cast<float>(static_cast<double>(y) * 0.5f);
        sphere.w = static_cast<float>((static_cast<double>(sum_w) + distance) * 0.5f);
    }
    out->x = sphere.x;
    out->y = sphere.y;
    out->z = sphere.z;
    out->w = sphere.w + sphere.w;
    return out;
}

} // namespace render
