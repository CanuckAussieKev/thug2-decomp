// The front end's 3D model screen element (Element3d) - and, with thug2-decomp.ini [features] widescreen_menus,
// the widescreen front end's rule for the ones spanning the 4:3 menu layout (widescreen_stretch).

#include "frontend/model_widget.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/model.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "platform/run_log.h"
#include "engine/core/stubs.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/engine_geom.h"
#include "engine/objects/model_component.h"

namespace frontend {

namespace {

using script::Struct;

// Names of the properties (checksums of the names; two weren't recovered).
constexpr uint32_t kSkeletonName = 0x09794932, kCloneModel = 0x073EC4C8, kModel = 0x286A8D26, kModels = 0x1B29CFF6;
constexpr uint32_t kSector = 0xB45C2617, kSectors = 0x4A6BF967, kClass = 0x12B4E660, kLevelObject = 0xB7B3BD86;
constexpr uint32_t kName = 0xA1DC81F9, kCameraZ = 0xED7C6031;
constexpr uint32_t kAngleX = 0x0AFFD09D, kAngleY = 0x7DF8E00B, kAngleZ = 0xE4F1B1B1;
constexpr uint32_t kAngVelX = 0xC1608989, kAngVelY = 0xB667B91F, kAngVelZ = 0x2F6EE8A5;
constexpr uint32_t kAngleOrders[6] = {0x233F6B18, 0x911B6961, 0x3BE6306E, 0xE7DE33C0, 0xA0A9DF8D, 0xCEB5DE5A};
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
float aspect_to_layout() { return frontend::g_aspect_to_layout; } // 0.75000006 (3/4, a bit over)
float wide_screen() { return frontend::g_wide_screen; }           // 0.9
float minus_inv448() { return frontend::g_minus_inv448; }         // -1/448
float inv448() { return frontend::g_inv448; }                     // 1/448
float inv896() { return frontend::g_inv896; }                     // 1/896
float inv1280() { return frontend::g_inv1280; }                   // 1/1280
float inv_two_pi() { return frontend::g_inv_two_pi; }
float two_pi() { return frontend::g_two_pi; }
float get_pi() { return frontend::g_pi; }
float get_degrees() { return ::commands::g_degree_to_radian_ratio; } // pi / 180
float visible_alpha() { return ::modules::g_competition_tiny; }      // 0.0001

// UiWidget's pieces.
void base_set_properties(UiWidget* e, Struct* params) { e->UiWidget::change_traits(params); }
void base_set_morph(UiWidget* e, Struct* params) { e->UiWidget::blend_towards(params); }

// The viewport's camera (0x004A09C0, cdecl; null if none) and the camera's matrix, position and horizontal angle
// in degrees (thiscall getters).
void* viewport_camera(int viewport) { return render::fetch_live_camera(viewport); }
const maths::Mat4* cam_orientation(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_mat(); }
const maths::Vec4* viewer_location(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_location(); }
float camera_angle(void* camera) { return static_cast<gfx::ViewCam*>(camera)->fetch_effective_h_fov(); }

// A geom's bounding box (0x0048DE80, thiscall).
const maths::Vec4* geometry_bounding_box(void* geometry)
{
    return reinterpret_cast<const maths::Vec4*>(static_cast<render::Geom*>(geometry)->fetch_bounds());
}

// A model's model (the model component's +0x208) and that model's bounding box (min x y z w, max x y z w): the
// model's 0x00493CD0(0), then its 0x0048DE80 (thiscall).
const maths::Vec4* model_bounding_box(objects::ObjectModule* component)
{
    void* model = static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_;
    void* geometry = static_cast<render::RenderModel*>(model)->get_geom_by_name(0);
    return geometry_bounding_box(geometry);
}
// The same, or null when the model has no geometry yet (the widescreen rule asks every frame, for every element).
const maths::Vec4* model_bounding_box_if_any(objects::ObjectModule* component)
{
    void* model = static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_;
    if (!model)
        return nullptr;
    void* geometry = static_cast<render::RenderModel*>(model)->get_geom_by_name(0);
    if (!geometry)
        return nullptr;
    return geometry_bounding_box(geometry);
}
void set_model_viewports(objects::ObjectModule* component, uint32_t mask)
{
    void* model = static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_;
    static_cast<render::RenderModel*>(model)->toggle_shown(mask);
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
    int turns = get_truncated(angle * inv_two_pi());
    return static_cast<float>(angle - static_cast<double>(turns) * two_pi());
}

} // namespace

// 0x004A6060 (the models' smart pointers start empty: 0x0042B580)
ModelWidget::ModelWidget()
{
    for (ShapeMesh& model : models_)
        model.offset = {0.0f, 0.0f, 0.0f, 1.0f};
    model_total_ = 0;
    camera_z_ = -400.0f;
    angle_[0] = angle_[1] = angle_[2] = 0.0f;
    angular_velocity_[0] = angular_velocity_[1] = angular_velocity_[2] = 0.0f;
    point_at_ = kPointAtNothing;
    euler_sequence_ = 2;
    target_position_ = {0.0f, 0.0f, 0.0f, 1.0f};
    target_object_ = 0;
    lean_angle_ = 0.0f;
    anchor_object_ = 0;
    parent_integer_ = 0;
    fit_scale_ = 1.0f;
    view_slot_ = 0;
    size_follows_range_ = false;
    distance_scale_ = 0.002f;
    max_scale_ = 3.0f;
    is_dead_ = false;
    basis_frame_.reset_to_identity();
    pivot_ = {0.0f, 0.0f, 0.0f, 1.0f};
    centre_ = {0.0f, 0.0f, 0.0f, 1.0f};
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
        model.offset = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    model_total_ = 0;
}

// 0x004A5E80
void ModelWidget::apply_viewport()
{
    uint32_t mask = 1u << (view_slot_ & 31);
    for (int i = 0; i < model_total_; ++i) {
        if (models_[i].object)
            set_model_viewports(models_[i].object->fetch_module(kModelModuleName), mask);
    }
}

// 0x004A6540
void ModelWidget::attach_model(Struct* params, maths::Vec4 offset)
{
    objects::GameEntity* object = objects::object_hub_slot()->create_composite_object();
    models_[model_total_].object.assign_value(object);
    script::launch_script(kAddComponentsScript, params, models_[model_total_].object.get_pull(), false, nullptr);
    seal(models_[model_total_].object.get_pull());
    script::launch_script(kInitModelScript, params, models_[model_total_].object.get_pull(), false, nullptr);
    models_[model_total_].offset = offset;
    ++model_total_;
}

// 0x004A62B0. The models' bounding boxes (each moved by its offset) together; the scale is the smaller of the two
// that make the box's width (or depth, if larger) the element's width and its height the element's height, at
// CameraZ, through the layout's perspective factors - the vertical one uses the width too (sic).
void ModelWidget::scale_to_screen_dims()
{
    // (left uninitialised by the original when the first slot is empty)
    float min_x = 0, min_y = 0, min_z = 0, max_x = 0, max_y = 0, max_z = 0;
    for (int i = 0; i < model_total_; ++i) {
        ShapeMesh& model = models_[i];
        if (!model.object)
            continue;
        const maths::Vec4* box = model_bounding_box(model.object->fetch_module(kModelModuleName));
        double low_x = static_cast<double>(box[0].x) + model.offset.x; // (compared unrounded)
        float low_y = box[0].y + model.offset.y;
        float low_z = box[0].z + model.offset.z;
        float high_x = box[1].x + model.offset.x;
        float high_y = box[1].y + model.offset.y;
        float high_z = box[1].z + model.offset.z;
        if (i == 0) {
            min_x = static_cast<float>(low_x);
            min_y = low_y;
            min_z = low_z;
            max_x = high_x;
            max_y = high_y;
            max_z = high_z;
            continue;
        }
        if (low_x < min_x)
            min_x = static_cast<float>(low_x);
        if (low_y < min_y)
            min_y = low_y;
        if (low_z < min_z)
            min_z = low_z;
        if (high_x > max_x)
            max_x = high_x;
        if (high_y > max_y)
            max_y = high_y;
        if (high_z > max_z)
            max_z = high_z;
    }
    double extent_x = static_cast<double>(max_x) - min_x;
    float extent_y = max_y - min_y;
    float extent_z = max_z - min_z;
    double extent = extent_x > extent_z ? extent_x : extent_z;
    double width = span_, z = camera_z_;
    auto horizontal = static_cast<float>(width * z / ((width * inv1280() + 1.0) * 640.0f) / extent);
    double vertical = z * level_y_ / ((width * inv896() + 1.0) * 448.0f) / extent_y;
    horizontal = -horizontal;
    vertical = -vertical;
    fit_scale_ = static_cast<float>(horizontal < vertical ? horizontal : vertical);
    centre_.w = 0.0f;
    centre_.x = static_cast<float>((static_cast<double>(min_x) + max_x) * -0.5f);
    centre_.y = static_cast<float>((static_cast<double>(max_y) + min_y) * -0.5f);
    centre_.z = static_cast<float>((static_cast<double>(max_z) + min_z) * -0.5f);
}

// 0x004A6630
void ModelWidget::change_traits(Struct* params)
{
    base_set_properties(this, params);
    maths::Vec4 offset = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->has_entry_named(kSkeletonName) || params->has_entry_named(kCloneModel) ||
        params->has_entry_named(kModel) || params->has_entry_named(kModels)) {
        clear_models();
        attach_model(params, offset);
    } else if (params->has_entry_named(kSector)) {
        // level geometry: a LevelObject of that sector
        clear_models();
        params->put_name_key(kClass, kLevelObject);
        uint32_t sector;
        params->read_hash_value(kSector, &sector, 1);
        params->put_name_key(kName, sector);
        attach_model(params, offset);
    } else if (params->has_entry_named(kSectors)) {
        // several, each {sector (offset)}
        clear_models();
        params->put_name_key(kClass, kLevelObject);
        script::Array* sectors = nullptr;
        params->fetch_list(kSectors, &sectors, 0);
        for (uint32_t i = 0; i < sectors->extent_; ++i) {
            auto* entry = reinterpret_cast<Struct*>(sectors->fetch_part(i));
            uint32_t sector;
            entry->read_hash_value(0u, &sector, 0);
            params->put_name_key(kName, sector);
            entry->fetch_vec(0u, reinterpret_cast<script::VectorValue*>(&offset), 0);
            attach_model(params, offset);
        }
    }

