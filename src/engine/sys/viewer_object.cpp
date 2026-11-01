// The model viewer's object (0x005897B0-0x0058AFB7). See viewer_object.h.

#include "engine/sys/viewer_object.h"
#include "engine/gfx/d3dx.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/text_store.h"
#include "engine/core/task_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/movie_cfuncs.h"
#include "engine/script/cscript.h"
#include "engine/script/load.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/model_viewer.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"

namespace objects {

namespace {

constexpr uint32_t kKillViewerObjectPanel = 0x9E65A71E; // kill_viewer_object_panel
constexpr uint32_t kCasArtist = 0x2A648514;             // cas_artist
constexpr uint32_t kModelName = 0xB974F2FC;             // modelname
constexpr uint32_t kModel = 0x286A8D26;                 // model
constexpr uint32_t kProfile = 0x7EA855F0;               // profile
constexpr uint32_t kSkeletonFileName = 0xF679DB13;      // skeletonFileName
constexpr uint32_t kSkeletonName = 0x09794932;          // skeletonName
constexpr uint32_t kViewerObjPos = 0x15670AB7;          // ViewerObjPos
constexpr uint32_t kAddComponents = 0x24C71BC7;         // viewerobj_add_components
constexpr uint32_t kSetupModelScript = 0xBBAB79DE;      // viewerobj_init_model
constexpr uint32_t kIdle = 0x23DB4AEA;                  // idle
constexpr int kAssetKindSkeleton = 6;                   // (the asset type)

// `path` set to a model's file path: "models\<name>\<name><extension>", or "models\<name>" when the name holds a path
// separator (0x004879D0, in the CAS code - not rewritten yet - makes it as a temporary String).
void set_model_path(text::Chars& path, const char* name, const char* extension)
{
    alignas(text::Chars) uint8_t temporary[sizeof(text::Chars)];
    gfx::make_model_path(reinterpret_cast<text::Chars*>(temporary), name, extension);
    path = *reinterpret_cast<text::Chars*>(temporary);
    reinterpret_cast<text::Chars*>(temporary)->~Chars();
}

// `path` without its .skin / .mdl part, with `extension` (".col" / ".tex") on the end (0x0048BFE0 cuts the path).
void sibling_file(const char* path, const char* extension, char (&out)[256])
{
    render::engine::split_skin_name(path, out);
    strcat_s(out, extension);
}

// A CAS artist's cached copy of `file` dropped, so it's read again from the edited file.
void drop_cached_file(const char* file)
{
    if (!script::fetch_int_value(kCasArtist))
        return;
    assets::ResourceRegistry* assets = assets::asset_manager();
    if (assets::Resource* asset = assets->obtain_loaded_item(checksums::hash_from_text(file), 0))
        assets->discard_loaded_item(asset);
}

// Model `name`'s .col, .skin, .mdl and .tex files dropped (in that order).
void drop_cached_model(const char* name)
{
    text::Chars path;
    set_model_path(path, name, ".skin");
    char sibling[256];
    sibling_file(path.c_str(), ".col", sibling);
    drop_cached_file(sibling);
    drop_cached_file(path.c_str());
    set_model_path(path, name, ".mdl");
    drop_cached_file(path.c_str());
    sibling_file(path.c_str(), ".tex", sibling);
    drop_cached_file(sibling);
}

AnimBlenderComponent* get_animation(GameEntity* object)
{
    return static_cast<AnimBlenderComponent*>(object->fetch_module(AnimBlenderComponent::kType));
}

// The same, for its virtual member functions (vtable +0x30).
ObjectModule* animation_base(GameEntity* object) { return object->fetch_module(AnimBlenderComponent::kType); }

// More names.
constexpr uint32_t kSpeed = 0xF0D90109;    // speed
constexpr uint32_t kSetSpeed = 0xBD4EDD44; // (the animation component's Speed member function)
constexpr uint32_t kStep = 0x6AAEB76F;     // (its step member function: frames)
constexpr uint32_t kFrames = 0x06C9F278;
constexpr uint32_t kReverse = 0xF5E2B871;                     // (its reverse member function)
constexpr uint32_t kSkeleton = 0x222756D5;                    // skeleton (the component)
constexpr uint32_t kMaxWheelSpeed = 0xCACA33A4;               // max_wheel_speed
constexpr uint32_t kLine1 = 0x282C53D5;                       // line1
constexpr uint32_t kLine2 = 0xB125026F;                       // line2
constexpr uint32_t kLine3 = 0xC62232F9;                       // line3
constexpr uint32_t kLine4 = 0x5846A75A;                       // text_row4
constexpr uint32_t kDrawViewerObjectPanel = 0xED9C1839;       // draw_viewer_object_panel
constexpr uint32_t kViewerAutoRepeatTime = 0xE60442D5;        // ViewerAutoRepeatTime
constexpr uint32_t kViewerAutoRepeatSpeed = 0x2DA212E5;       // ViewerAutoRepeatSpeed
constexpr uint32_t kObjectTypeComposite = 0xCDEB9229;         // OBJECT_TYPE_COMPOSITE
const float kStepSeconds = std::bit_cast<float>(0x3C888889u); // 1/60 s
constexpr float kSpeedStep = 0.25f;                           // 0x0064F130
constexpr float kRadiansPerDegree = 0.017453292f;             // 0x00645464

// A pooled script structure, and one cleared and freed (either may be null).
script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

// D3DXMatrixMultiply (library code: stdcall), out = a * b.
void multiply_matrix(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The model component's model (+0x208), its bone count (0x00492FF0) and hierarchy (0x00493040: objects of 0x50 bytes,
// each with its matrix at +0x10) - render::RenderModel, not rewritten yet; the skeleton component's skeleton (the folded
// getter 0x005480F0: +0x18) and the skeleton's bone matrices (0x005480C0: +0x04).
uint8_t* model_of(ObjectModule* component)
{
    return reinterpret_cast<uint8_t*>(static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_);
}
int get_bone_count(uint8_t* model)
{
    return static_cast<int>(reinterpret_cast<render::RenderModel*>(model)->get_asset_bone_count());
}
uint8_t* hierarchy_of(uint8_t* model)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<render::RenderModel*>(model)->get_asset_hierarchy());
}
uint8_t* skeleton_of(ObjectModule* component) { return static_cast<objects::BoneRigModule*>(component)->bone_rig_; }
maths::Mat4* skeleton_palette(uint8_t* skeleton) { return *reinterpret_cast<maths::Mat4**>(skeleton + 0x04); }

uint32_t g_no_skeleton_counter{10};
int g_repeat_count;
uint32_t g_repeat_count_guard;
float g_wheel_angle;

// The wheels' angle (degrees) and the counter that moves on while there's no skeleton.
float& wheel_angle() { return g_wheel_angle; }
uint32_t& no_skeleton_counter() { return g_no_skeleton_counter; }
// The input handler's auto-repeat count (a function static: its guard is bit 1 of 0x007CE58C).
int& repeat_count() { return g_repeat_count; }
uint32_t& repeat_count_guard() { return g_repeat_count_guard; }
pad_input::Hub* input_manager() { return pad_input::g_input_manager; }

PreviewEntity* object_of(net::InboundMsgCtx* context) { return static_cast<PreviewEntity*>(context->content_); }

} // namespace

