// NOT ORIGINAL - the level editor's model viewer (ours). See level_editor_preview.h. No Direct3D here: the target is
// the UI backend's, the drawing the game renderer's.

#include "editor/level_editor_preview.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <array>

#include "editor/level_editor.h"
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
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/maths/vec4.h"
#include "engine/core/packed_colour.h"
#include "engine/maths/vec2.h"

namespace frontend::level_editor::preview {

namespace {

constexpr float kZoomStep = 1.2f; // the orbit's zoom change per wheel step
constexpr float kTinySine = 1e-4f; // below this the framing angle is too narrow to divide by
constexpr float kTinySineReciprocal = 1e4f; // and the distance is capped to this multiple
constexpr int kNotCulledFlag = 8; // draw_one_instance's flag: not culled
constexpr uint32_t kMeshDrawnFlag = 0x40; // the mesh flag a drawn mesh has
constexpr uint32_t kMeshSkipFlags = 0x210; // the mesh flags that keep a mesh out of the preview
constexpr double kLongestFrameSeconds = 0.1; // the orbit's turn uses at most this much of a frame
constexpr float kBoundsPadding = 4.0f; // added around the object's bounds
constexpr std::size_t kSizeTextSize = 96; // the "w x h x d" line

} // namespace

namespace {

constexpr float kHalf = 0.5f; // half

} // namespace

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
    orbit->zoom = std::clamp(orbit->zoom * std::pow(kZoomStep, steps), kMinZoom, kMaxZoom);
}

float fit_distance(FitAt fit_at)
{
    const auto radius = fit_at.radius;
    const auto half_angle = fit_at.half_angle_;
    const auto margin = fit_at.margin;
    const float s = std::sin(half_angle);
    return s > kTinySine ? radius * margin / s : radius * margin * kTinySineReciprocal;
}