    params->fetch_float(kCameraZ, &camera_z_, 0);
    params->fetch_float(kAngleX, &angle_[0], 0);
    params->fetch_float(kAngleY, &angle_[1], 0);
    params->fetch_float(kAngleZ, &angle_[2], 0);
    params->fetch_float(kAngVelX, &angular_velocity_[0], 0);
    params->fetch_float(kAngVelY, &angular_velocity_[1], 0);
    params->fetch_float(kAngVelZ, &angular_velocity_[2], 0);
    for (int order = 0; order < 6; ++order) {
        if (params->has_bare_flag(kAngleOrders[order]))
            euler_sequence_ = order;
    }
    if (params->has_bare_flag(kPointAtNothingFlag))
        point_at_ = kPointAtNothing;
    uint32_t node = 0;
    if (params->read_hash_value(kNodeToPointTo, &node, 0)) {
        node_position(node_structure(node), &target_position_);
        point_at_ = kPointAtNode;
    }
    target_object_ = 0;
    if (params->read_hash_value(kObjectToPointTo, &target_object_, 0))
        point_at_ = kPointAtObject;
    lean_angle_ = 0.0f;
    params->fetch_float(kTilt, &lean_angle_, 0);

    bob_height_ = 0.0f;
    bob_period_ = 0;
    Struct* hover = nullptr;
    if (params->read_sub_struct(kHoverParams, &hover, 0)) {
        hover->fetch_float(kAmp, &bob_height_, 0);
        float frequency = 1.0f;
        hover->fetch_float(kFreq, &frequency, 0);
        bob_period_ = get_truncated(1000.0 / frequency);
    }

