// The flexible particle component (GameObjects::FlexibleParticleComponent) and the particle system's flag setter: see
// flexible_particle.h. From the disassembly of 0x0041D6E0-0x0041E25E.

#include "engine/objects/flexible_particle.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/particle_system.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/collide_and_die.h"
#include "engine/gfx/engine.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"

namespace render {

// 0x0041D9C0
void EmitterInstance::set_flag(int bit, bool on)
{
    if (on)
        flag_bits_ |= 1u << bit;
    else
        flag_bits_ &= ~(1u << bit);
}

} // namespace render

namespace objects {

static float g_particle_update_time{1.0f};
uint32_t g_flexible_particle_member_functions[156]{
    0x63274395, 0xC5BCEB9E, 0, 0, 0, 0, 0x8FFDEF38, 0xC5BCEB9E, 0, 0, 0, 0, 0x7911855B, 0xC5BCEB9E, 0, 0, 0, 0,
    0xDB8621F7, 0xC5BCEB9E, 0, 0, 0, 0, 0xE985A9A6, 0xC5BCEB9E, 0, 0, 0, 0, 0xE3309AB5, 0xC5BCEB9E, 0, 0, 0, 0,
    0xFBB6720D, 0xC5BCEB9E, 0, 0, 0, 0, 0xA075EE45, 0xC5BCEB9E, 0, 0, 0, 0, 0x2A479569, 0xC5BCEB9E, 0, 0, 0, 0,
    0x2E66815E, 0xC5BCEB9E, 0, 0, 0, 0, 0xD3F1D333, 0xC5BCEB9E, 0, 0, 0, 0, 0xB373FB6E, 0xC5BCEB9E, 0, 0, 0, 0,
    0x1CFBF078, 0xC5BCEB9E, 0, 0, 0, 0, 0x514B2584, 0xC5BCEB9E, 0, 0, 0, 0, 0x711B1A6A, 0xC5BCEB9E, 0, 0, 0, 0,
    0x35B65BD4, 0xC5BCEB9E, 0, 0, 0, 0, 0x099CD17B, 0xC5BCEB9E, 0, 0, 0, 0, 0x4893F05D, 0xC5BCEB9E, 0, 0, 0, 0,
    0x482482C6, 0xC5BCEB9E, 0, 0, 0, 0, 0x7F97BAD3, 0xC5BCEB9E, 0, 0, 0, 0, 0xE99D53A8, 0xC5BCEB9E, 0, 0, 0, 0,
    0xED0FE310, 0xC5BCEB9E, 0, 0, 0, 0, 0x2A1F307B, 0xC5BCEB9E, 0, 0, 0, 0, 0xABF5F9E1, 0xC5BCEB9E, 0, 0, 0, 0,
    0x83B57984, 0xC5BCEB9E, 0, 0, 0, 0, 0,          0xC5BCEB9E, 0, 0, 0, 0};

namespace {

constexpr uint32_t kName = 0xA1DC81F9;       // name
constexpr uint32_t kMax = 0x6289DD76;        // max
constexpr uint32_t kMaxStreams = 0x9E52A20F; // MaxStreams
constexpr uint32_t kTexture = 0x7D99F28D;    // texture
constexpr uint32_t kBlendMode = 0xDD6BB3D5;  // blendmode
constexpr uint32_t kTypeParam = 0x7321A8D6;  // type
constexpr uint32_t kUnknown184197d5 = 0x184197D5;
constexpr uint32_t kUnknownA605b89f = 0xA605B89F;
constexpr uint32_t kSegments = 0xD93124D6; // segments
constexpr uint32_t kUnknown4e0a65eE = 0x4E0A65EE;
constexpr uint32_t kHistory = 0xD8458FB4;            // history
constexpr uint32_t kPerm = 0xD5928F25;               // perm
constexpr uint32_t kEmitterIndependent = 0x03D4E483; // emitterIndependent
constexpr uint32_t kEmitRate = 0x8E9A640E;           // emit_rate
constexpr uint32_t kModel = 0x286A8D26;              // model
constexpr uint32_t kUseGlobalColorModulation = 0xA0E585B0;
constexpr uint32_t kGroupId = 0x87FA53ED;              // groupID
constexpr uint32_t kParams = 0x7031F10C;               // params
constexpr uint32_t kEmitScript = 0xFC57577F;           // emitscript
constexpr uint32_t kUpdateScript = 0x092276DB;         // UpdateScript
constexpr uint32_t kEnableRotate = 0xA2487CC3;         // EnableRotate
constexpr uint32_t kUpdateBrightness = 0xBBFA4B06;     // update_Brightness
constexpr uint32_t kObjId = 0x0830ECAF;                // ObjID
constexpr uint32_t kUseRandomUvQuadrants = 0x47580C70; // UseRandomUVQuadrants
constexpr uint32_t kUnknown5446aF14 = 0x5446AF14;
constexpr uint32_t kFillPath = 0x9B9381E2; // FillPath
constexpr uint32_t kUnknownDe45a90d = 0xDE45A90D;
constexpr uint32_t kPriority = 0x9D5923D8;                // priority
constexpr uint32_t kAlignTo = 0xE4247A6B;                 // align_to
constexpr uint32_t kForceParticleUpdateTime = 0x0CF3CBA1; // Force_Particle_Update_Time (a script global)
constexpr uint32_t kIfEmpty = 0x0CF50F29;                 // ifEmpty
constexpr uint32_t kIsEmitting = 0x2299A9F2;              // IsEmitting
constexpr uint32_t kApplyEnvBrightness = 0x96896315;      // ApplyEnvBrightness
constexpr uint32_t kSuspend = 0xCE0CA665;

// The particle manager's update time (0x0067D6D0: Force_Particle_Update_Time as a float).
float& particle_update_time() { return g_particle_update_time; }

// The particle manager (0x006FA7C0; no source yet).
void* particle_hub_handle() { return render::g_flexible_particle_manager; }
render::EmitterInstance* find_system(uint32_t name)
{
    return static_cast<render::FlexibleParticleManager*>(particle_hub_handle())->find_system(name);
}
// 0x0049AC60: a new system (named `name`) from the parameters.
render::EmitterInstance* start_subsystem(uint32_t name, uint32_t type, int max, int max_streams, uint32_t texture,
                                         uint32_t blend_mode, int unknowna605b89f, int segments, float unknown4e0a65ee,
                                         int history, int perm, bool use_global_color_modulation, const char* model)
{
    return static_cast<render::FlexibleParticleManager*>(particle_hub_handle())
        ->start_subsystem(name, type, max, max_streams, texture, blend_mode, unknowna605b89f, segments, unknown4e0a65ee,
                          history, perm, use_global_color_modulation, model);
}
void destroy_system_now(uint32_t name)
{
    static_cast<render::FlexibleParticleManager*>(particle_hub_handle())->destroy_system_now(name);
}
void destroy_system_when_empty(uint32_t name)
{
    static_cast<render::FlexibleParticleManager*>(particle_hub_handle())->destroy_system_when_empty(name);
}

// The system's own (no source yet).
bool system_call_member_function(render::EmitterInstance* system, uint32_t name, script::Struct* params,
                                 script::Script* script)
{
    return system->invoke_method_by_name(name, params, script);
}
void set_emit_script(render::EmitterInstance* system, uint32_t script, script::Struct* params, Entity* object)
{
    system->set_emit_script(script, params, object);
}
void set_update_script(render::EmitterInstance* system, uint32_t script, script::Struct* params, Entity* object)
{
    system->set_update_script(script, params, object);
}
void update_system(render::EmitterInstance* system, float time, int unknown) { system->tick_frame(time, unknown != 0); }
void idle_system(render::EmitterInstance* system) { system->run_emit_script(); }
// 0x00497AB0: the matrix of the system's rotation, into `out` (returned).
maths::Mat4* system_rotation(render::EmitterInstance* system, maths::Mat4* out)
{
    return system->build_emitter_orientation(out);
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The suspend component (no source yet): whether its object should be updated (0x00433EA0), suspended (+0x30, the
// getter 0x00433E90), told to start (0x00433E70).
bool suspend_should_update(uint8_t* suspend)
{
    return reinterpret_cast<DormancyModule*>(suspend)->wants_animating(nullptr);
}
void suspend_start(uint8_t* suspend) { reinterpret_cast<DormancyModule*>(suspend)->disable_lod(); }

// The object dies.
void get_kill(GameEntity* object) { object->flag_as_dead(); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0041E1E0
memory::owner<ObjectModule*> FlexibleParticleComponent::fabricate() { return new FlexibleParticleComponent; }

// 0x0041D6E0
void FlexibleParticleComponent::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(FlexibleParticleComponent::kType),
                                           g_flexible_particle_member_functions, 0x18);
}

// 0x0041DA00
FlexibleParticleComponent::FlexibleParticleComponent()
{
    system_.preceding_ = nullptr;
    system_.following_ = nullptr;
    system_.owner_object_ = nullptr;
    kind_tag_ = FlexibleParticleComponent::kType;
    system_.get_release(); // (the original lets go of the null it was just given)
    system_.owner_object_ = nullptr;
    label_ = 0;
    unknown28_ = 0;
    suspend_ = nullptr;
    brightness_model_ = nullptr;
    rotate_ = false;
    clock_ = 0.0f;
}

// 0x0041DA80 (0x0041E240 the deleting one)
FlexibleParticleComponent::~FlexibleParticleComponent()
{
    delete system_.get_pull();
    system_.get_release();
}

// 0x0041DB00
void FlexibleParticleComponent::configure_from_data(script::Struct* params)
{
    params->read_hash_value(kName, &label_, 0);
    float max_float = 1.0f;
    params->fetch_float(kMax, &max_float, 0);
    int max = get_truncated(max_float);
    int max_streams = 2;
    params->fetch_int_value(kMaxStreams, &max_streams, 0);
    uint32_t texture = 0;
    params->read_hash_value(kTexture, &texture, 0);
    uint32_t blend_mode = 0x515E298E;
    params->read_hash_value(kBlendMode, &blend_mode, 0);
    uint32_t type = 0xAAB555BB;
    params->read_hash_value(kTypeParam, &type, 0);
    params->read_hash_value(kUnknown184197d5, &unknown28_, 0);
    int unknowna605b89f = 0;
    params->fetch_int_value(kUnknownA605b89f, &unknowna605b89f, 0);
    int segments = 8;
    params->fetch_int_value(kSegments, &segments, 0);
    float unknown4e0a65ee = 0.5f;
    params->fetch_float(kUnknown4e0a65eE, &unknown4e0a65ee, 0);
    int history = 1;
    params->fetch_int_value(kHistory, &history, 0);
    int perm = 0;
    params->fetch_int_value(kPerm, &perm, 0);
    int emitter_independent = 0;
    params->fetch_int_value(kEmitterIndependent, &emitter_independent, 0);
    float emit_rate = 0.0f;
    params->fetch_float(kEmitRate, &emit_rate, 0);
    const char* model = nullptr;
    params->fetch_text(kModel, &model, 0);
    bool use_global_color_modulation = params->has_bare_flag(kUseGlobalColorModulation);
    // A permanent system of this name, if there's one; else a new one, named by the component's address.
    bool found = false;
    if (perm) {
        system_.assign_value(find_system(label_));
        found = system_.get_pull() != nullptr;
    }
    if (!found) {
        label_ = reinterpret_cast<uint32_t>(this);
        system_.assign_value(start_subsystem(reinterpret_cast<uint32_t>(this), type, max, max_streams, texture,
                                             blend_mode, unknowna605b89f, segments, unknown4e0a65ee, history, perm,
                                             use_global_color_modulation, model));
        if (!system_.get_pull())
            return;
    }
    uint32_t group_id = 0;
    if (params->read_hash_value(kGroupId, &group_id, 0))
        system_.get_pull()->group_id_ = group_id;
    system_.get_pull()->emitter_independent_ = emitter_independent > 0;
    system_.get_pull()->unknown78_ = unknown28_;
    system_.get_pull()->emit_rate_ = emit_rate;
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    uint32_t emit_script = 0;
    if (params->read_hash_value(kEmitScript, &emit_script, 0))
        set_emit_script(system_.get_pull(), emit_script, script_params, entity_);
    uint32_t update_script = 0;
    if (params->read_hash_value(kUpdateScript, &update_script, 0))
        set_update_script(system_.get_pull(), update_script, script_params, entity_);
    if (params->has_bare_flag(kEnableRotate))
        rotate_ = true;
    bool update_brightness = params->has_bare_flag(kUpdateBrightness);
    system_.get_pull()->update_brightness_ = false;
    if (update_brightness) {
        uint32_t object_id = unknown28_;
        params->read_hash_value(kObjId, &object_id, 0);
        if (object_id)
            brightness_model_ = static_cast<GameEntity*>(find_object(object_id))->fetch_module(kModel);
    }
    if (params->has_bare_flag(kUseRandomUvQuadrants))
        system_.get_pull()->flag_bits_ |= 0x4000;
    if (params->has_bare_flag(kUnknown5446aF14))
        system_.get_pull()->flag_bits_ |= 0x8000;
    if (params->has_bare_flag(kFillPath))
        system_.get_pull()->fill_path_ = true;
    if (params->has_bare_flag(kUnknownDe45a90d))
        system_.get_pull()->unknown133_ = false;
    int priority = 0;
    params->fetch_int_value(kPriority, &priority, 0);
    system_.get_pull()->rank_ = static_cast<uint8_t>(priority);
    script::VectorValue align_to;
    if (params->fetch_vec(kAlignTo, &align_to, 0)) {
        render::EmitterInstance* aligned = system_.get_pull();
        aligned->align_to_ = {align_to.x, align_to.y, align_to.z, align_to.w};
        aligned->aligned_ = true;
    }
    particle_update_time() = static_cast<float>(script::fetch_int_value(kForceParticleUpdateTime));
}

// 0x0041D740
void FlexibleParticleComponent::seal()
{
    suspend_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSuspend));
    if (suspend_)
        suspend_start(suspend_);
}

// 0x0041DF80
void FlexibleParticleComponent::tick_frame()
{
    if (!system_.get_pull()) {
        get_kill(entity_);
        return;
    }
    clock_ = static_cast<float>(frame_clock::get_slomo_frame_seconds() + clock_);
    bool update = true;
    if (suspend_ && !suspend_should_update(suspend_))
        update = false;
    if (suspend_ && reinterpret_cast<DormancyModule*>(suspend_)->far_ && !g_online_hub->in_net_game()) {
        // Suspended: only catching up, once the time reaches the particle manager's update time.
        if (clock_ >= particle_update_time()) {
            render::EmitterInstance* system = system_.get_pull();
            system->flag_bits_ |= 1;
            system_.get_pull()->unknown12f_ = true;
            if (system_.get_pull()->wants_time_) {
                update_system(system_.get_pull(), clock_, 0);
                clock_ = 0.0f;
            }
            if (render::EmitterInstance* after = system_.get_pull()) {
                after->flag_bits_ &= ~1u;
                system_.get_pull()->unknown12f_ = false;
            }
        }
    } else {
        system_.get_pull()->flag_bits_ |= 1;
        system_.get_pull()->unknown12f_ = true;
        if (update) {
            if (rotate_) {
                maths::Mat4 orientation = entity_->display_matrix_;
                orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
                maths::Mat4 rotation;
                matrix_multiply(&entity_->display_matrix_, system_rotation(system_.get_pull(), &rotation),
                                &orientation);
                system_.get_pull()->xform_ref_ = &entity_->display_matrix_;
            }
            system_.get_pull()->at_point_ = entity_->at_point_;
            render::EmitterInstance* system = system_.get_pull();
            if (system->wants_time_) {
                if (brightness_model_)
                    system->brightness_ = static_cast<MeshInstanceModule*>(brightness_model_)->brightness_;
                update_system(system_.get_pull(), clock_, 0);
                clock_ = 0.0f;
            }
        } else {
            idle_system(system_.get_pull());
        }
    }
    if (entity_->composite_flags_ & kCompositeFlagHidden) {
        system_.get_pull()->unknown12f_ = false;
        system_.get_pull()->unknown130_ = false;
    }
}

// 0x0041D760
void FlexibleParticleComponent::conceal(bool hide)
{
    if (render::EmitterInstance* system = system_.get_pull())
        system->unknown12f_ = hide;
}

// 0x0041D780
int FlexibleParticleComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kIsEmitting: {
        render::EmitterInstance* system = system_.get_pull(); // (no null check, as the original)
        return system->spewing_ || system->emit_rate_ > 0.0f ? 1 : 0;
    }
    case kApplyEnvBrightness:
        if (render::EmitterInstance* system = system_.get_pull())
            return system_call_member_function(system, kApplyEnvBrightness, params, script) ? 1 : 0;
        return 0;
    default:
        return 2;
    }
}