void sphere_of(V3 min, V3 max, float min_radius, V3* centre, float* radius)
{
    *centre = (min + max) * kHalf;
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

// NOT ORIGINAL grouping - level_editor_preview.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_preview_state()).
struct LevelEditorPreviewState {
    Kind kind_ = Kind::kNone;
    uint32_t id_{};
    Orbit orbit_{};
    bool have_time_{};
    std::chrono::steady_clock::time_point last_frame_{};
    std::chrono::steady_clock::time_point last_drag_{};
    bool failed_logged_{};
};

LevelEditorPreviewState& level_editor_preview_state()
{
    static core::NoDestroy<LevelEditorPreviewState> state;
    return state.get();
}

// The renderer's calls (not rewritten yet; thiscall / cdecl).
void set_material(render::MaterialRef* material) { material->hand_in(); }
void draw_mesh(render::MeshRef* mesh) { mesh->render(); }
// 0x004D16B0: one instance drawn (flag 8: not culled) - its world matrix, its bones, its lights.
void draw_instance(render::Singleton* instance) { render::draw_one_instance(instance, kNotCulledFlag); }
// 0x004CDAA0 (thiscall on 0x00775B80): the view frustum from the camera's globals.
void update_frustum() { render::billboard_pool().setup_axes(); }

// A geometry's meshes as the world pass draws them: shown and not hidden; opaque ones first, then the transparent.
void draw_meshes(render::PcGeom* geom)
{
    const std::span<render::MeshRef*> meshes = geom->mesh_run();
    const uint32_t count = geom->mesh_count();
    if (!geom->meshes())
        return;
    const render::MaterialRef* current = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < count && i < kMeshScanLimit; ++i) {
            render::MeshRef* mesh = core::at(meshes, i);
            if (!mesh)
                continue;
            const uint32_t flags = mesh->flags();
            if (!(flags & kMeshDrawnFlag) || (flags & kMeshSkipFlags))
                continue;
            render::MaterialRef* material = mesh->material();
            const bool transparent = material && (core::at(material->pass_flags(), 0) & 0x40);
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
    if (auto* instance = geom->instance())
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
    return object && !(object->object_flags() & objects::kObjectFlagDead) && object_edit::model_box(object, box);
}
bool subject_box(OrientedBox* box) { return box_of(level_editor_preview_state().kind_, level_editor_preview_state().id_, box); }

void draw_subject(Kind kind, uint32_t id)
{
    if (kind == Kind::kSector) {
        if (render::PcGeom* geom = scenery::sector_geometry(id))
            draw_meshes(geom);
        return;
    }
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    for (int i = 0; object && i < kMaxGeometries; ++i) {
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
    if (kind == level_editor_preview_state().kind_ && id == level_editor_preview_state().id_)
        return;
    level_editor_preview_state().kind_ = kind;
    level_editor_preview_state().id_ = id;
    level_editor_preview_state().orbit_ = Orbit{};
    level_editor_preview_state().last_drag_ = {};
}

void intake(maths::Vec2 delta, float wheel)
{
    const auto dx = delta.x;
    const auto dy = delta.y;
    if (dx != 0.0f || dy != 0.0f) {
        get_drag(&level_editor_preview_state().orbit_, dx, dy, kRadiansPerPixel);
        level_editor_preview_state().last_drag_ = std::chrono::steady_clock::now();
    }
    if (wheel != 0.0f)
        get_wheel(&level_editor_preview_state().orbit_, wheel);
}

uint64_t render(int size)
{
    auto now = std::chrono::steady_clock::now();
    const double elapsed = level_editor_preview_state().have_time_ ? std::chrono::duration<double>(now - level_editor_preview_state().last_frame_).count() : 0.0;
    level_editor_preview_state().last_frame_ = now;
    level_editor_preview_state().have_time_ = true;
    if (level_editor_preview_state().kind_ == Kind::kNone)
        return 0;
    // The slow turn (paused for a while after a drag).
    if (std::chrono::duration<double>(now - level_editor_preview_state().last_drag_).count() > kTurnWait)
        level_editor_preview_state().orbit_.yaw += kTurnSpeed * static_cast<float>(std::min(elapsed, kLongestFrameSeconds));

    OrientedBox box{};
    if (!subject_box(&box))
        return 0;
    if (!ui_backend::begin_offscreen(size, colour::Packed{kBackground})) {
        if (!level_editor_preview_state().failed_logged_)
            get_log("level editor: the Add palette's preview has no target - shown without one");
        level_editor_preview_state().failed_logged_ = true;
        return 0;
    }
    draw_view(level_editor_preview_state().kind_, level_editor_preview_state().id_, level_editor_preview_state().orbit_);
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
    sphere_of(min, max, kBoundsPadding, &centre, &radius);
    const Lens lens = lens_for({.angle_degrees_ = kAngle, .aspect = kSquareAspect});
    const float half_angle = std::atan(std::min(lens.tan_x_, lens.tan_y_));
    const Eye eye = orbit_eye(centre, fit_distance({.radius = radius, .half_angle_ = half_angle, .margin = kMargin}) / orbit.zoom, orbit);
    // (the camera's state render::choose_camera and the instance render change, saved and put back)
    const render::ViewSnapshot saved = render::renderer().save_view();
    maths::Mat4 orientation = {};
    core::at(orientation.row, 0) = {.x = eye.frame.right.x, .y = eye.frame.right.y, .z = eye.frame.right.z, .w = 0.0f};
    core::at(orientation.row, 1) = {.x = eye.frame.up.x, .y = eye.frame.up.y, .z = eye.frame.up.z, .w = 0.0f};
    core::at(orientation.row, 2) = {.x = eye.frame.back.x, .y = eye.frame.back.y, .z = eye.frame.back.z, .w = 0.0f};
    core::at(orientation.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    const maths::Vec4 position = {.x = eye.pos.x, .y = eye.pos.y, .z = eye.pos.z, .w = 1.0f};
    render::choose_camera(&orientation, &position, {.angle = kAngle, .aspect = kSquareAspect, .zoom = 1.0f}, false);
    render::set_world_transform(nullptr);
    render::change_phase(render::kZWrite, 1);
    render::change_phase(render::kZTest, 1);
    draw_subject(kind, id);
    // Put back: the camera's globals, the frustum from them, no instance's lights taken as set (0x007825BC: the
    // next instance sets its own); the device's target and transforms are the caller's (the backend's) to put back.
    render::renderer().restore_view(saved);
    update_frustum();
    render::set_last_bones(nullptr);
    return true;
}

void conceal()
{
    level_editor_preview_state().kind_ = Kind::kNone;
    level_editor_preview_state().id_ = 0;
    level_editor_preview_state().have_time_ = false;
    ui_backend::free_offscreen();
}

void tick_cycle(ui::ShapeMesh* model)
{
    ui::ShapeMesh::PreviewView& view = model->preview;
    const std::vector<ui::BrowserItem>* items = model->browser_items_;
    if (!model->browser || !items || view.index < 0 || view.index >= ints::as_signed(items->size())) {
        if (level_editor_preview_state().kind_ != Kind::kNone)
            conceal();
        return;
    }
    const uint32_t id = core::at((*items), view.index).id;
    get_show(model->browser_tab_ == 0 ? Kind::kSector : Kind::kObject, id);
    view.name = object_edit::caption(id);
    OrientedBox box{};
    if (subject_box(&box)) {
        std::array<char, kSizeTextSize> size{};
        text::format_to(size, "%.0f x %.0f x %.0f  (x by y by z)", box.max.x - box.min.x, box.max.y - box.min.y,
                        box.max.z - box.min.z);
        view.size = size.data();
    }
    if (level_editor_preview_state().kind_ == Kind::kSector) {
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
