// NOT ORIGINAL - the level editor's model viewer (ours). See level_editor_preview.h. No Direct3D here: the target is
// the UI backend's, the drawing the game renderer's.

#include "editor/level_editor_preview.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>

#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_backend.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/maths/mat4.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/gfx/pc_instance.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::preview {

// --- the pieces ---

void get_drag(Orbit* orbit, float dx, float dy, float radians_per_pixel)
{
    orbit->yaw -= dx * radians_per_pixel;
    orbit->pitch = std::clamp(orbit->pitch + dy * radians_per_pixel, kMinPitch, kMaxPitch);
    // (the yaw kept within a turn either way: it only ever matters as an angle)
    constexpr float kTurn = 6.2831853f;
    if (orbit->yaw > kTurn || orbit->yaw < -kTurn)
        orbit->yaw = std::fmod(orbit->yaw, kTurn);
}

void get_wheel(Orbit* orbit, float steps)
{
    orbit->zoom = std::clamp(orbit->zoom * std::pow(1.2f, steps), kMinZoom, kMaxZoom);
}

float fit_distance(float radius, float half_angle, float margin)
{
    const float s = std::sin(half_angle);
    return s > 1e-4f ? radius * margin / s : radius * margin * 1e4f;
}

void sphere_of(V3 min, V3 max, float min_radius, V3* centre, float* radius)
{
    *centre = (min + max) * 0.5f;
    const V3 half = (max - min) * 0.5f;
    *radius = std::max(min_radius, std::sqrt(get_dot(half, half)));
}

Eye orbit_eye(V3 centre, float distance, const Orbit& orbit)
{
    // (the view looks down by the eye's height angle)
    Eye eye{};
    eye.frame = frame_from_yaw_pitch(orbit.yaw, -orbit.pitch);
    eye.pos = centre + eye.frame.back * distance;
    return eye;
}

// --- the viewer ---

namespace {

constexpr uint32_t kBackground = 0xFF2A2C30;
constexpr float kRadiansPerPixel = 0.01f;

Kind g_kind = Kind::kNone;
uint32_t g_id;
Orbit g_orbit;
bool g_have_time;
std::chrono::steady_clock::time_point g_last_frame, g_last_drag;
bool g_failed_logged;

// The renderer's calls (not rewritten yet; thiscall / cdecl).
void set_material(render::MaterialRef* material) { material->hand_in(); }
void draw_mesh(render::MeshRef* mesh) { mesh->render(); }
// 0x004D16B0: one instance drawn (flag 8: not culled) - its world matrix, its bones, its lights.
void draw_instance(render::Singleton* instance) { render::draw_one_instance(instance, 8); }
// 0x004CDAA0 (thiscall on 0x00775B80): the view frustum from the camera's globals.
void update_frustum() { render::g_billboard_pool.setup_axes(); }

// The camera's globals render::choose_camera and the instance render change, saved and put back.
struct SavedRange {
    void* (*address)(); // (a function: the globals are set up after this table)
    uint32_t size;
};
constexpr auto kSaved = std::to_array<SavedRange>({
    {[]() -> void* { return &render::g_world_matrix; }, sizeof(maths::Mat4)},
    {[]() -> void* { return &render::g_view_matrix; }, sizeof(maths::Mat4)},
    {[]() -> void* { return &render::g_projection_matrix; }, sizeof(maths::Mat4)},
    {[]() -> void* { return &render::g_heading_matrix; }, sizeof(maths::Mat4)},
    {[]() -> void* { return &render::g_device_viewport; }, sizeof(render::DeviceViewport)},
    {[]() -> void* { return &render::g_camera; }, sizeof(render::RenderCamera)},
    {[]() -> void* { return &render::g_view_shape; }, sizeof(render::ViewShape)}, // (the view's shape: culling)
    {[]() -> void* { return static_cast<void*>(&render::g_world_transform); },
     sizeof(void*)}, // (the culling world matrix: none or the copy)
    {[]() -> void* { return &render::g_world_transform_copy; }, sizeof(float[16])},
});
constexpr uint32_t kSavedBytes = 4 * sizeof(maths::Mat4) + sizeof(render::DeviceViewport) +
                                 sizeof(render::RenderCamera) + sizeof(render::ViewShape) + sizeof(void*) +
                                 sizeof(float[16]);
// (the camera's globals from the world matrix to the camera's end: 0x164 bytes; the view's shape: 0x18)
static_assert(4 * sizeof(maths::Mat4) + sizeof(render::DeviceViewport) + sizeof(render::RenderCamera) == 0x164 &&
                  sizeof(render::ViewShape) == 0x18,
              "the saved camera globals");
struct SavedView {
    std::array<uint8_t, kSavedBytes> bytes;
    void store_away()
    {
        uint8_t* at = bytes.data();
        for (const SavedRange& range : kSaved) {
            std::memcpy(at, range.address(), range.size);
            at += range.size;
        }
    }
    void get_restore() const
    {
        const uint8_t* at = bytes.data();
        for (const SavedRange& range : kSaved) {
            std::memcpy(range.address(), at, range.size);
            at += range.size;
        }
    }
};

// A geometry's meshes as the world pass draws them: shown and not hidden; opaque ones first, then the transparent.
void draw_meshes(render::PcGeom* geom)
{
    auto* const* meshes = geom->meshes;
    const uint32_t count = geom->mesh_count_;
    if (!meshes)
        return;
    const render::MaterialRef* current = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < count && i < 4096; ++i) {
            render::MeshRef* mesh = meshes[i];
            if (!mesh)
                continue;
            const uint32_t flags = mesh->flags;
            if (!(flags & 0x40) || (flags & 0x210))
                continue;
            render::MaterialRef* material = mesh->material;
            const bool transparent = material && (core::at(material->pass_flags_, 0) & 0x40);
            if (transparent != (pass == 1))
                continue;
            if (material != current) {
                current = material;
                set_material(material);
            }
            draw_mesh(mesh);
        }
    }
}