// 0x0041D700
int ObjectModule::dispatch_named_script_method(script::MemberFunctionEntry* entry, script::Struct* params,
                                               script::Script* script)
{
    if (entry->method_code_) {
        using Member = int(__thiscall*)(void*, uint32_t, script::Struct*, script::Script*);
        return reinterpret_cast<Member>(entry->method_code_)(reinterpret_cast<uint8_t*>(this) + entry->this_adjustment_,
                                                             entry->callsign_, params, script);
    }
    return invoke_method_by_name(entry->callsign_, params, script);
}

// 0x0041D7F0
int FlexibleParticleComponent::pass_to_system(uint32_t name, script::Struct* params, script::Script* script)
{
    render::EmitterInstance* system = system_.get_pull();
    if (!system)
        return 0;
    return system_call_member_function(system, name, params, script) ? 1 : 0;
}

// 0x0041D820
int FlexibleParticleComponent::empty_system(uint32_t, script::Struct*, script::Script*)
{
    if (render::EmitterInstance* system = system_.get_pull())
        system->particle_count_ = 0;
    return 1;
}

// 0x0041D840
int FlexibleParticleComponent::set_scripts(uint32_t, script::Struct* params, script::Script*)
{
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    if (system_.get_pull()) {
        uint32_t emit_script = 0;
        if (params->read_hash_value(kEmitScript, &emit_script, 0))
            set_emit_script(system_.get_pull(), emit_script, script_params, entity_);
        uint32_t update_script = 0;
        if (params->read_hash_value(kUpdateScript, &update_script, 0))
            set_update_script(system_.get_pull(), update_script, script_params, entity_);
    }
    return 1;
}

