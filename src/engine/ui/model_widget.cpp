// The front end's 3D model screen element (Element3d) - and, with thug2-decomp.ini [features] widescreen_menus,
// the widescreen front end's rule for the ones spanning the 4:3 menu layout (widescreen_stretch).

#include "engine/ui/model_widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>
#include <span>

#include "engine/gfx/view_cam.h"
#include "engine/gfx/model.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/viewport.h"
#include "base/maths/mat4.h"
#include "base/maths/trig.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_constants.h"
#include "engine/gfx/engine_geom.h"
#include "engine/objects/model_component.h"
#include "base/core/down_cast.h"
#include "base/maths/shape_tests.h"
#include "base/core/checked_index.h"
#include "engine/script/degree_ratio.h"
#include "engine/gfx/renderer.h"
#include "base/maths/vec2.h"
#include <gsl/pointers>

namespace frontend {

namespace {

constexpr float kHalf = 0.5F;
constexpr int kAngleOrderCount = 6;            // the Euler orders a model can be turned in
constexpr int kAngleOrderZYX = 5;              // the order the sixth bare flag selects: spin z, turn y, spin x
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr int kBoxCorners = 8;                 // the corners of a bounding box

using script::Struct;

// Names of the properties (checksums of the names; two weren't recovered).
constexpr uint32_t kSkeletonName = 0x09794932, kCloneModel = 0x073EC4C8, kModel = 0x286A8D26, kModels = 0x1B29CFF6;
constexpr uint32_t kSector = 0xB45C2617, kSectors = 0x4A6BF967, kClass = 0x12B4E660, kLevelObject = 0xB7B3BD86;
constexpr uint32_t kName = 0xA1DC81F9, kCameraZ = 0xED7C6031;
constexpr uint32_t kAngleX = 0x0AFFD09D, kAngleY = 0x7DF8E00B, kAngleZ = 0xE4F1B1B1;
constexpr uint32_t kAngVelX = 0xC1608989, kAngVelY = 0xB667B91F, kAngVelZ = 0x2F6EE8A5;
constexpr std::array<uint32_t, 6> kAngleOrders = {{0x233F6B18, 0x911B6961, 0x3BE6306E, 0xE7DE33C0, 0xA0A9DF8D, 0xCEB5DE5A}};
constexpr uint32_t kPointAtNothingFlag = 0xF01FDCA4; // (name not recovered)
constexpr uint32_t kNodeToPointTo = 0xB2A86EB5, kObjectToPointTo = 0xDAD39B88, kTilt = 0xE3C07609;
constexpr uint32_t kHoverParams = 0x00CA1C41, kAmp = 0xC9FDE32C, kFreq = 0xA80BEA4A;
constexpr uint32_t kOwnerArgs = 0x36388933, kSkater = 0x5B8AB877, kNode = 0x7A8017BA;
constexpr uint32_t kScaleToScreenDims = 0x1B399065, kShrinkWithRange = 0x4564C8F2;
constexpr uint32_t kDistanceScale = 0x571F3F3A; // (name not recovered)
constexpr uint32_t kMaxScale = 0x31E78F98, kTargetViewport = 0x655EE08E;
constexpr uint32_t kElement3d = 0x82C59D78;
// The scripts run on each new model's object, and the one run when a ParentParams object is gone.
constexpr uint32_t kAddComponentsScript = 0xDF835908; // element3d_add_components
constexpr uint32_t kInitModelScript = 0xFA03586F;     // element3d_init_model
constexpr uint32_t kKillScript = 0xA66C1C31;          // KillElement3d
// The model component's type.
constexpr uint32_t kModelModuleName = 0x286A8D26;

// The originals' float constants (read from the exe).
float aspect_to_layout() { return frontend::kAspectToLayout; } // 0.75000006 (3/4, a bit over)
float wide_screen() { return frontend::kWideScreen; }           // 0.9
float minus_inv448() { return frontend::kMinusInv448; }         // -1/448
float inv448() { return frontend::kInv448; }                     // 1/448
float inv896() { return frontend::kInv896; }                     // 1/896
float inv1280() { return frontend::kInv1280; }                   // 1/1280
float inv_two_pi() { return frontend::kInvTwoPi; }
float two_pi() { return frontend::kTwoPi; }
float get_pi() { return maths::kPi; }
float get_degrees() { return ::commands::kDegreeToRadianRatio; } // pi / 180
float visible_alpha() { return kTinyAlpha; }      // 0.0001

// UiWidget's pieces.
void base_set_properties(UiWidget* widget, Struct* params) { widget->UiWidget::change_traits(params); }
void base_set_morph(UiWidget* widget, Struct* params) { widget->UiWidget::blend_towards(params); }

// The viewport's camera (0x004A09C0, cdecl; null if none) and the camera's matrix, position and horizontal angle
// in degrees (thiscall getters).
void* viewport_camera(int viewport) { return render::fetch_live_camera(viewport); }
const maths::Mat4* cam_orientation(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_mat(); }
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
float camera_angle(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_effective_h_fov(); }

// A geom's bounding box (0x0048DE80, thiscall).
const maths::BBox* geometry_bounding_box(void* geometry)
{
    return static_cast<render::Geom*>(geometry)->fetch_bounds();
}

// A model's model (the model component's +0x208) and that model's bounding box (min x y z w, max x y z w): the
// model's 0x00493CD0(0), then its 0x0048DE80 (thiscall).
const maths::BBox* model_bounding_box(objects::ObjectModule* component)
{
    render::RenderModel* model = core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get();
    void* geometry = model->get_geom_by_name(0);
    return geometry_bounding_box(geometry);
}
// The same, or null when the model has no geometry yet (the widescreen rule asks every frame, for every element).
const maths::BBox* model_bounding_box_if_any(objects::ObjectModule* component)
{
    render::RenderModel* model = core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get();
    if (!model) {
        return nullptr;
    }
    void* geometry = model->get_geom_by_name(0);
    if (!geometry) {
        return nullptr;
    }
    return geometry_bounding_box(geometry);
}
void set_model_viewports(objects::ObjectModule* component, uint32_t mask)
{
    core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance()->toggle_shown(mask);
}

// The node `name`'s structure (0x0045C1B0, asserting) and its position (0x0045C050).
Struct* node_structure(uint32_t name) { return level_nodes::fetch_vertex(name, true); }
void node_position(Struct* node, maths::Vec4* out) { level_nodes::fetch_place(node, out); }

// The composite objects' calls used here.
void mark_object_dead(objects::GameEntity* object) { object->flag_as_dead(); }
void seal(objects::GameEntity* object) { object->seal(); }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// An angle plus its velocity, brought within one turn (the turns truncated towards zero, so the sign is kept).
float wrapped(double angle)
{
    const int turns = get_truncated(angle * inv_two_pi());
    return static_cast<float>(angle - (static_cast<double>(turns) * two_pi()));
}

} // namespace

// 0x004A6060 (the models' smart pointers start empty: 0x0042B580)
ModelWidget::ModelWidget()
{
    for (ShapeMesh& model : models_) {
        model.offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    }
    core::at(angle_, 0) = core::at(angle_, 1) = core::at(angle_, 2) = 0.0F;
    core::at(angular_velocity_, 0) = core::at(angular_velocity_, 1) = core::at(angular_velocity_, 2) = 0.0F;
    target_position_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    basis_frame_.reset_to_identity();
    pivot_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    centre_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    // (not set here: the hover, ParentParams' offset and node - change_traits sets them)
    change_species(kElement3d);
}

// 0x004A6240 (then the models' smart pointers - the array's destructor, last to first - and UiWidget's)
ModelWidget::~ModelWidget() { clear_models(); }

// 0x004A5E30
void ModelWidget::clear_models()
{
    for (ShapeMesh& model : models_) {
        if (model.object) {
            model.object->conceal(true);
            mark_object_dead(model.object.get_pull());
        }
        model.offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    }
    model_total_ = 0;
}

// 0x004A5E80
void ModelWidget::apply_viewport()
{
    const uint32_t mask = 1U << (view_slot_ & 31);
    for (int i = 0; i < model_total_; ++i) {
        if (core::at(models_, i).object) {
            set_model_viewports(core::at(models_, i).object->fetch_module(kModelModuleName), mask);
        }
    }
}

// 0x004A6540
void ModelWidget::attach_model(Struct* params, maths::Vec4 offset)
{
    gsl::owner<objects::GameEntity*> object = objects::object_hub_slot()->create_composite_object();
    core::at(models_, model_total_).object.assign_value(object);
    script::launch_script(kAddComponentsScript, params, core::at(models_, model_total_).object.get_pull(), false, nullptr);
    seal(core::at(models_, model_total_).object.get_pull());
    script::launch_script(kInitModelScript, params, core::at(models_, model_total_).object.get_pull(), false, nullptr);
    core::at(models_, model_total_).offset = offset;
    ++model_total_;
}

// 0x004A62B0. The models' bounding boxes (each moved by its offset) together; the scale is the smaller of the two
// that make the box's width (or depth, if larger) the element's width and its height the element's height, at
// CameraZ, through the layout's perspective factors - the vertical one uses the width too (sic).
void ModelWidget::scale_to_screen_dims()
{
    // (left uninitialised by the original when the first slot is empty)
    float min_x = 0;
    float min_y = 0;
    float min_z = 0;
    float max_x = 0;
    float max_y = 0;
    float max_z = 0;
    for (int i = 0; i < model_total_; ++i) {
        const ShapeMesh& model = core::at(models_, i);
        if (!model.object) {
            continue;
        }
        const maths::BBox* box = model_bounding_box(model.object->fetch_module(kModelModuleName));
        const double low_x = static_cast<double>(box->min.x) + model.offset.x; // (compared unrounded)
        const float low_y = box->min.y + model.offset.y;
        const float low_z = box->min.z + model.offset.z;
        const float high_x = box->max.x + model.offset.x;
        const float high_y = box->max.y + model.offset.y;
        const float high_z = box->max.z + model.offset.z;
        if (i == 0) {
            min_x = static_cast<float>(low_x);
            min_y = low_y;
            min_z = low_z;
            max_x = high_x;
            max_y = high_y;
            max_z = high_z;
            continue;
        }
        if (low_x < min_x) {
            min_x = static_cast<float>(low_x);
        }
        min_y = std::min(min_y, low_y);
        min_z = std::min(min_z, low_z);
        max_x = std::max(max_x, high_x);
        max_y = std::max(max_y, high_y);
        max_z = std::max(max_z, high_z);
    }
    const double extent_x = static_cast<double>(max_x) - min_x;
    const float extent_y = max_y - min_y;
    const float extent_z = max_z - min_z;
    const double extent = extent_x > extent_z ? extent_x : extent_z;
    const double width = span();
    const double z = camera_z_;
    auto horizontal = static_cast<float>(width * z / (((width * inv1280()) + 1.0) * render::kLayoutWidth) / extent);
    double vertical = z * level_y() / (((width * inv896()) + 1.0) * render::kElementLayoutHeight) / extent_y;
    horizontal = -horizontal;
    vertical = -vertical;
    fit_scale_ = static_cast<float>(horizontal < vertical ? horizontal : vertical);
    centre_.w = 0.0F;
    centre_.x = static_cast<float>((static_cast<double>(min_x) + max_x) * -kHalf);
    centre_.y = static_cast<float>((static_cast<double>(max_y) + min_y) * -kHalf);
    centre_.z = static_cast<float>((static_cast<double>(max_z) + min_z) * -kHalf);
}

// 0x004A6630
void ModelWidget::change_traits(Struct* params)
{
    base_set_properties(this, params);
    maths::Vec4 offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (params->has_entry_named(kSkeletonName) || params->has_entry_named(kCloneModel) ||
        params->has_entry_named(kModel) || params->has_entry_named(kModels)) {
        clear_models();
        attach_model(params, offset);
    } else if (params->has_entry_named(kSector)) {
        // level geometry: a LevelObject of that sector
        clear_models();
        params->put_name_key(checksums::Name{kClass}, kLevelObject);
        uint32_t sector = 0;
        params->read_hash_value(kSector, &sector, 1);
        params->put_name_key(checksums::Name{kName}, sector);
        attach_model(params, offset);
    } else if (params->has_entry_named(kSectors)) {
        // several, each {sector (offset)}
        clear_models();
        params->put_name_key(checksums::Name{kClass}, kLevelObject);
        script::Array* sectors = nullptr;
        params->fetch_list(kSectors, &sectors, 0);
        for (uint32_t i = 0; i < sectors->extent(); ++i) {
            auto* entry = sectors->struct_at(i);
            uint32_t sector = 0;
            entry->read_hash_value(0U, &sector, 0);
            params->put_name_key(checksums::Name{kName}, sector);
            entry->fetch_vec(0U, &offset, 0);
            attach_model(params, offset);
        }
    }

    params->fetch_float(kCameraZ, &camera_z_, 0);
    params->fetch_float(kAngleX, &core::at(angle_, 0), 0);
    params->fetch_float(kAngleY, &core::at(angle_, 1), 0);
    params->fetch_float(kAngleZ, &core::at(angle_, 2), 0);
    params->fetch_float(kAngVelX, &core::at(angular_velocity_, 0), 0);
    params->fetch_float(kAngVelY, &core::at(angular_velocity_, 1), 0);
    params->fetch_float(kAngVelZ, &core::at(angular_velocity_, 2), 0);
    for (int order = 0; order < kAngleOrderCount; ++order) {
        if (params->has_bare_flag(core::at(kAngleOrders, order))) {
            euler_sequence_ = order;
        }
    }
    if (params->has_bare_flag(kPointAtNothingFlag)) {
        point_at_ = kPointAtNothing;
    }
    uint32_t node = 0;
    if (params->read_hash_value(kNodeToPointTo, &node, 0)) {
        node_position(node_structure(node), &target_position_);
        point_at_ = kPointAtNode;
    }
    target_object_ = 0;
    if (params->read_hash_value(kObjectToPointTo, &target_object_, 0)) {
        point_at_ = kPointAtObject;
    }
    lean_angle_ = 0.0F;
    params->fetch_float(kTilt, &lean_angle_, 0);

    bob_height_ = 0.0F;
    bob_period_ = 0;
    Struct* hover = nullptr;
    if (params->read_sub_struct(kHoverParams, &hover, 0)) {
        hover->fetch_float(kAmp, &bob_height_, 0);
        float frequency = 1.0F;
        hover->fetch_float(kFreq, &frequency, 0);
        bob_period_ = get_truncated(kMillisecondsPerSecond / frequency);
    }

    parent_integer_ = 0;
    anchor_object_ = 0;
    attach_to_name_ = 0;
    anchor_shift_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    Struct* parent = nullptr;
    if (params->read_sub_struct(kOwnerArgs, &parent, 0)) {
        parent->read_hash_value(kName, &anchor_object_, 0);
        parent->fetch_vec(0U, &anchor_shift_, 0);
        if (parent->fetch_int_value(kSkater, &parent_integer_, 0)) {
            anchor_object_ = kSkater;
        }
        parent->read_hash_value(kNode, &attach_to_name_, 0);
    }

    if (params->has_bare_flag(kScaleToScreenDims)) {
        scale_to_screen_dims();
    } else if (params->fetch_vec("PivotPoint", &pivot_, 0)) {
        pivot_.w = 1.0F;
    }
    size_follows_range_ = params->has_bare_flag(kShrinkWithRange);
    distance_scale_ = kDefaultDistanceScale;
    params->fetch_float(kDistanceScale, &distance_scale_, 0);
    max_scale_ = kDefaultMaxScale;
    params->fetch_float(kMaxScale, &max_scale_, 0);
    int viewport = 0;
    if (params->fetch_int_value(kTargetViewport, &viewport, 0)) {
        view_slot_ = viewport;
        apply_viewport();
    }
}

// 0x004A6BA0
void ModelWidget::blend_towards(Struct* params)
{
    base_set_morph(this, params);
    if (params->has_bare_flag("scale_to_screen_dims")) {
        scale_to_screen_dims();
    }
}

// 0x004A6BD0
void apply_angles(maths::Mat4* matrix, std::span<const float, 3> angles, int order)
{
    const float x = core::at(angles, 0);
    const float y = core::at(angles, 1);
    const float z = core::at(angles, 2);
    switch (order) {
    case 0:
        matrix->spin_around_x(x);
        matrix->rotate_y(y);
        matrix->spin_around_z(z);
        break;
    case 1:
        matrix->spin_around_x(x);
        matrix->spin_around_z(z);
        matrix->rotate_y(y);
        break;
    case 2:
        matrix->rotate_y(y);
        matrix->spin_around_x(x);
        matrix->spin_around_z(z);
        break;
    case 3:
        matrix->rotate_y(y);
        matrix->spin_around_z(z);
        matrix->spin_around_x(x);
        break;
    case 4:
        matrix->spin_around_z(z);
        matrix->spin_around_x(x);
        matrix->rotate_y(y);
        break;
    case kAngleOrderZYX:
        matrix->spin_around_z(z);
        matrix->rotate_y(y);
        matrix->spin_around_x(x);
        break;
    default:
        break;
    }
}

// 0x004A5EE0
void screen_to_world(const maths::Vec4* centre, maths::Mat4* out, int viewport, maths::Vec2 layout, float z,
                     maths::Vec4* local)
{
    const float x = layout.x;
    const float y = layout.y;
    float down = y - render::kElementLayoutCentreY; // (stored: rounded to float)
    double across =
        (static_cast<double>(x) - render::kLayoutCentreX) * (static_cast<double>(render::fetch_view_ratio()) * aspect_to_layout());
    if (static_cast<int32_t>(render::renderer().back_buffer_width()) > render::kLayoutWidthPixels) { // the back buffer's width
        across *= wide_screen();
        down = static_cast<float>(static_cast<double>(down) * wide_screen());
    }
    across = across * z * minus_inv448();
    const double up = static_cast<double>(down) * z * inv448();
    maths::Vec4 point;
    point.x = static_cast<float>(across + centre->x);
    point.y = static_cast<float>(up + centre->y);
    point.z = static_cast<float>(static_cast<double>(z) + centre->z);
    point.w = static_cast<float>(static_cast<double>(centre->w) + 1.0F);
    if (local) {
        *local = point;
    }
    out->reset_to_identity();
    void* camera = viewport_camera(viewport);
    if (!camera) {
        return;
    }
    maths::Mat4 rotation = *cam_orientation(camera);
    core::at(rotation.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 place = point * rotation;
    const maths::Vec4* position = viewer_location(camera);
    place.x = position->x + place.x;
    place.y = place.y + position->y;
    place.z = place.z + position->z;
    maths::get_multiply(*out, *out, rotation);
    core::at(out->row, 3).x = place.x + core::at(out->row, 3).x;
    core::at(out->row, 3).y = place.y + core::at(out->row, 3).y;
    core::at(out->row, 3).z = place.z + core::at(out->row, 3).z;
}

// The widescreen front end's rule for Element3d (thug2-decomp.ini [features] widescreen_menus, not in the original).
//
// In the widescreen front end the 3D scene fills the window (a wider horizontal angle) while the 2D menu layout
// stays 4:3 and centred. An Element3d's models are world objects drawn by that 3D pass, so a backdrop made to fill
// the 4:3 screen - the menus' dark purple panel UI_bg.mdl (network, goals, Create-A-Graphic, high scores, dialog
// boxes) - now covers only the 4:3 middle, with the skate shop showing at the sides. Its edges are the model's own:
// widening the view can't show more of it (the view is already the window's; there is no more model), so the
// element is stretched horizontally instead - in the camera's frame, about the element's place, by just enough
// that both ends reach the window's edges (plus 1%). Elements that don't span the 4:3 layout (signs, icons, the
// spinning boards and decks) are left as they are.
//
// "Spans the layout": every model corner is in front of the camera, and the leftmost / rightmost corners reach
// within 5% of the 4:3 area's left / right edges or past them (the panel reaches 0.752 / 0.768 of the half-width
// on 16:9, the 4:3 area 0.75; the goals menu's sign, the largest of the others, starts right of the centre).
// `corners` are in the element's frame scaled (x right, y up, z backward, as the camera's), `place` is the
// element's place in the camera's frame; a corner is seen at x / (-z * tan(half angle)) of the half-width.
float widescreen_stretch(const maths::Vec4* corner_list, int count, const maths::Vec4& place, double tan_half_angle)
{
    const std::span<const maths::Vec4> corners(corner_list, count > 0 ? static_cast<size_t>(count) : 0);
    if (!render::get_front_end_widescreen() || count <= 0 || !(tan_half_angle > 0.0)) {
        return 1.0F;
    }
    const double layout_half_width = (4.0 / 3.0) / render::desired_3d_aspect_ratio(); // the 4:3 area, in half-widths
    double leftmost = 0.0;
    double rightmost = 0.0;
    for (int i = 0; i < count; ++i) {
        const double depth = -(static_cast<double>(core::at(corners, i).z) + place.z);
        if (!(depth > 0.0)) {
            return 1.0F;
        }
        const double seen = (static_cast<double>(core::at(corners, i).x) + place.x) / (depth * tan_half_angle);
        if (i == 0 || seen < leftmost) {
            leftmost = seen;
        }
        if (i == 0 || seen > rightmost) {
            rightmost = seen;
        }
    }
    const double reach = layout_half_width * 0.95;
    if (!(leftmost <= -reach && rightmost >= reach)) {
        return 1.0F;
    }
    // Stretched by k about the place, a corner is at place.x + k * x: the left edge is reached as soon as one
    // corner left of the place reaches it, the right likewise; both are needed.
    double need_left = 0.0;
    double need_right = 0.0;
    bool left = false;
    bool right = false;
    for (int i = 0; i < count; ++i) {
        const double x = core::at(corners, i).x;
        const double edge = -(static_cast<double>(core::at(corners, i).z) + place.z) * tan_half_angle; // the half-width there
        if (x < 0.0) {
            const double ratio = (-edge - place.x) / x;
            if (!left || ratio < need_left) {
                need_left = ratio;
            }
            left = true;
        } else if (x > 0.0) {
            const double ratio = (edge - place.x) / x;
            if (!right || ratio < need_right) {
                need_right = ratio;
            }
            right = true;
        }
    }
    if (!left || !right) {
        return 1.0F;
    }
    const double ratio = (need_left > need_right ? need_left : need_right) * 1.01;
    return ratio > 1.0 ? static_cast<float>(ratio) : 1.0F;
}

// 0x004A6D30. Where the element is (basis_frame_): following ParentParams' object (its position + the offset) or
// node, else on the layout CameraZ in front of the viewport's camera (screen_to_world, at its centre) - pointed at
// its target, if any, and tilted. Then the models' own turn (the angles, advanced by the angular velocities, about
// the pivot), scale (also with the distance to the camera, ScaleWithDistance) and hover, and each model's object
// placed: hidden while the element is transparent.
// In the widescreen front end, how far an element spanning the 4:3 layout is stretched to the window's edges: from
// its models' boxes (turned by `local` and scaled) as `view` sees them at `place`.
float ModelWidget::layout_stretch(void* view, const maths::Vec4& scale, const maths::Mat4& local,
                                  const maths::Vec4& place) const
{
    std::array<maths::Vec4, size_t{kBoxCorners} * kMaxModels> corners{};
    int count = 0;
    for (int i = 0; i < model_total_; ++i) {
        if (!core::at(models_, i).object) {
            continue;
        }
        objects::ObjectModule* component = core::at(models_, i).object->fetch_module(kModelModuleName);
        const maths::BBox* box = component ? model_bounding_box_if_any(component) : nullptr;
        if (!box) {
            count = 0; // (a model without a box yet: left as it is this frame)
            break;
        }
        for (int corner_index = 0; corner_index < kBoxCorners; ++corner_index) {
            const maths::Vec4 corner = {
                .x = (((corner_index & 1) != 0) ? box->max.x : box->min.x) + core::at(models_, i).offset.x,
                .y = (((corner_index & 2) != 0) ? box->max.y : box->min.y) + core::at(models_, i).offset.y,
                .z = (((corner_index & 4) != 0) ? box->max.z : box->min.z) + core::at(models_, i).offset.z,
                .w = 1.0F};
            const maths::Vec4 moved = corner * local;
            core::at(corners,
                     count++) = {.x = moved.x * scale.x, .y = moved.y * scale.y, .z = moved.z * scale.z, .w = 1.0F};
        }
    }
    const double tan_half_angle = std::tan(static_cast<double>(camera_angle(view)) * (3.14159265358979323846 / 360.0));
    const float stretch = widescreen_stretch(corners.data(), count, place, tan_half_angle);
    return stretch;
}

void ModelWidget::tick_frame()
{
    if (is_dead_ || (model_total_ == 0)) {
        return;
    }
    bool on_layout = false;
    maths::Vec4 place = {};
    if (anchor_object_ != 0U) {
        objects::Entity* parent = objects::ObjectHubBase::look_up_entity(anchor_object_);
        if (parent) {
            basis_frame_.reset_to_identity();
            core::at(basis_frame_.row, 3) =
                core::down_cast<objects::GameEntity>(parent)->at_point() + anchor_shift_ * basis_frame_;
        } else {
            script::Script* script =
                script::spawn_script(kKillScript, nullptr);
            script->entity().assign_value(static_cast<objects::Entity*>(this));
            is_dead_ = true;
        }
    } else if (attach_to_name_ != 0U) {
        maths::Vec4 position;
        node_position(node_structure(attach_to_name_), &position);
        basis_frame_.reset_to_identity();
        const maths::Vec4 offset = anchor_shift_ * basis_frame_;
        core::at(basis_frame_.row, 3).z = position.z + offset.z;
        core::at(basis_frame_.row, 3).w = position.w + offset.w;
        core::at(basis_frame_.row, 3).x = position.x + offset.x;
        core::at(basis_frame_.row, 3).y = position.y + offset.y;
    } else {
        const maths::Vec4 centre = {.x = static_cast<float>(static_cast<double>(centre_.x) * fit_scale_),
                                    .y = static_cast<float>(static_cast<double>(centre_.y) * fit_scale_),
                                    .z = static_cast<float>(static_cast<double>(centre_.z) * fit_scale_),
                                    .w = 1.0F};
        auto y = static_cast<float>((get_layout_height() * kHalf) + summed().top);
        auto x = static_cast<float>((get_layout_width() * kHalf) + summed().left);
        screen_to_world(&centre, &basis_frame_, view_slot_, {.x = x, .y = y}, camera_z_, &place);
        on_layout = point_at_ == kPointAtNothing;
        maths::Vec4 target; // (left uninitialised by the original if the object isn't found)
        target = {};
        if (point_at_ == kPointAtNode) {
            target = target_position_;
        } else if (point_at_ == kPointAtObject) {
            if (objects::Entity* object = objects::find_object(target_object_)) {
                target = core::down_cast<objects::GameEntity>(object)->at_point();
            }
        }
        if (point_at_ != kPointAtNothing) {
            // Facing the target (row 2 the direction to it, row 0 across, w 1 - sic), then tilted about the
            // across axis by the tilt plus the tilt times the direction's height (degrees).
            const maths::Vec4 from = core::at(basis_frame_.row, 3);
            basis_frame_.reset_to_identity();
            maths::Vec4 direction = target - from;
            direction.make_unit();
            core::at(basis_frame_.row, 2) = direction;
            core::at(basis_frame_.row, 0).z = -direction.x;
            core::at(basis_frame_.row, 0).x = direction.z;
            core::at(basis_frame_.row, 0).y = 0.0F;
            core::at(basis_frame_.row, 0).w = 1.0F;
            core::at(basis_frame_.row, 0).make_unit();
            basis_frame_.square_up_axes(2);
            const double tilt = lean_angle_;
            basis_frame_.pitch_locally(static_cast<float>((tilt - -(tilt * core::at(basis_frame_.row, 2).y)) * get_degrees()));
            basis_frame_.get_slide(from);
        }
    }

    // The models' own turn about the pivot.
    maths::Mat4 local;
    core::at(local.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(local.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(local.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(local.row, 3) = {
        .x = -pivot_.x, .y = -pivot_.y, .z = -pivot_.z, .w = static_cast<float>(static_cast<double>(-pivot_.w) + 1.0F)};
    const double angle_x = static_cast<double>(core::at(angular_velocity_, 0)) + core::at(angle_, 0);
    const double angle_y = static_cast<double>(core::at(angular_velocity_, 1)) + core::at(angle_, 1);
    auto angle_z = static_cast<float>(static_cast<double>(core::at(angular_velocity_, 2)) + core::at(angle_, 2));
    core::at(angle_, 0) = wrapped(angle_x);
    core::at(angle_, 1) = wrapped(angle_y);
    core::at(angle_, 2) = wrapped(angle_z);
    apply_angles(&local, angle_, euler_sequence_);
    core::at(local.row, 3).x += pivot_.x;
    core::at(local.row, 3).y += pivot_.y;
    core::at(local.row, 3).z += pivot_.z;
    core::at(local.row, 3).w += pivot_.w;

    auto scale_x = static_cast<float>(static_cast<double>(summed().scale_x_) * fit_scale_);
    maths::Vec4 scale = {.x = scale_x,
                         .y = static_cast<float>(static_cast<double>(summed().scale_y_) * fit_scale_),
                         .z = scale_x,
                         .w = 1.0F};
    void* camera = size_follows_range_ ? viewport_camera(view_slot_) : nullptr;
    if (camera != nullptr) {
        const maths::Vec4 away = core::at(basis_frame_.row, 3) - *viewer_location(camera);
        double distance = std::sqrt((static_cast<double>(away.x) * away.x) + (static_cast<double>(away.z) * away.z) +
                                    (static_cast<double>(away.y) * away.y)) *
                          distance_scale_;
        distance = std::max<double>(distance, 1.0F);
        distance = std::min<double>(distance, max_scale_);
        scale.x = static_cast<float>(scale.x * distance);
        scale.y = static_cast<float>(scale.y * distance);
        scale.z = static_cast<float>(scale.z * distance);
    }

    // Widescreen front end (widescreen_menus): an element spanning the 4:3 layout is stretched to the window's
    // edges (widescreen_stretch) - only on the layout and facing the camera (the matrix is the camera's orientation),
    // so the element's x is the screen's horizontal.
    if (on_layout && render::get_front_end_widescreen()) {
        void* view = viewport_camera(view_slot_);
        if (view) {
            scale.x *= layout_stretch(view, scale, local, place);
        }
    }

    local.scale_tool(scale);
    maths::get_multiply(basis_frame_, local, basis_frame_);

    maths::Vec4 hover = {};
    if (bob_height_ > 0.0F) {
        auto period = static_cast<uint32_t>(bob_period_);
        const uint32_t phase = frame_clock::get_time() % period * 2;
        const double hover_scale =
            std::sin(static_cast<double>(static_cast<int32_t>(phase)) * get_pi() / bob_period_) * bob_height_;
        hover.x = static_cast<float>(hover_scale * core::at(basis_frame_.row, 1).x);
        hover.y = static_cast<float>(hover_scale * core::at(basis_frame_.row, 1).y);
        hover.z = static_cast<float>(hover_scale * core::at(basis_frame_.row, 1).z);
        hover.w = static_cast<float>(hover_scale * core::at(basis_frame_.row, 1).w);
    }
    const bool visible = summed().alpha >= visible_alpha();
    for (int i = 0; i < model_total_; ++i) {
        const ShapeMesh& model = core::at(models_, i);
        if (!model.object) {
            continue;
        }
        objects::ObjectModule* component = model.object->fetch_module(kModelModuleName);
        maths::Vec4 position = model.offset * basis_frame_;
        const maths::Mat4 orientation = basis_frame_;
        if (bob_height_ > 0.0F) {
            position.x = hover.x + position.x;
            position.y = position.y + hover.y;
            position.z = position.z + hover.z;
            position.w = position.w + hover.w;
        }
        model.object->conceal(!visible);
        model.object->at_point() = position;
        model.object->display_matrix() = orientation;
        component->tick_frame();
    }
}

} // namespace frontend