// 0x005897B0
void PreviewEntity::power_down() { script::launch_script(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr); }

// 0x005897D0
void PreviewEntity::play_animation(uint32_t anim)
{
    // (The original looks the component up again for each call.)
    if (!get_animation(this) || !get_animation(this)->has_value(anim))
        return;
    const float length = get_animation(this)->fetch_value(anim);
    get_animation(this)->play_animation_ex(anim, 0, 0, std::bit_cast<uint32_t>(length), 1,
                                           std::bit_cast<uint32_t>(0.3f), std::bit_cast<uint32_t>(1.0f));
}

// 0x00589860
PreviewEntity::~PreviewEntity()
{
    task_->drop_out(); // (its input handler; not null-checked: as the original)
    if (asset_) {
        assets::ResourceRegistry* assets = assets::asset_manager();
        if (assets::Resource* asset = assets->obtain_loaded_item(asset_, 0)) {
            assets->discard_loaded_item(asset);
            assets->obtain_loaded_item(asset_, 0);
        }
        asset_ = 0;
    }
    delete task_;
}

namespace {
maths::Vec4 g_origin_position{0.0f, 0.0f, 0.0f, 1.0f};
} // namespace

// 0x00589910
void PreviewEntity::read_in(script::Struct* params)
{
    object_hub_slot()->enrol_subject(this);
    add_motion_components(this);
    add_node_components(this, params);
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(commands::g_got_extra_memory ? static_cast<memory::MemSource*>(memory->debug_heap_)
                                                          : static_cast<memory::MemSource*>(memory->bottom_up_heap_));
    const char* name = nullptr;
    for (int i = 0; i < 4; ++i) {
        char key[260];
        std::snprintf(key, sizeof(key), "model%d", i);
        if (params->fetch_text(key, &name, 0))
            drop_cached_model(name);
    }
    if (params->fetch_text(kModelName, &name, 0) || params->fetch_text(kModel, &name, 0)) {
        drop_cached_model(name);
    } else {
        script::Struct* profile = nullptr;
        params->read_sub_struct(kProfile, &profile, 0); // (not used)
    }
    const char* skeleton_file = nullptr;
    if (params->fetch_text(kSkeletonFileName, &skeleton_file, 0)) {
        uint32_t skeleton = 0; // (left unset if skeletonName is missing: as the original)
        params->read_hash_value(kSkeletonName, &skeleton, 1);
        assets::ResourceRegistry* assets = assets::asset_manager();
        if (!assets->obtain_loaded_item(skeleton, 0)) {
            assets->load_asset(skeleton_file, kAssetKindSkeleton, skeleton, 0, false, false, false, nullptr, 0);
            asset_ = skeleton;
        }
    }
    script::VectorValue position{};
    if (params->fetch_vec(kViewerObjPos, &position, 0)) {
        at_point_.x = position.x;
        at_point_.y = position.y;
        at_point_.z = position.z;
        at_point_.w = position.w;
    } else {
        at_point_ = g_origin_position; // (0, 0, 0, 1)
    }
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&basis_frame_, kIdentity, sizeof(kIdentity));
    script::launch_script(kAddComponents, params, this, false, nullptr);
    seal(); // (Finalize)
    script::launch_script(kSetupModelScript, params, this, false, nullptr);
    play_animation(kIdle);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00589FD0