// 0x0041D8F0
int FlexibleParticleComponent::should_emit(uint32_t, script::Struct*, script::Script*)
{
    render::EmitterInstance* system = system_.get_pull();
    if (!system)
        return 0;
    return system->spewing_ || system->emit_rate_ > 0.0f ? 1 : 0;
}

// 0x0041D920
int FlexibleParticleComponent::emit_on(uint32_t, script::Struct*, script::Script*)
{
    if (render::EmitterInstance* system = system_.get_pull())
        system->spewing_ = true;
    return 1;
}

// 0x0041D940
int FlexibleParticleComponent::emit_off(uint32_t, script::Struct*, script::Script*)
{
    if (render::EmitterInstance* system = system_.get_pull())
        system->spewing_ = false;
    return 1;
}

// 0x0041D960
int FlexibleParticleComponent::destroy_system(uint32_t, script::Struct* params, script::Script*)
{
    int if_empty = 0;
    params->fetch_int_value(kIfEmpty, &if_empty, 0);
    if (if_empty)
        destroy_system_when_empty(label_);
    else
        destroy_system_now(label_);
    return 1;
}

// 0x0041E190
int FlexibleParticleComponent::destroy_particles(uint32_t, script::Struct*, script::Script*)
{
    if (system_.get_pull()) {
        destroy_system_now(label_);
        system_.get_release();
        system_.owner_object_ = nullptr;
    }
    get_kill(entity_);
    return 1;
}

} // namespace objects