// An object's geometry: through its instance when it has one, else its meshes.
void draw_geometry(render::PcGeom* geom)
{
    if (auto* instance = geom->instance)
        draw_instance(instance);
    else
        draw_meshes(geom);
}

// A subject's box where it is (false: gone).
bool box_of(Kind kind, uint32_t id, OrientedBox* box)
{
    if (kind == Kind::kSector)
        return scenery::sector_geometry(id) && scenery::cuboid(id, box);
    if (kind != Kind::kObject)
        return false;
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) && object_edit::model_box(object, box);
}
bool subject_box(OrientedBox* box) { return box_of(g_kind, g_id, box); }

void draw_subject(Kind kind, uint32_t id)
{
    if (kind == Kind::kSector) {
        if (render::PcGeom* geom = scenery::sector_geometry(id))
            draw_meshes(geom);
        return;
    }
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    for (int i = 0; object && i < 16; ++i) {
        auto* geom = static_cast<render::PcGeom*>(object_edit::get_geometry(object, i));
        if (!geom)
            break;
        if (scenery::tintable(geom)) // (the PC geometry class: its fields as read here)
            draw_geometry(geom);
    }
}

} // namespace

void get_show(Kind kind, uint32_t id)
{
    if (kind == g_kind && id == g_id)
        return;
    g_kind = kind;
    g_id = id;
    g_orbit = Orbit{};
    g_last_drag = {};
}

void intake(float dx, float dy, float wheel)
{
    if (dx != 0.0f || dy != 0.0f) {
        get_drag(&g_orbit, dx, dy, kRadiansPerPixel);
        g_last_drag = std::chrono::steady_clock::now();
    }
    if (wheel != 0.0f)
        get_wheel(&g_orbit, wheel);
}