void PreviewEntity::reload_animation(const char* file, uint32_t anim)
{
    if (!get_animation(this))
        return;
    assets::ResourceRegistry* assets = assets::asset_manager();
    uint32_t name = checksums::hash_from_text(file);
    if (anim)
        name = get_animation(this)->get_anim_set() + anim;
    if (assets::Resource* asset = assets->obtain_loaded_item(name, 0))
        assets->discard_loaded_item(asset);
    assets->load_anim(file, anim, get_animation(this)->get_anim_set(), false, false);
    play_animation(anim);
}

// 0x0058A2E0
void PreviewEntity::change_spin(float z, float x)
{
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&basis_frame_, kIdentity, sizeof(kIdentity));
    maths::Mat4 rotation{};
    multiply_matrix(&basis_frame_, &basis_frame_, &maths::make_roll_matrix(rotation, z));
    multiply_matrix(&basis_frame_, &basis_frame_, &maths::make_pitch_matrix(rotation, x));
}

// 0x0058A360
void PreviewEntity::scale_anim_rate(float speed)
{
    speed_ = speed;
    if (speed > 1.0f)
        speed_ = 1.0f;
    if (speed_ < 0.0f)
        speed_ = 0.0f;
    if (get_animation(this)) {
        get_animation(this)->first_channel_47e_850(speed_, 0.0f, 0.0f, 0.0f);
        script::Struct* params = spawn_param_table();
        params->push_float(kSpeed, speed_);
        animation_base(this)->invoke_method_by_name(kSetSpeed, params, nullptr);
        delete_structure(params);
    }
    is_halted_ = false;
}

