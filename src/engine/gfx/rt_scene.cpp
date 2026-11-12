// NOT ORIGINAL - ray tracing's contract (rt_scene.h): the installed backend, the frame record and the ids.
#include "engine/gfx/rt_scene.h"

#include "engine/gfx/gpu.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace render::rt {

namespace {
RayTracing*& backend_slot()
{
    static RayTracing* backend = nullptr;
    return backend;
}
} // namespace

RayTracing* ray_tracing() { return backend_slot(); }
void set_ray_tracing(RayTracing* backend) { backend_slot() = backend; }

FrameRecord& frame_record()
{
    static FrameRecord record;
    return record;
}

MeshId next_mesh_id()
{
    static MeshId last = 0;
    return ++last;
}

MaterialId next_material_id()
{
    static MaterialId last = 0;
    return ++last;
}

namespace {
std::vector<ViewListener*>& view_listeners()
{
    static std::vector<ViewListener*> listeners;
    return listeners;
}
} // namespace

void add_view_listener(ViewListener* listener)
{
    std::vector<ViewListener*>& listeners = view_listeners();
    if (listener != nullptr && std::ranges::find(listeners, listener) == listeners.end()) {
        // (by order(); after those of the same order - as added)
        const auto at = std::ranges::find_if(
            listeners, [&](const ViewListener* other) { return other->order() > listener->order(); });
        listeners.insert(at, listener);
    }
}

void remove_view_listener(ViewListener* listener) { std::erase(view_listeners(), listener); }

void opaque_view_drawn(const ViewDrawn& view)
{
    for (ViewListener* listener : view_listeners()) {
        listener->opaque_drawn(view);
    }
}

void world_view_drawn(const ViewDrawn& view)
{
    for (ViewListener* listener : view_listeners()) {
        listener->world_drawn(view);
    }
}

bool view_listened() { return !view_listeners().empty(); }

namespace {
bool& hdr_slot()
{
    static bool on = false;
    return on;
}
} // namespace

bool hdr_colour() { return hdr_slot(); }
void set_hdr_colour(bool on) { hdr_slot() = on; }

uint32_t world_target_format(uint32_t format)
{
    if (!hdr_colour()) {
        return format;
    }
    if (format == gpu_value::kFormatA8R8G8B8) {
        return gpu_value::kFormatA16B16G16R16F;
    }
    if (format == gpu_value::kFormatX8R8G8B8) {
        return gpu_value::kFormatX16B16G16R16F;
    }
    return format;
}

} // namespace render::rt