uint64_t render(int size)
{
    auto now = std::chrono::steady_clock::now();
    const double elapsed = g_have_time ? std::chrono::duration<double>(now - g_last_frame).count() : 0.0;
    g_last_frame = now;
    g_have_time = true;
    if (g_kind == Kind::kNone)
        return 0;
    // The slow turn (paused for a while after a drag).
    if (std::chrono::duration<double>(now - g_last_drag).count() > kTurnWait)
        g_orbit.yaw += kTurnSpeed * static_cast<float>(std::min(elapsed, 0.1));

    OrientedBox box{};
    if (!subject_box(&box))
        return 0;
    if (!ui_backend::begin_offscreen(size, kBackground)) {
        if (!g_failed_logged)
            get_log("level editor: the Add palette's preview has no target - shown without one");
        g_failed_logged = true;
        return 0;
    }
    draw_view(g_kind, g_id, g_orbit);
    ui_backend::end_offscreen();
    return ui_backend::offscreen_texture();
}

bool draw_view(Kind kind, uint32_t id, const Orbit& orbit)
{
    OrientedBox box{};
    if (!box_of(kind, id, &box))
        return false;
    V3 min{}, max{};
    bounds_of(box, &min, &max);
    V3 centre{};
    float radius = 0.0f;
    sphere_of(min, max, 4.0f, &centre, &radius);
    const Lens lens = lens_for(kAngle, kSquareAspect);
    const float half_angle = std::atan(std::min(lens.tan_x_, lens.tan_y_));
    const Eye eye = orbit_eye(centre, fit_distance(radius, half_angle, kMargin) / orbit.zoom, orbit);
    SavedView saved{};
    saved.store_away();
    maths::Mat4 orientation = {};
    core::at(orientation.row, 0) = {eye.frame.right.x, eye.frame.right.y, eye.frame.right.z, 0.0f};
    core::at(orientation.row, 1) = {eye.frame.up.x, eye.frame.up.y, eye.frame.up.z, 0.0f};
    core::at(orientation.row, 2) = {eye.frame.back.x, eye.frame.back.y, eye.frame.back.z, 0.0f};
    core::at(orientation.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    const maths::Vec4 position = {eye.pos.x, eye.pos.y, eye.pos.z, 1.0f};
    render::choose_camera(&orientation, &position, kAngle, kSquareAspect, 1.0f, false);
    render::set_world_transform(nullptr);
    render::change_phase(render::kZWrite, 1);
    render::change_phase(render::kZTest, 1);
    draw_subject(kind, id);
    // Put back: the camera's globals, the frustum from them, no instance's lights taken as set (0x007825BC: the
    // next instance sets its own); the device's target and transforms are the caller's (the backend's) to put back.
    saved.get_restore();
    update_frustum();
    render::g_last_bones = nullptr;
    return true;
}

void conceal()
{
    g_kind = Kind::kNone;
    g_id = 0;
    g_have_time = false;
    ui_backend::free_offscreen();
}

void tick_cycle(ui::ShapeMesh* model)
{
    ui::ShapeMesh::PreviewView& view = model->preview;
    const std::vector<ui::BrowserItem>* items = model->browser_items_;
    if (!model->browser || !items || view.index < 0 || view.index >= ints::as_signed(items->size())) {
        if (g_kind != Kind::kNone)
            conceal();
        return;
    }
    const uint32_t id = core::at((*items), view.index).id;
    get_show(model->browser_tab_ == 0 ? Kind::kSector : Kind::kObject, id);
    view.name = object_edit::caption(id);
    OrientedBox box{};
    if (subject_box(&box)) {
        std::array<char, 96> size{};
        std::snprintf(size.data(), sizeof(size), "%.0f x %.0f x %.0f  (x by y by z)", box.max.x - box.min.x,
                      box.max.y - box.min.y, box.max.z - box.min.z);
        view.size = size.data();
    }
    if (g_kind == Kind::kSector) {
        view.type = "scene sector (scenery)";
    } else {
        view.type = object_edit::class_of(id);
        const std::string file = object_edit::get_model_file(id);
        if (!file.empty())
            view.type += "  -  " + file;
    }
    view.texture = render(ui::preview_pixels());
}

} // namespace frontend::level_editor::preview
