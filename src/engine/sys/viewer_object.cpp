// The model viewer's object (0x005897B0-0x0058AFB7). See viewer_object.h.

#include "engine/sys/viewer_object.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <cstring>
#include <array>

#include "engine/assets/resource_entry.h"
#include "engine/assets/resource_registry.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/util/text_store.h"
#include "engine/memory/task_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
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
#include "engine/objects/entity.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/rig.h"
#include "wire/net_wire.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/sys/game_config.h"

namespace objects {

namespace {

constexpr size_t kPathBytes = 256;          // a path to a sibling file (the original's buffer)
constexpr size_t kMaxPathBytes = 260;       // MAX_PATH: the texts' keys and the animation view script's path
constexpr uint32_t kNoSkeletonStart = 10;   // where the no-skeleton counter starts
constexpr float kBlendTime = 0.3F;          // the idle animation's blend
constexpr uint32_t kNoSkeletonCounterMask = 0x1F;
constexpr double kFullTurnDegrees = 360.0;
constexpr int kLastNonWheelBone = 5;        // a model with no more bones than these has no wheels to turn
constexpr int kWheelBoneEnd = 6;            // the wheels are bones 2-5; every bone but the first goes under bone 0 up to here
constexpr size_t kPanelLineBytes = 128;

} // namespace

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

// `path` set to a model's file path: "models\<name>\<name><extension>", or "models\<name>" when the name holds a path
// separator (0x004879D0, in the CAS code - not rewritten yet - makes it as a temporary String).
void set_model_path(text::Chars& path, const char* name, const char* extension)
{
    path = gfx::make_model_path(name, extension);
}

// A file's path and the extension of its sibling.
struct SiblingName {
    const char* path{};
    const char* extension{};
};

// `path` without its .skin / .mdl part, with `extension` (".col" / ".tex") on the end (0x0048BFE0 cuts the path).
void sibling_file(SiblingName sibling, std::array<char, kPathBytes>& out)
{
    const auto* const path = sibling.path;
    const auto* const extension = sibling.extension;
    render::engine::split_skin_name(path, out);
    strcat_s(out.data(), out.size(), extension);
}

// A CAS artist's cached copy of `file` dropped, so it's read again from the edited file.
void drop_cached_file(const char* file)
{
    if (script::fetch_int_value(kCasArtist) == 0) {
        return;
    }
    assets::ResourceRegistry* assets = assets::asset_manager();
    if (assets::Resource* asset = assets->obtain_loaded_item(checksums::hash_from_text(file), 0)) {
        assets->discard_loaded_item(asset);
    }
}

// Model `name`'s .col, .skin, .mdl and .tex files dropped (in that order).
void drop_cached_model(const char* name)
{
    text::Chars path;
    set_model_path(path, name, ".skin");
    std::array<char, kPathBytes> sibling{};
    sibling_file({.path = path.c_str(), .extension = ".col"}, sibling);
    drop_cached_file(sibling.data());
    drop_cached_file(path.c_str());
    set_model_path(path, name, ".mdl");
    drop_cached_file(path.c_str());
    sibling_file({.path = path.c_str(), .extension = ".tex"}, sibling);
    drop_cached_file(sibling.data());
}

AnimBlenderComponent* get_animation(GameEntity* object)
{
    return core::down_cast<AnimBlenderComponent>(object->fetch_module(AnimBlenderComponent::kType));
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
const float kStepSeconds = std::bit_cast<float>(0x3C888889U); // 1/60 s
constexpr float kSpeedStep = 0.25F;                           // 0x0064F130
constexpr float kRadiansPerDegree = 0.017453292F;             // 0x00645464

// A pooled script structure, and one cleared and freed (either may be null).

// D3DXMatrixMultiply (library code: stdcall), out = a * b.
void multiply_matrix(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    maths::matrix_multiply(out, a, b);
}

// The model component's model (+0x208), its bone count (0x00492FF0) and hierarchy (0x00493040: objects of 0x50 bytes,
// each with its matrix at +0x10) - render::RenderModel, not rewritten yet; the skeleton component's skeleton (the folded
// getter 0x005480F0: +0x18) and the skeleton's bone matrices (0x005480C0: +0x04).
render::RenderModel* model_of(ObjectModule* component)
{
    return core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get();
}
int get_bone_count(render::RenderModel* model)
{
    return static_cast<int>(model->get_asset_bone_count());
}
gfx::Rig* skeleton_of(ObjectModule* component) { return core::down_cast<objects::BoneRigModule>(component)->bone_rig(); }

// NOT ORIGINAL grouping - the viewer object's wheels and its repeat count. Made on first use, never destroyed
// (viewer_object_state()).
struct ViewerObjectState {
    uint32_t no_skeleton_counter_{kNoSkeletonStart};
    int repeat_count_{};
    uint32_t repeat_count_guard_{};
    float wheel_angle_{};
};

ViewerObjectState& viewer_object_state()
{
    static core::NoDestroy<ViewerObjectState> state;
    return state.get();
}

// The wheels' angle (degrees) and the counter that moves on while there's no skeleton.
float& wheel_angle() { return viewer_object_state().wheel_angle_; }
uint32_t& no_skeleton_counter() { return viewer_object_state().no_skeleton_counter_; }
// The input handler's auto-repeat count (a function static: its guard is bit 1 of 0x007CE58C).
int& repeat_count() { return viewer_object_state().repeat_count_; }
uint32_t& repeat_count_guard() { return viewer_object_state().repeat_count_guard_; }
pad_input::Hub* input_manager() { return pad_input::get_input_manager(); }

PreviewEntity* object_of(net::InboundMsgCtx* context) { return static_cast<PreviewEntity*>(context->content()); }

} // namespace

// 0x005897B0
void PreviewEntity::power_down() { script::launch_script(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr); }

// 0x005897D0
void PreviewEntity::play_animation(uint32_t anim)
{
    // (The original looks the component up again for each call.)
    if (!get_animation(this) || !get_animation(this)->has_value(anim)) {
        return;
    }
    const float length = get_animation(this)->fetch_value(anim);
    get_animation(this)->play_animation_ex(anim, 0, 0, std::bit_cast<uint32_t>(length), 1,
                                           std::bit_cast<uint32_t>(kBlendTime), std::bit_cast<uint32_t>(1.0F));
}

// 0x00589860
PreviewEntity::~PreviewEntity()
{
    task_->drop_out(); // (its input handler; not null-checked: as the original)
    if (asset_ != 0U) {
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
const maths::Vec4 kOriginPosition{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
} // namespace

// 0x00589910
void PreviewEntity::read_in(script::Struct* params)
{
    object_hub_slot()->enrol_subject(this);
    add_motion_components(this);
    add_node_components(this, params);
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(game_config::get_extra_memory() ? static_cast<memory::MemSource*>(memory->debug_heap())
                                                          : static_cast<memory::MemSource*>(memory->bottom_up_heap()));
    const char* name = nullptr;
    for (int i = 0; i < 4; ++i) {
        std::array<char, kMaxPathBytes> key{};
        text::format_to(key, "model%d", i);
        if (params->fetch_text(key.data(), &name, 0)) {
            drop_cached_model(name);
        }
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
            assets->load_asset(skeleton_file, assets::ResourceKind::kSkeleton, {.checksum = skeleton, .group = 0}, false, false, false, nullptr, nullptr);
            asset_ = skeleton;
        }
    }
    script::VectorValue position{};
    if (params->fetch_vec(kViewerObjPos, &position, 0)) {
        at_point().x = position.x;
        at_point().y = position.y;
        at_point().z = position.z;
        at_point().w = position.w;
    } else {
        at_point() = kOriginPosition; // (0, 0, 0, 1)
    }
    static const std::array<float, 16> kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    std::memcpy(&basis_frame(), kIdentity.data(), sizeof(kIdentity));
    script::launch_script(kAddComponents, params, this, false, nullptr);
    seal(); // (Finalize)
    script::launch_script(kSetupModelScript, params, this, false, nullptr);
    play_animation(kIdle);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00589FD0
void PreviewEntity::reload_animation(const char* file, uint32_t anim)
{
    if (!get_animation(this)) {
        return;
    }
    assets::ResourceRegistry* assets = assets::asset_manager();
    uint32_t name = checksums::hash_from_text(file);
    if (anim != 0U) {
        name = get_animation(this)->get_anim_set() + anim;
    }
    if (assets::Resource* asset = assets->obtain_loaded_item(name, 0)) {
        assets->discard_loaded_item(asset);
    }
    assets->load_anim(file, anim, get_animation(this)->get_anim_set(), false, false);
    play_animation(anim);
}

// 0x0058A2E0
void PreviewEntity::change_spin(SpinAngles spin)
{
    const auto z = spin.z;
    const auto x = spin.x;
    static const std::array<float, 16> kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    std::memcpy(&basis_frame(), kIdentity.data(), sizeof(kIdentity));
    maths::Mat4 rotation{};
    multiply_matrix(&basis_frame(), &basis_frame(), &maths::make_roll_matrix(rotation, z));
    multiply_matrix(&basis_frame(), &basis_frame(), &maths::make_pitch_matrix(rotation, x));
}

// 0x0058A360
void PreviewEntity::scale_anim_rate(float speed)
{
    speed_ = speed;
    if (speed > 1.0F) {
        speed_ = 1.0F;
    }
    speed_ = std::max(speed_, 0.0F);
    if (get_animation(this)) {
        get_animation(this)->first_channel_47e_850(speed_, 0.0F, 0.0F, 0.0F);
        const script::StructPtr params = script::make_struct();
        params->push_float(checksums::Name{kSpeed}, speed_);
        animation_base(this)->invoke_method_by_name(kSetSpeed, params.get(), nullptr);
    }
    is_halted_ = false;
}

// 0x0058A4A0
void PreviewEntity::step_animation(bool forward)
{
    if (!is_halted_) {
        saved_speed_ = speed_;
        scale_anim_rate(0.0F);
    }
    if (get_animation(this)) {
        // (The original looks the component up again for each call.)
        if (forward) {
            get_animation(this)->pass_to_each_channel(std::bit_cast<uint32_t>(kStepSeconds));
            get_animation(this)->visit_channels();
            const script::StructPtr params = script::make_struct();
            params->push_float(checksums::Name{kFrames}, kStepSeconds);
            animation_base(this)->invoke_method_by_name(kStep, params.get(), nullptr);
        } else {
            // Reversed for a step forward, then turned round again.
            get_animation(this)->first_channel_47e_810(0);
            get_animation(this)->pass_to_each_channel(std::bit_cast<uint32_t>(-kStepSeconds));
            get_animation(this)->visit_channels();
            get_animation(this)->first_channel_47e_810(0);
            animation_base(this)->invoke_method_by_name(kReverse, nullptr, nullptr);
            script::StructPtr params = script::make_struct();
            params->push_float(checksums::Name{kFrames}, -kStepSeconds);
            animation_base(this)->invoke_method_by_name(kStep, params.get(), nullptr);
            params.reset();
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
    if (!model) {
        return;
    }
    render::RenderModel* nx_model = model_of(model);
    const std::span<render::HierarchyNode> hierarchy = nx_model->asset_hierarchy_run();
    if (!fetch_module(kSkeleton)) {
        no_skeleton_counter() = (no_skeleton_counter() + 1) & kNoSkeletonCounterMask;
        return;
    }
    const gfx::Rig* skeleton = skeleton_of(fetch_module(kSkeleton));
    // The angle kept in double precision through the wrap (the original's x87 register), stored as a float each time.
    double angle = (static_cast<double>(script::fetch_float(kMaxWheelSpeed)) * speed_) + wheel_angle();
    wheel_angle() = static_cast<float>(angle);
    if (angle > kFullTurnDegrees) {
        angle -= kFullTurnDegrees;
        while (angle > kFullTurnDegrees) {
            angle -= kFullTurnDegrees;
        }
        wheel_angle() = static_cast<float>(angle);
    }
    if (angle < 0.0) {
        angle += kFullTurnDegrees;
        while (angle < 0.0) {
            angle += kFullTurnDegrees;
        }
        wheel_angle() = static_cast<float>(angle);
    }
    if (get_bone_count(nx_model) <= kLastNonWheelBone) {
        return;
    }
    // The bones from the model's hierarchy, the wheels (2-5) turned about x, and all but the first put under it.
    const std::span<maths::Mat4> bones = skeleton->matrices();
    for (int i = 0; i < get_bone_count(nx_model); ++i) {
        core::at(bones, i) = core::at(hierarchy, i).matrix();
    }
    for (int wheel = 2; wheel < kWheelBoneEnd; ++wheel) {
        maths::Mat4 spin{};
        const auto radians = static_cast<float>(static_cast<double>(wheel_angle()) * kRadiansPerDegree);
        multiply_matrix(&core::at(bones, wheel), &maths::make_pitch_matrix(spin, radians), &core::at(bones, wheel));
    }
    for (int bone = 1; bone < kWheelBoneEnd; ++bone) {
        multiply_matrix(&core::at(bones, bone), &core::at(bones, bone), &core::at(bones, 0));
    }
}

// 0x0058A8B0
void PreviewEntity::update_panel()
{
    if (get_animation(this) && panel_shown_) {
        const uint32_t anim = get_animation(this)->get_timeline30();
        if (get_animation(this)->has_value(anim)) {
            const script::StructPtr params = script::make_struct();
            std::array<char, kPanelLineBytes> line1{};
            std::array<char, kPanelLineBytes> line2{};
            std::array<char, kPanelLineBytes> line3{};
            std::array<char, kPanelLineBytes> text_row4{};
            text::format_to(line1, "name %s", "Unknown");
            params->append_word(kLine1, line1.data());
            text::format_to(line2, "speed %f", is_halted_ ? 0.0 : static_cast<double>(speed_));
            params->append_word(kLine2, line2.data());
            // (Each value looked up again for each line, the length before the time.)
            uint32_t current = get_animation(this)->get_timeline30();
            float length = get_animation(this)->fetch_value(current);
            float time = get_animation(this)->get_timeline18();
            text::format_to(line3, "time %f of %f", static_cast<double>(time), static_cast<double>(length));
            params->append_word(kLine3, line3.data());
            current = get_animation(this)->get_timeline30();
            length = get_animation(this)->fetch_value(current);
            const int last_frame = static_cast<int>(static_cast<double>(length) * 60.0);
            time = get_animation(this)->get_timeline18();
            const int frame = static_cast<int>(static_cast<double>(time) * 60.0);
            text::format_to(text_row4, "frame %d of %d", frame, last_frame);
            params->append_word(kLine4, text_row4.data());
            script::launch_script(kDrawViewerObjectPanel, params.get(), nullptr, false, nullptr);
        }
    }
    ObjectModule* model = fetch_module(kModel);
    if (model && (get_bone_count(model_of(model)) != 0)) {
        spin_wheels();
    }
}

// 0x0058AB30
void PreviewEntity::own_update()
{
    if ((actions_ & kCommandTogglePanel) != 0U) {
        panel_shown_ = !panel_shown_;
        script::launch_script(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr);
    }
    if ((actions_ & kCommandFaster) != 0U) {
        scale_anim_rate((is_halted_ ? saved_speed_ : speed_) + kSpeedStep);
    }
    if ((actions_ & kCommandSlower) != 0U) {
        scale_anim_rate((is_halted_ ? saved_speed_ : speed_) - kSpeedStep);
    }
    if ((actions_ & kCommandStepForward) != 0U) {
        step_animation(true);
    }
    if ((actions_ & kCommandStepBack) != 0U) {
        step_animation(false);
    }
    // The rest are the viewer's (if there is one).
    struct ViewerCommand {
        uint32_t command;
        uint32_t viewer_command_;
    };
    static const auto kViewerCommands = std::to_array<ViewerCommand>({
        {.command = kCommandToggleMode, .viewer_command_ = 0x4000},
        {.command = kCommandCameraUp, .viewer_command_ = modules::Viewer::kCommandShoulderA},
        {.command = kCommandCameraDown, .viewer_command_ = modules::Viewer::kCommandShoulderB},
        {.command = kCommandRollLeft, .viewer_command_ = 0x10000},
        {.command = kCommandRollRight, .viewer_command_ = 0x20000},
    });
    for (const auto& entry : kViewerCommands) {
        if ((actions_ & entry.command) != 0U) {
            if (modules::Viewer* viewer = modules::get_live_viewer()) {
                viewer->add_commands(entry.viewer_command_);
            }
        }
    }
    update_panel();
    actions_ = 0;
}

// 0x0058A060
void handle_viewer_object_input(ViewerObjectInputHandler* handler)
{
    PreviewEntity* object = handler->content();
    const int delay = script::fetch_int_value(kViewerAutoRepeatTime);
    const int rate = script::fetch_int_value(kViewerAutoRepeatSpeed);
    int count = 0; // (the static starts as the first call's delay)
    if ((repeat_count_guard() & 1) == 0U) {
        repeat_count_guard() |= 1;
        count = delay;
    } else {
        count = repeat_count();
    }
    if ((handler->control_data()->makes() & pad_input::kMaskTriangle) != 0U) {
        object->actions() |= PreviewEntity::kCommandTogglePanel;
    }
    // A frame back / on when Left / Right is pressed, and while it's held: after `delay` frames, then every `rate`.
    const pad_input::Payload* data = handler->control_data();
    if ((data->makes() & pad_input::Payload::kButtonLeft) != 0U) {
        object->actions() |= PreviewEntity::kCommandStepBack;
        repeat_count() = delay;
    } else if ((data->makes() & pad_input::Payload::kButtonRight) != 0U) {
        object->actions() |= PreviewEntity::kCommandStepForward;
        repeat_count() = delay;
    } else if ((data->buttons() & pad_input::Payload::kButtonLeft) != 0U) {
        repeat_count() = --count;
        if (count <= 0) {
            object->actions() |= PreviewEntity::kCommandStepBack;
            repeat_count() = rate;
        }
    } else if ((data->buttons() & pad_input::Payload::kButtonRight) != 0U) {
        repeat_count() = --count;
        if (count <= 0) {
            object->actions() |= PreviewEntity::kCommandStepForward;
            repeat_count() = rate;
        }
    } else {
        repeat_count() = delay;
    }
    if ((handler->control_data()->makes() & pad_input::kMaskLeftStickClick) != 0U) {
        object->actions() |= PreviewEntity::kCommandToggleMode;
    }
    const uint32_t buttons = handler->control_data()->buttons();
    if ((buttons & pad_input::kMaskR1) != 0U) {
        if ((buttons & 0x2) == 0U) {
            object->actions() |= PreviewEntity::kCommandCameraUp;
        }
    } else if ((buttons & 0x2) != 0U) {
        object->actions() |= PreviewEntity::kCommandCameraDown;
    }
    const uint32_t makes = handler->control_data()->makes();
    if ((makes & 0x4) != 0U) {
        object->actions() |= PreviewEntity::kCommandFaster;
    } else if ((makes & 0x1) != 0U) {
        object->actions() |= PreviewEntity::kCommandSlower;
    }
    if (!object->fetch_module(AnimBlenderComponent::kType)) {
        const uint32_t held = handler->control_data()->buttons();
        if ((held & pad_input::kMaskCircle) != 0U) {
            object->actions() |= PreviewEntity::kCommandRollLeft;
        } else if ((held & pad_input::kMaskSquare) != 0U) {
            object->actions() |= PreviewEntity::kCommandRollRight;
        }
    }
}

// 0x00589840
int handle_play_animation(net::InboundMsgCtx* context)
{
    object_of(context)->play_animation(net::wire::value_at<uint32_t>(context->body()));
    return 1;
}

// 0x0058ACA0
int on_clip_rate_msg(net::InboundMsgCtx* context)
{
    object_of(context)->scale_anim_rate(net::wire::value_at<float>(context->body()));
    return 1;
}

// 0x0058ACC0
int handle_step_animation(net::InboundMsgCtx* context)
{
    object_of(context)->step_animation(net::wire::value_at<uint8_t>(context->body()) != 0); // (the byte as a flag)
    return 1;
}

// 0x0058A1F0
int handle_reload_animation(net::InboundMsgCtx* context)
{
    const char* message = net::wire::text_of(context->body());
    object_of(context)->reload_animation(message, net::wire::value_at<uint32_t>(context->body(), viewer_message::kSecondTextOffset));
    return 1;
}

// 0x0058ACE0
int handle_reload_cam_animation(net::InboundMsgCtx* context)
{
    const char* message = net::wire::text_of(context->body());
    script::StructPtr params = script::make_struct();
    params->append_word(0U, message);
    params->put_name_key(checksums::Name{0U},
                         net::wire::value_at<uint32_t>(context->body(), viewer_message::kSecondTextOffset));
    commands::cfunc_reload_skater_cam_anim(params.get(), nullptr);
    params.reset();
    return 1;
}

// 0x0058A210
int handle_run_anim_view_script(net::InboundMsgCtx* context)
{
    const char* message = net::wire::text_of(context->body());
    PreviewEntity* object = object_of(context);
    std::array<char, kMaxPathBytes> path{};
    text::format_to(path, "scripts\\animview\\%s.qb", message);
    script::load_qb(path.data(), 0, 0);
    script::Script* script = script::spawn_script(message, nullptr);
    script->entity().assign_value(object); // (not null-checked: as the original)
    return 1;
}

// 0x0058ADB0
PreviewEntity::PreviewEntity(net::AppHost* app) : owner_app_(app)
{
    add_base_components(this);
    change_species(kObjectTypeComposite);
    pad_input::Hub* input = input_manager();
    gsl::owner<ViewerObjectInputHandler*> handler{
        memory::make_owned<ViewerObjectInputHandler>(&handle_viewer_object_input, this, 0, 0)};
    task_ = handler;
    // Onto its controller's handlers (not null-checked: as the original).
    pad_input::HostEnd& controller = core::at(input->hosts(), handler->ordinal());
    controller.handlers().queue_job(handler);
    handler->set_control_data(&controller.content());
    handler->set_dev(controller.dev());
    static const std::array<float, 16> kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    std::memcpy(&basis_frame(), kIdentity.data(), sizeof(kIdentity));
    if (app) {
        net::Router& dispatcher = app->dispatcher();
        dispatcher.append_listener(viewer_message::kPlayAnimation, &handle_play_animation, 3, this, viewer_message::kListenerPriority);
        dispatcher.append_listener(viewer_message::kClipRate, &on_clip_rate_msg, 3, this, viewer_message::kListenerPriority);
        dispatcher.append_listener(viewer_message::kStepAnimation, &handle_step_animation, 3, this, viewer_message::kListenerPriority);
        dispatcher.append_listener(viewer_message::kReloadAnimation, &handle_reload_animation, 3, this, viewer_message::kListenerPriority);
        dispatcher.append_listener(viewer_message::kReloadCamAnimation, &handle_reload_cam_animation, 3, this, viewer_message::kListenerPriority);
        dispatcher.append_listener(viewer_message::kRunAnimViewScript, &handle_run_anim_view_script, 3, this, viewer_message::kListenerPriority);
    }
}

} // namespace objects