// 0x0058A4A0
void PreviewEntity::step_animation(bool forward)
{
    if (!is_halted_) {
        saved_speed_ = speed_;
        scale_anim_rate(0.0f);
    }
    if (get_animation(this)) {
        // (The original looks the component up again for each call.)
        if (forward) {
            get_animation(this)->pass_to_each_channel(std::bit_cast<uint32_t>(kStepSeconds));
            get_animation(this)->visit_channels();
            script::Struct* params = spawn_param_table();
            params->push_float(kFrames, kStepSeconds);
            animation_base(this)->invoke_method_by_name(kStep, params, nullptr);
            delete_structure(params);
        } else {
            // Reversed for a step forward, then turned round again.
            get_animation(this)->first_channel_47e_810(0);
            get_animation(this)->pass_to_each_channel(std::bit_cast<uint32_t>(-kStepSeconds));
            get_animation(this)->visit_channels();
            get_animation(this)->first_channel_47e_810(0);
            animation_base(this)->invoke_method_by_name(kReverse, nullptr, nullptr);
            script::Struct* params = spawn_param_table();
            params->push_float(kFrames, -kStepSeconds);
            animation_base(this)->invoke_method_by_name(kStep, params, nullptr);
            delete_structure(params);
            animation_base(this)->invoke_method_by_name(kReverse, nullptr, nullptr);
        }
        get_animation(this)->first_channel_47e_ff0();
    }
    is_halted_ = true;
}

// 0x0058A720
void PreviewEntity::spin_wheels()
{
    ObjectModule* model = fetch_module(kModel);
    if (!model)
        return;
    uint8_t* nx_model = model_of(model);
    const uint8_t* hierarchy = hierarchy_of(nx_model);
    if (!fetch_module(kSkeleton)) {
        no_skeleton_counter() = (no_skeleton_counter() + 1) & 0x1F;
        return;
    }
    uint8_t* skeleton = skeleton_of(fetch_module(kSkeleton));
    // The angle kept in double precision through the wrap (the original's x87 register), stored as a float each time.
    double angle = static_cast<double>(script::fetch_float(kMaxWheelSpeed)) * speed_ + wheel_angle();
    wheel_angle() = static_cast<float>(angle);
    if (angle > 360.0) {
        do
            angle -= 360.0;
        while (angle > 360.0);
        wheel_angle() = static_cast<float>(angle);
    }
    if (angle < 0.0) {
        do
            angle += 360.0;
        while (angle < 0.0);
        wheel_angle() = static_cast<float>(angle);
    }
    if (get_bone_count(nx_model) <= 5)
        return;
    // The bones from the model's hierarchy, the wheels (2-5) turned about x, and all but the first put under it.
    maths::Mat4* bones = skeleton_palette(skeleton);
    for (int i = 0; i < get_bone_count(nx_model); ++i)
        bones[i] = *reinterpret_cast<const maths::Mat4*>(hierarchy + 0x10 + i * 0x50);
    for (int wheel = 2; wheel < 6; ++wheel) {
        maths::Mat4 spin{};
        const auto radians = static_cast<float>(static_cast<double>(wheel_angle()) * kRadiansPerDegree);
        multiply_matrix(&bones[wheel], &maths::make_pitch_matrix(spin, radians), &bones[wheel]);
    }
    for (int bone = 1; bone < 6; ++bone)
        multiply_matrix(&bones[bone], &bones[bone], &bones[0]);
}

// 0x0058A8B0
void PreviewEntity::update_panel()
{
    if (get_animation(this) && panel_shown_) {
        const uint32_t anim = get_animation(this)->get_timeline30();
        if (get_animation(this)->has_value(anim)) {
            script::Struct* params = spawn_param_table();
            char line1[128], line2[128], line3[128], text_row4[128];
            std::snprintf(line1, sizeof(line1), "name %s", "Unknown");
            params->append_word(kLine1, line1);
            std::snprintf(line2, sizeof(line2), "speed %f", is_halted_ ? 0.0 : static_cast<double>(speed_));
            params->append_word(kLine2, line2);
            // (Each value looked up again for each line, the length before the time.)
            uint32_t current = get_animation(this)->get_timeline30();
            float length = get_animation(this)->fetch_value(current);
            float time = get_animation(this)->get_timeline18();
            std::snprintf(line3, sizeof(line3), "time %f of %f", static_cast<double>(time),
                          static_cast<double>(length));
            params->append_word(kLine3, line3);
            current = get_animation(this)->get_timeline30();
            length = get_animation(this)->fetch_value(current);
            const int last_frame = static_cast<int>(static_cast<double>(length) * 60.0);
            time = get_animation(this)->get_timeline18();
            const int frame = static_cast<int>(static_cast<double>(time) * 60.0);
            std::snprintf(text_row4, sizeof(text_row4), "frame %d of %d", frame, last_frame);
            params->append_word(kLine4, text_row4);
            script::launch_script(kDrawViewerObjectPanel, params, nullptr, false, nullptr);
            delete_structure(params);
        }
    }
    ObjectModule* model = fetch_module(kModel);
    if (model && get_bone_count(model_of(model)))
        spin_wheels();
}

