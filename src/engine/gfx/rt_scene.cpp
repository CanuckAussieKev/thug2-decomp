// NOT ORIGINAL - ray tracing's contract (rt_scene.h): the installed backend, the frame record and the ids.
#include "engine/gfx/rt_scene.h"

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

} // namespace render::rt
