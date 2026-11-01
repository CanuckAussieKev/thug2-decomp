// The engine's shadows: see engine_shadows.h. The x87 code is followed: products kept unrounded where the original
// keeps them on its stack, rounded to floats where it stores them.

#include "engine/gfx/engine_shadows.h"

#include <memory>
#include <cstdint>
#include <cstring>

#include "engine/gfx/engine_texture.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/lists/key_map.h"

namespace render {

namespace {

ShadowVolume** shadow_volumes() { return g_shadow_volumes; }
lists::KeyMap& edge_table() { return g_shadow_edge_table; }

// The engine's model calls not rewritten yet: the shadow caster added for a render target (0x0048C870), a model
// deleted (0x0048C3C0), a file name with its extension (0x00487A70).
void add_shadow_caster(uint8_t* texture, RenderModel* model, int unused)
{
    render::add_shadow_caster(texture, model, reinterpret_cast<WorldScene*>(unused));
}
void delete_model(RenderModel* model) { delete model; }
void add_extension(const char* name, const char* extension, char* out)
{
    gfx::make_model_file_name(name, extension, out);
}

// A new camera (null without memory: the original goes on with it) at the origin; turned to look along a direction
// (normalised) by look_along: rows (z, -x, -y), (y, -z, -x), the direction.
gfx::ViewCam* new_shadow_camera()
{
    auto* camera = new gfx::ViewCam;
    camera->change_location(maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    return camera;
}
void look_along(gfx::ViewCam* camera, maths::Vec4 v)
{
    maths::Mat4 matrix;
    matrix.row[0] = {v.z, -v.x, -v.y, 0.0f};
    matrix.row[1] = {v.y, -v.z, -v.x, 0.0f};
    matrix.row[2] = v;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    camera->change_mat(matrix);
}
// The camera's place for a shadow at `position`: `distance` back along the light (the x product left unrounded).
maths::Vec4 back_along_light(const maths::Vec4& position, const maths::Vec4& direction, float distance)
{
    double d = distance;
    return {static_cast<float>(position.x - d * direction.x),
            static_cast<float>(position.y - static_cast<float>(d * direction.y)),
            static_cast<float>(position.z - static_cast<float>(d * direction.z)),
            static_cast<float>(position.w - static_cast<float>(d * direction.w))};
}

} // namespace

// 0x004A19C0
FullShadow::FullShadow(RenderModel* model, maths::Vec4 direction, float distance) : ShadowBase(1)
{
    bitmap_ = static_cast<uint8_t*>(create_render_target(0x100, 0x100, 0x10, 0x10));
    add_shadow_caster(bitmap_, model, 0);
    reach_ = distance;
    direction_ = direction;
    direction_.make_unit();
    view_camera_.reset(new_shadow_camera());
    maths::Vec4 v = direction_;
    v.make_unit();
    look_along(view_camera_.get(), v);
    model->activate_shadow(1);
}

// 0x004A16E0
FullShadow::~FullShadow()
{
    remove_texture_shadow_caster(bitmap_);
    delete reinterpret_cast<BitmapTexture*>(bitmap_);
    view_camera_.reset();
}

// 0x004A1910
void FullShadow::change_place(maths::Vec4 position)
{
    view_camera_->change_location(back_along_light(position, direction_, reach_));
    aim_texture_shadow_caster(bitmap_, view_camera_.get());
}

// 0x004A1BA0
void FullShadow::set_light_direction(const maths::Vec4* direction)
{
    maths::Vec4 position = *view_camera_->fetch_location();
    direction_ = *direction;
    direction_.make_unit();
    view_camera_->change_location(maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    look_along(view_camera_.get(), direction_);
    view_camera_->change_location(position);
    aim_texture_shadow_caster(bitmap_, view_camera_.get());
}

// 0x004A1750
BlobShadow::BlobShadow() : ShadowBase(2), mesh_instance_(nullptr), size_factor_(1.0f), shift_(1.0f) {}

// 0x004A1780
BlobShadow::~BlobShadow()
{
    if (mesh_instance_) {
        delete_model(mesh_instance_);
        mesh_instance_ = nullptr;
    }
}

// 0x004A17E0
void BlobShadow::load_mesh(const char* name)
{
    if (mesh_instance_) {
        delete_model(mesh_instance_);
        mesh_instance_ = nullptr;
    }
    mesh_instance_ = spawn_model();
    char file_name[0x200] = {};
    add_extension(name, ".mdl", file_name);
    mesh_instance_->add_file_geom(file_name, 0, true, 0, false, false, true, false);
}

// 0x004A1CD0
void BlobShadow::tick_frame(const maths::Vec4* position, const maths::Mat4* matrix, maths::Vec4 normal)
{
    const maths::Vec4& forward = matrix->row[2];
    maths::Mat4 placed;
    placed.row[0] = {
        static_cast<float>(static_cast<double>(forward.z) * normal.y - static_cast<double>(forward.y) * normal.z),
        static_cast<float>(static_cast<double>(forward.x) * normal.z - static_cast<double>(forward.z) * normal.x),
        static_cast<float>(static_cast<double>(forward.y) * normal.x - static_cast<double>(forward.x) * normal.y),
        0.0f};
    placed.row[1] = normal;
    placed.row[2] = forward;
    placed.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    placed.scale_tool(maths::Vec4{size_factor_, size_factor_, size_factor_, 1.0f});
    double offset = shift_;
    float dx = static_cast<float>(normal.x * offset);
    float dy = static_cast<float>(normal.y * offset);
    float dz = static_cast<float>(normal.z * offset);
    placed.row[3] = {static_cast<float>(static_cast<double>(dx) + position->x),
                     static_cast<float>(static_cast<double>(dy) + position->y),
                     static_cast<float>(static_cast<double>(dz) + position->z),
                     static_cast<float>(offset * normal.w + position->w)};
    if (mesh_instance_)
        mesh_instance_->render(&placed, true, nullptr);
}

// 0x004A1880
void BlobShadow::hide()
{
    if (mesh_instance_)
        mesh_instance_->change_live(0);
}

// 0x004A1890
void BlobShadow::show()
{
    if (mesh_instance_)
        mesh_instance_->change_live(1);
}

// 0x004A1F80
ShadowVolume::ShadowVolume(RenderModel*, maths::Vec4 direction, float distance) : ShadowBase(3)
{
    reach_ = distance;
    direction_ = direction;
    direction_.make_unit();
    view_camera_.reset(new_shadow_camera());
    maths::Vec4 v = direction_;
    v.make_unit();
    look_along(view_camera_.get(), v);
    model_key_ = 0;
    edges_ = nullptr;
}

// 0x004A2120
ShadowVolume::~ShadowVolume()
{
    view_camera_.reset();
    if (edges_) {
        if (edges_->count) {
            remove_shadow_volume(this);
            release_shadow_edges(model_key_);
        }
        edges_ = nullptr;
    }
}

// 0x004A1E70
void ShadowVolume::change_place(maths::Vec4 position)
{
    view_camera_->change_location(back_along_light(position, direction_, reach_));
}

// 0x004A21A0
void ShadowVolume::set_light_direction(const maths::Vec4* direction)
{
    maths::Vec4 position = *view_camera_->fetch_location();
    direction_ = *direction;
    direction_.make_unit();
    view_camera_->change_location(maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    look_along(view_camera_.get(), direction_);
    view_camera_->change_location(position);
}

// 0x004A1EE0
void ShadowVolume::change_mat(const maths::Mat4* matrix) { basis_frame_ = *matrix; }

// 0x004A1E40
void remove_shadow_volume(ShadowVolume* volume)
{
    for (int i = 0; i < 16; ++i) {
        if (shadow_volumes()[i] == volume) {
            shadow_volumes()[i] = nullptr;
            return;
        }
    }
}

// 0x004A1EF0
ShadowEdges* find_shadow_edges(uint32_t key) { return static_cast<ShadowEdges*>(edge_table().fetch_entry(key)); }

// 0x004A1F30
void release_shadow_edges(uint32_t key)
{
    ShadowEdges* edges = find_shadow_edges(key);
    if (!edges || --edges->users != 0)
        return;
    if (edges->edges) {
        memory::Hub::get_pull()->dealloc(edges->edges);
        edges->edges = nullptr;
    }
    edge_table().erase_entry(key);
    memory::Hub::get_pull()->dealloc(edges);
}

} // namespace render