// 0x0058AB30
void PreviewEntity::own_update()
{
    if (actions_ & kCommandTogglePanel) {
        panel_shown_ = !panel_shown_;
        script::launch_script(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr);
    }
    if (actions_ & kCommandFaster)
        scale_anim_rate((is_halted_ ? saved_speed_ : speed_) + kSpeedStep);
    if (actions_ & kCommandSlower)
        scale_anim_rate((is_halted_ ? saved_speed_ : speed_) - kSpeedStep);
    if (actions_ & kCommandStepForward)
        step_animation(true);
    if (actions_ & kCommandStepBack)
        step_animation(false);
    // The rest are the viewer's (if there is one).
    static const struct {
        uint32_t command;
        uint32_t viewer_command_;
    } kViewerCommands[] = {
        {kCommandToggleMode, 0x4000},
        {kCommandCameraUp, modules::Viewer::kCommandShoulderA},
        {kCommandCameraDown, modules::Viewer::kCommandShoulderB},
        {kCommandRollLeft, 0x10000},
        {kCommandRollRight, 0x20000},
    };
    for (const auto& c : kViewerCommands) {
        if (actions_ & c.command) {
            if (modules::Viewer* viewer = modules::get_live_viewer())
                viewer->add_commands(c.viewer_command_);
        }
    }
    update_panel();
    actions_ = 0;
}

// 0x0058A060
void handle_viewer_object_input(ViewerObjectInputHandler* handler)
{
    PreviewEntity* object = handler->content_;
    const int delay = script::fetch_int_value(kViewerAutoRepeatTime);
    const int rate = script::fetch_int_value(kViewerAutoRepeatSpeed);
    int count = 0; // (the static starts as the first call's delay)
    if (!(repeat_count_guard() & 1)) {
        repeat_count_guard() |= 1;
        count = delay;
    } else {
        count = repeat_count();
    }
    if (handler->control_data_->makes_ & 0x10)
        object->actions_ |= PreviewEntity::kCommandTogglePanel;
    // A frame back / on when Left / Right is pressed, and while it's held: after `delay` frames, then every `rate`.
    const pad_input::Payload* data = handler->control_data_;
    if (data->makes_ & pad_input::Payload::kButtonLeft) {
        object->actions_ |= PreviewEntity::kCommandStepBack;
        repeat_count() = delay;
    } else if (data->makes_ & pad_input::Payload::kButtonRight) {
        object->actions_ |= PreviewEntity::kCommandStepForward;
        repeat_count() = delay;
    } else if (data->buttons_ & pad_input::Payload::kButtonLeft) {
        repeat_count() = --count;
        if (count <= 0) {
            object->actions_ |= PreviewEntity::kCommandStepBack;
            repeat_count() = rate;
        }
    } else if (data->buttons_ & pad_input::Payload::kButtonRight) {
        repeat_count() = --count;
        if (count <= 0) {
            object->actions_ |= PreviewEntity::kCommandStepForward;
            repeat_count() = rate;
        }
    } else {
        repeat_count() = delay;
    }
    if (handler->control_data_->makes_ & 0x200)
        object->actions_ |= PreviewEntity::kCommandToggleMode;
    const uint32_t buttons = handler->control_data_->buttons_;
    if (buttons & 0x8) {
        if (!(buttons & 0x2))
            object->actions_ |= PreviewEntity::kCommandCameraUp;
    } else if (buttons & 0x2) {
        object->actions_ |= PreviewEntity::kCommandCameraDown;
    }
    const uint32_t makes = handler->control_data_->makes_;
    if (makes & 0x4)
        object->actions_ |= PreviewEntity::kCommandFaster;
    else if (makes & 0x1)
        object->actions_ |= PreviewEntity::kCommandSlower;
    if (!object->fetch_module(AnimBlenderComponent::kType)) {
        const uint32_t held = handler->control_data_->buttons_;
        if (held & 0x20)
            object->actions_ |= PreviewEntity::kCommandRollLeft;
        else if (held & 0x80)
            object->actions_ |= PreviewEntity::kCommandRollRight;
    }
}