    parent_integer_ = 0;
    anchor_object_ = 0;
    attach_to_name_ = 0;
    anchor_shift_ = {0.0f, 0.0f, 0.0f, 1.0f};
    Struct* parent = nullptr;
    if (params->read_sub_struct(kOwnerArgs, &parent, 0)) {
        parent->read_hash_value(kName, &anchor_object_, 0);
        parent->fetch_vec(0u, reinterpret_cast<script::VectorValue*>(&anchor_shift_), 0);
        if (parent->fetch_int_value(kSkater, &parent_integer_, 0))
            anchor_object_ = kSkater;
        parent->read_hash_value(kNode, &attach_to_name_, 0);
    }

    if (params->has_bare_flag(kScaleToScreenDims)) {
        scale_to_screen_dims();
    } else if (params->fetch_vec("PivotPoint", reinterpret_cast<script::VectorValue*>(&pivot_), 0)) {
        pivot_.w = 1.0f;
    }
    size_follows_range_ = params->has_bare_flag(kShrinkWithRange);
    distance_scale_ = 0.002f;
    params->fetch_float(kDistanceScale, &distance_scale_, 0);
    max_scale_ = 3.0f;
    params->fetch_float(kMaxScale, &max_scale_, 0);
    int viewport;
    if (params->fetch_int_value(kTargetViewport, &viewport, 0)) {
        view_slot_ = viewport;
        apply_viewport();
    }
}

// 0x004A6BA0
void ModelWidget::blend_towards(Struct* params)
{
    base_set_morph(this, params);
    if (params->has_bare_flag("scale_to_screen_dims"))
        scale_to_screen_dims();
}

// 0x004A6BD0
void apply_angles(maths::Mat4* m, float x, float y, float z, int order)
{
    switch (order) {
    case 0:
        m->spin_around_x(x);
        m->rotate_y(y);
        m->spin_around_z(z);
        break;
    case 1:
        m->spin_around_x(x);
        m->spin_around_z(z);
        m->rotate_y(y);
        break;
    case 2:
        m->rotate_y(y);
        m->spin_around_x(x);
        m->spin_around_z(z);
        break;
    case 3:
        m->rotate_y(y);
        m->spin_around_z(z);
        m->spin_around_x(x);
        break;
    case 4:
        m->spin_around_z(z);
        m->spin_around_x(x);
        m->rotate_y(y);
        break;
    case 5:
        m->spin_around_z(z);
        m->rotate_y(y);
        m->spin_around_x(x);
        break;
    default:
        break;
    }
}

// 0x004A5EE0
void screen_to_world(const maths::Vec4* centre, maths::Mat4* out, int viewport, float x, float y, float z,
                     maths::Vec4* local)
{
    float down = y - 224.0f; // (stored: rounded to float)
    double across =
        (static_cast<double>(x) - 320.0f) * (static_cast<double>(render::fetch_view_ratio()) * aspect_to_layout());
    if (static_cast<int32_t>(render::g_back_buffer_width) > 640) { // the back buffer's width
        across *= wide_screen();
        down = static_cast<float>(static_cast<double>(down) * wide_screen());
    }
    across = across * z * minus_inv448();
    double up = static_cast<double>(down) * z * inv448();
    maths::Vec4 point;
    point.x = static_cast<float>(across + centre->x);
    point.y = static_cast<float>(up + centre->y);
    point.z = static_cast<float>(static_cast<double>(z) + centre->z);
    point.w = static_cast<float>(static_cast<double>(centre->w) + 1.0f);
    if (local)
        *local = point;
    out->reset_to_identity();
    void* camera = viewport_camera(viewport);
    if (!camera)
        return;
    maths::Mat4 rotation = *cam_orientation(camera);
    rotation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Vec4 place = point * rotation;
    const maths::Vec4* position = viewer_location(camera);
    place.x = position->x + place.x;
    place.y = place.y + position->y;
    place.z = place.z + position->z;
    maths::get_multiply(*out, *out, rotation);
    out->row[3].x = place.x + out->row[3].x;
    out->row[3].y = place.y + out->row[3].y;
    out->row[3].z = place.z + out->row[3].z;
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
float widescreen_stretch(const maths::Vec4* corners, int count, const maths::Vec4& place, double tan_half_angle)
{
    if (!render::get_front_end_widescreen() || count <= 0 || !(tan_half_angle > 0.0))
        return 1.0f;
    double layout_half_width = (4.0 / 3.0) / render::desired_3d_aspect_ratio(); // the 4:3 area, in half-widths
    double leftmost = 0.0, rightmost = 0.0;
    for (int i = 0; i < count; ++i) {
        double depth = -(static_cast<double>(corners[i].z) + place.z);
        if (!(depth > 0.0))
            return 1.0f;
        double seen = (static_cast<double>(corners[i].x) + place.x) / (depth * tan_half_angle);
        if (i == 0 || seen < leftmost)
            leftmost = seen;
        if (i == 0 || seen > rightmost)
            rightmost = seen;
    }
    double reach = layout_half_width * 0.95;
    if (!(leftmost <= -reach && rightmost >= reach))
        return 1.0f;
    // Stretched by k about the place, a corner is at place.x + k * x: the left edge is reached as soon as one
    // corner left of the place reaches it, the right likewise; both are needed.
    double need_left = 0.0, need_right = 0.0;
    bool left = false, right = false;
    for (int i = 0; i < count; ++i) {
        double x = corners[i].x;
        double edge = -(static_cast<double>(corners[i].z) + place.z) * tan_half_angle; // the half-width there
        if (x < 0.0) {
            double k = (-edge - place.x) / x;
            if (!left || k < need_left)
                need_left = k;
            left = true;
        } else if (x > 0.0) {
            double k = (edge - place.x) / x;
            if (!right || k < need_right)
                need_right = k;
            right = true;
        }
    }
    if (!left || !right)
        return 1.0f;
    double k = (need_left > need_right ? need_left : need_right) * 1.01;
    return k > 1.0 ? static_cast<float>(k) : 1.0f;
}

// 0x004A6D30. Where the element is (basis_frame_): following ParentParams' object (its position + the offset) or
// node, else on the layout CameraZ in front of the viewport's camera (screen_to_world, at its centre) - pointed at
// its target, if any, and tilted. Then the models' own turn (the angles, advanced by the angular velocities, about
// the pivot), scale (also with the distance to the camera, ScaleWithDistance) and hover, and each model's object
// placed: hidden while the element is transparent.
void ModelWidget::tick_frame()
{
    if (is_dead_ || !model_total_)
        return;
    bool on_layout = false;
    maths::Vec4 place = {};
    if (anchor_object_) {
        auto* manager = reinterpret_cast<objects::ObjectHubBase*>(objects::object_hub_slot());
        objects::Entity* parent = manager->look_up_entity(anchor_object_);
        if (parent) {
            basis_frame_.reset_to_identity();
            basis_frame_.row[3] =
                reinterpret_cast<objects::GameEntity*>(parent)->at_point_ + anchor_shift_ * basis_frame_;
        } else {
            script::Script* script =
                script::spawn_script(kKillScript, nullptr, 0, nullptr, 0, 0, false, false, false, false);
            script->entity_.assign_value(reinterpret_cast<objects::Entity*>(this));
            is_dead_ = true;
        }
    } else if (attach_to_name_) {
        maths::Vec4 position;
        node_position(node_structure(attach_to_name_), &position);
        basis_frame_.reset_to_identity();
        maths::Vec4 offset = anchor_shift_ * basis_frame_;
        basis_frame_.row[3].z = position.z + offset.z;
        basis_frame_.row[3].w = position.w + offset.w;
        basis_frame_.row[3].x = position.x + offset.x;
        basis_frame_.row[3].y = position.y + offset.y;
    } else {
        maths::Vec4 centre = {static_cast<float>(static_cast<double>(centre_.x) * fit_scale_),
                              static_cast<float>(static_cast<double>(centre_.y) * fit_scale_),
                              static_cast<float>(static_cast<double>(centre_.z) * fit_scale_), 1.0f};
        auto y = static_cast<float>(get_layout_height() * 0.5f + summed_.top);
        auto x = static_cast<float>(get_layout_width() * 0.5f + summed_.left);
        screen_to_world(&centre, &basis_frame_, view_slot_, x, y, camera_z_, &place);
        on_layout = point_at_ == kPointAtNothing;
        maths::Vec4 target; // (left uninitialised by the original if the object isn't found)
        target = {};
        if (point_at_ == kPointAtNode) {
            target = target_position_;
        } else if (point_at_ == kPointAtObject) {
            if (objects::Entity* object = objects::find_object(target_object_))
                target = reinterpret_cast<objects::GameEntity*>(object)->at_point_;
        }
        if (point_at_ != kPointAtNothing) {
            // Facing the target (row 2 the direction to it, row 0 across, w 1 - sic), then tilted about the
            // across axis by the tilt plus the tilt times the direction's height (degrees).
            maths::Vec4 from = basis_frame_.row[3];
            basis_frame_.reset_to_identity();
            maths::Vec4 direction = target - from;
            direction.make_unit();
            basis_frame_.row[2] = direction;
            basis_frame_.row[0].z = -direction.x;
            basis_frame_.row[0].x = direction.z;
            basis_frame_.row[0].y = 0.0f;
            basis_frame_.row[0].w = 1.0f;
            basis_frame_.row[0].make_unit();
            basis_frame_.square_up_axes(2);
            double tilt = lean_angle_;
            basis_frame_.pitch_locally(static_cast<float>((tilt - -(tilt * basis_frame_.row[2].y)) * get_degrees()));
            basis_frame_.get_slide(from);
        }
    }

    // The models' own turn about the pivot.
    maths::Mat4 local;
    local.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    local.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    local.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    local.row[3] = {-pivot_.x, -pivot_.y, -pivot_.z, static_cast<float>(static_cast<double>(-pivot_.w) + 1.0f)};
    double angle_x = static_cast<double>(angular_velocity_[0]) + angle_[0];
    double angle_y = static_cast<double>(angular_velocity_[1]) + angle_[1];
    auto angle_z = static_cast<float>(static_cast<double>(angular_velocity_[2]) + angle_[2]);
    angle_[0] = wrapped(angle_x);
    angle_[1] = wrapped(angle_y);
    angle_[2] = wrapped(angle_z);
    apply_angles(&local, angle_[0], angle_[1], angle_[2], euler_sequence_);
    local.row[3].x += pivot_.x;
    local.row[3].y += pivot_.y;
    local.row[3].z += pivot_.z;
    local.row[3].w += pivot_.w;

    auto scale_x = static_cast<float>(static_cast<double>(summed_.scale_x_) * fit_scale_);
    maths::Vec4 scale = {scale_x, static_cast<float>(static_cast<double>(summed_.scale_y_) * fit_scale_), scale_x,
                         1.0f};
    void* camera = nullptr;
    if (size_follows_range_ && (camera = viewport_camera(view_slot_)) != nullptr) {
        maths::Vec4 away = basis_frame_.row[3] - *viewer_location(camera);
        double distance = std::sqrt(static_cast<double>(away.x) * away.x + static_cast<double>(away.z) * away.z +
                                    static_cast<double>(away.y) * away.y) *
                          distance_scale_;
        if (distance < 1.0f)
            distance = 1.0f;
        if (distance > max_scale_)
            distance = max_scale_;
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
            maths::Vec4 corners[8 * kMaxModels];
            int count = 0;
            for (int i = 0; i < model_total_; ++i) {
                if (!models_[i].object)
                    continue;
                objects::ObjectModule* component = models_[i].object->fetch_module(kModelModuleName);
                const maths::Vec4* box = component ? model_bounding_box_if_any(component) : nullptr;
                if (!box) {
                    count = 0; // (a model without a box yet: left as it is this frame)
                    break;
                }
                for (int c = 0; c < 8; ++c) {
                    maths::Vec4 corner = {(c & 1 ? box[1].x : box[0].x) + models_[i].offset.x,
                                          (c & 2 ? box[1].y : box[0].y) + models_[i].offset.y,
                                          (c & 4 ? box[1].z : box[0].z) + models_[i].offset.z, 1.0f};
                    maths::Vec4 e = corner * local;
                    corners[count++] = {e.x * scale.x, e.y * scale.y, e.z * scale.z, 1.0f};
                }
            }
            double tan_half_angle =
                std::tan(static_cast<double>(camera_angle(view)) * (3.14159265358979323846 / 360.0));
            float stretch = widescreen_stretch(corners, count, place, tan_half_angle);
            scale.x *= stretch;
        }
    }

    local.scale_tool(scale);
    maths::get_multiply(basis_frame_, local, basis_frame_);

    maths::Vec4 hover = {};
    if (bob_height_ > 0.0f) {
        auto period = static_cast<uint32_t>(bob_period_);
        uint32_t phase = frame_clock::get_time() % period * 2;
        double s = std::sin(static_cast<double>(static_cast<int32_t>(phase)) * get_pi() / bob_period_) * bob_height_;
        hover.x = static_cast<float>(s * basis_frame_.row[1].x);
        hover.y = static_cast<float>(s * basis_frame_.row[1].y);
        hover.z = static_cast<float>(s * basis_frame_.row[1].z);
        hover.w = static_cast<float>(s * basis_frame_.row[1].w);
    }
    bool visible = summed_.alpha >= visible_alpha();
    for (int i = 0; i < model_total_; ++i) {
        ShapeMesh& model = models_[i];
        if (!model.object)
            continue;
        objects::ObjectModule* component = model.object->fetch_module(kModelModuleName);
        maths::Vec4 position = model.offset * basis_frame_;
        maths::Mat4 orientation = basis_frame_;
        if (bob_height_ > 0.0f) {
            position.x = hover.x + position.x;
            position.y = position.y + hover.y;
            position.z = position.z + hover.z;
            position.w = position.w + hover.w;
        }
        model.object->conceal(!visible);
        model.object->at_point_ = position;
        model.object->display_matrix_ = orientation;
        component->tick_frame();
    }
}

} // namespace frontend