// 0x00589840
int handle_play_animation(net::InboundMsgCtx* context)
{
    object_of(context)->play_animation(*reinterpret_cast<const uint32_t*>(context->body_));
    return 1;
}

// 0x0058ACA0
int on_clip_rate_msg(net::InboundMsgCtx* context)
{
    object_of(context)->scale_anim_rate(*reinterpret_cast<const float*>(context->body_));
    return 1;
}

// 0x0058ACC0
int handle_step_animation(net::InboundMsgCtx* context)
{
    object_of(context)->step_animation(*reinterpret_cast<const bool*>(context->body_)); // (the byte as it is)
    return 1;
}

// 0x0058A1F0
int handle_reload_animation(net::InboundMsgCtx* context)
{
    const char* message = context->body_;
    object_of(context)->reload_animation(message, *reinterpret_cast<const uint32_t*>(message + 0x80));
    return 1;
}

// 0x0058ACE0
int handle_reload_cam_animation(net::InboundMsgCtx* context)
{
    const char* message = context->body_;
    script::Struct* params = spawn_param_table();
    params->append_word(0u, message);
    params->put_name_key(0u, *reinterpret_cast<const uint32_t*>(message + 0x80));
    commands::cfunc_reload_skater_cam_anim(params, nullptr);
    delete_structure(params);
    return 1;
}

// 0x0058A210
int handle_run_anim_view_script(net::InboundMsgCtx* context)
{
    const char* message = context->body_;
    PreviewEntity* object = object_of(context);
    char path[260];
    std::snprintf(path, sizeof(path), "scripts\\animview\\%s.qb", message);
    script::load_qb(path, 0, 0);
    script::Script* script = script::spawn_script(message, nullptr, 0, nullptr, 0, 0, false, false, false, false);
    script->entity_.assign_value(object); // (not null-checked: as the original)
    return 1;
}

// 0x0058ADB0
PreviewEntity::PreviewEntity(net::AppHost* app)
{
    add_base_components(this);
    change_species(kObjectTypeComposite);
    pad_input::Hub* input = input_manager();
    auto* handler = new ViewerObjectInputHandler(&handle_viewer_object_input, this, 0, 0);
    task_ = handler;
    // Onto its controller's handlers (not null-checked: as the original).
    pad_input::HostEnd& controller = input->hosts_[handler->ordinal_];
    controller.handlers_.queue_job(handler);
    handler->control_data_ = &controller.content_;
    handler->dev_ = controller.dev_;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    saved_speed_ = 0.0f;
    panel_shown_ = true;
    speed_ = 1.0f;
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&basis_frame_, kIdentity, sizeof(kIdentity));
    is_halted_ = false;
    asset_ = 0;
    owner_app_ = app;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    if (app) {
        net::Router& dispatcher = app->dispatcher_;
        dispatcher.append_listener(0x25, &handle_play_animation, 3, this, 0x80);
        dispatcher.append_listener(0x26, &on_clip_rate_msg, 3, this, 0x80);
        dispatcher.append_listener(0x27, &handle_step_animation, 3, this, 0x80);
        dispatcher.append_listener(0x28, &handle_reload_animation, 3, this, 0x80);
        dispatcher.append_listener(0x29, &handle_reload_cam_animation, 3, this, 0x80);
        dispatcher.append_listener(0x2C, &handle_run_anim_view_script, 3, this, 0x80);
    }
}

} // namespace objects
