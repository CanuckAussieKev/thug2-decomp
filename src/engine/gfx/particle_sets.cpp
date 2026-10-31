// The particle component's particle systems and their manager (Nx): see particle_sets.h. From the disassembly of
// 0x00495260-0x004968AF.

#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_particles.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/gfx/particle_maths.h"
#include "engine/core/game_vtables.h"
#include "engine/core/stubs.h"
#include "engine/objects/movie_manager.h"

namespace render {

namespace {

void** const& g_particle_vtable = vt::kEngineParticles;
extern void** const kManagerVtable;
constexpr float kByteToUnit = 1.0f / 255.0f;    // 0x00645D70 (the bit pattern 0x3B808081)
constexpr uint32_t kObjectIdParam = 0x40C698AF; // (the emit script's parameter: the object's id)

bool is_game_halted() { return objects::g_simulation_halted; }

// The system's virtual functions.
Sparkle* erase_one(Sparkle* particle, unsigned int flags)
{
    return reinterpret_cast<Sparkle*(__thiscall*)(Sparkle*, unsigned int)>(particle->vtable[0])(particle, flags);
}
void initialise(Sparkle* particle) { reinterpret_cast<void(__thiscall*)(Sparkle*)>(particle->vtable[1])(particle); }
void colour_changed(Sparkle* particle) { reinterpret_cast<void(__thiscall*)(Sparkle*)>(particle->vtable[6])(particle); }

script::Struct* spawn_param_table()
{
    script::Struct* structure = script::spawn_param_bag();
    return structure ? structure->prepare_all() : nullptr;
}

// The engine's helpers (not yet rewritten; register arguments).
// 0x00495130 / 0x004951B0 (ECX a, EDX b, ESI out; returns out): each component b's where it's below / above a's.
maths::Vec4* call_min_or_max(uint32_t code, const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* destination)
{
    maths::Vec4* result;
    __asm {
        mov ecx, a
        mov edx, b
        mov esi, destination
        mov eax, code
        call eax
        mov result, eax
    }
    return result;
}
maths::Vec4* min_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* out)
{
    return call_min_or_max(reinterpret_cast<uint32_t>(code_address(&min_vector_stub)), a, b, out);
}
maths::Vec4* max_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* out)
{
    return call_min_or_max(reinterpret_cast<uint32_t>(code_address(&max_vector_stub)), a, b, out);
}
// 0x00494F10 (thiscall on `out`, the 14 words of arguments popped by the caller): per component, the extreme of the
// quadratic through `start` (at 0), `mid` (at `midTime`) and `end` (at `life`); w 0. Returns out.
maths::Vec4* curve_extremes(maths::Vec4* destination, const maths::Vec4& start, const maths::Vec4& mid,
                            const maths::Vec4& end, float mid_time, float life)
{
    uint32_t args[14];
    std::memcpy(args, &start, 16);
    std::memcpy(args + 4, &mid, 16);
    std::memcpy(args + 8, &end, 16);
    std::memcpy(args + 12, &mid_time, 4);
    std::memcpy(args + 13, &life, 4);
    uint32_t* words = args;
    void* code = code_address(&quadratic_lowest_stub);
    maths::Vec4* result;
    __asm {
        sub esp, 0x38
        mov esi, words
        mov edi, esp
        mov ecx, 14
        rep movsd
        mov ecx, destination
        mov eax, code
        call eax
        add esp, 0x38
        mov result, eax
    }
    return result;
}

float current_round(double value) { return static_cast<float>(value); }

} // namespace

// --- the parameters ---

// 0x00496240
SparkleConfig* SparkleConfig::make_new()
{
    unknown04_ = 0xDEDFC057;
    flag151_ = true;
    local_positions_ = false;
    orient_with_object_ = false;
    for (int i = 0; i < 3; ++i) {
        extent_[i] = 1.0f;
        radius_[i] = 0.0f;
        at_point_[i] = maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        box_[i] = maths::Vec4{20.0f, 20.0f, 20.0f, 0.0f};
        offset_pos_[i] = maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k)
            shade_[i][k] = 0x80;
    }
    flag153_ = false;
    unknown140_ = 0;
    unknown138_ = 0;
    unknown30_ = 50.0f;
    unknown24_ = 2;
    unknown28_ = 300.0f;
    unknown2c_ = 4.0f;
    unknown34_ = 1.0f;
    unknown124_ = 50.0f;
    unknown134_ = 0xA86285A1;
    unknown13c_ = 1;
    for (float& scale : colour_scale_)
        scale = 1.0f;
    unknown14c_ = 0;
    unknown155[0] = 0;
    unknown155[1] = 0;
    flag158_ = false;
    entity_key_ = 0;
    unknown144_ = 400;
    unknown148_ = 401;
    return this;
}

// 0x00495C80
SparkleConfig* SparkleConfig::get_assign(const SparkleConfig& other)
{
    std::memcpy(this, &other, offsetof(SparkleConfig, unknown159));
    return this;
}

// --- the system ---

// 0x00496360
Sparkle* Sparkle::make_new()
{
    vtable = g_particle_vtable;
    args_.make_new();
    new (&extent_box_) maths::BBox();
    return this;
}

// 0x00496380
void Sparkle::prepare_all(const SparkleConfig* params)
{
    args_.get_assign(*params);
    update_bounds();
    initialise(this);
}

// 0x00495260. The corners are (position, size) less / plus (half the box, half the radius), the size and half the
// radius; the original's x87 code rounds some of the sums to floats and keeps others at 53 bits - followed term by term.
void Sparkle::update_bounds()
{
    const SparkleConfig& p = args_;
    float hb[3][3], hr[3], s[3];
    for (int i = 0; i < 3; ++i) {
        hb[i][0] = p.box_[i].x * 0.5f;
        hb[i][1] = p.box_[i].y * 0.5f;
        hb[i][2] = p.box_[i].z * 0.5f;
        hr[i] = p.radius_[i] * 0.5f;
        s[i] = p.extent_[i];
    }
    maths::Vec4 lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& at = p.at_point_[i];
        lo[i].x = current_round(current_round(current_round(static_cast<double>(at.x) - hb[i][0]) - s[i]) - hr[i]);
        lo[i].y = current_round(current_round(current_round(static_cast<double>(at.y) - hb[i][1]) - s[i]) - hr[i]);
        lo[i].z = current_round(current_round(current_round(static_cast<double>(at.z) - hb[i][2]) - s[i]) - hr[i]);
        lo[i].w = current_round(current_round(current_round(static_cast<double>(s[i]) - hr[i]) - s[i]) - hr[i]);
    }
    {
        const maths::Vec4& at = p.at_point_[0];
        hi[0].x =
            current_round((static_cast<double>(current_round(static_cast<double>(hb[0][0]) + at.x)) + s[0]) + hr[0]);
        hi[0].y = current_round(((static_cast<double>(at.y) + hb[0][1]) + s[0]) + hr[0]);
        hi[0].z = current_round(static_cast<double>(current_round(
                                    s[0] + static_cast<double>(current_round(static_cast<double>(at.z) + hb[0][2])))) +
                                hr[0]);
        hi[0].w = current_round(static_cast<double>(current_round(
                                    s[0] + static_cast<double>(current_round(static_cast<double>(hr[0]) + s[0])))) +
                                hr[0]);
    }
    for (int i = 1; i < 3; ++i) {
        const maths::Vec4& at = p.at_point_[i];
        hi[i].x = current_round(current_round(current_round(static_cast<double>(hb[i][0]) + at.x) + s[i]) + hr[i]);
        hi[i].y = current_round(current_round(current_round(static_cast<double>(at.y) + hb[i][1]) + s[i]) + hr[i]);
        hi[i].z =
            current_round((s[i] + static_cast<double>(current_round(static_cast<double>(at.z) + hb[i][2]))) + hr[i]);
        hi[i].w = current_round(static_cast<double>(current_round(s[i] + (static_cast<double>(hr[i]) + s[i]))) + hr[i]);
    }
    maths::Vec4 low, high, curve;
    low = *min_of(&lo[0], &lo[2], &curve);
    high = *max_of(&hi[0], &hi[2], &curve);
    if (p.flag151_) { // (the paths go through the mid: their extremes on the way)
        float life = p.unknown2c_;
        float mid_time = current_round(static_cast<double>(life) * p.unknown30_ * 0.01f);
        maths::Vec4 extreme;
        curve_extremes(&extreme, lo[0], lo[1], lo[2], mid_time, life);
        low = *min_of(&low, &extreme, &curve);
        curve_extremes(&extreme, hi[0], hi[1], hi[2], mid_time, life);
        high = *max_of(&high, &extreme, &curve);
    }
    extent_box_.min = low;
    extent_box_.max = high;
    // the sphere about the box
    float half_x = current_round((static_cast<double>(high.x) - low.x) * 0.5f);
    double half_y = (static_cast<double>(high.y) - low.y) * 0.5f;
    double half_z = static_cast<double>(current_round(static_cast<double>(high.z) - low.z)) * 0.5f;
    bound_ball_.w = low.w; // (overwritten below, as the original)
    bound_ball_.z = current_round(half_z + low.z);
    bound_ball_.x = current_round(static_cast<double>(half_x) + low.x);
    bound_ball_.y = current_round(half_y + low.y);
    bound_ball_.w =
        current_round(std::sqrt((static_cast<double>(half_x) * half_x + half_z * half_z) + half_y * half_y));
}

// 0x00496030
void Sparkle::change_place(int which, const maths::Vec4* pos) { args_.at_point_[which] = *pos; }

// 0x00496160
void Sparkle::set_box(int which, const maths::Vec4* box) { args_.box_[which] = *box; }

// 0x00496060
void Sparkle::spin_by(int which, const maths::Mat4* matrix)
{
    const maths::Vec4 start = args_.at_point_[0];
    maths::Vec4& pos = args_.at_point_[which];
    pos.x -= start.x;
    pos.y -= start.y;
    pos.z -= start.z;
    pos.w -= start.w;
    maths::Vec4 turned;
    pos = matrix->xform(turned, pos);
    maths::Vec4& box = args_.box_[which];
    box = matrix->xform(turned, box);
    pos.x = start.x + pos.x;
    pos.y = start.y + pos.y;
    pos.z = start.z + pos.z;
    pos.w = start.w + pos.w;
    args_.orientation_turn_ = *matrix;
}

// 0x00496190
void Sparkle::change_colour(const uint8_t* rgba)
{
    if (!rgba) {
        colour_changed(this);
        return;
    }
    if (args_.flag158_) {
        for (int i = 0; i < 4; ++i)
            args_.colour_scale_[i] = static_cast<float>(rgba[i]) * kByteToUnit;
        colour_changed(this);
    }
}

// 0x00496210
void Sparkle::set_colour_scale(const float* rgba)
{
    if (args_.flag158_) {
        for (int i = 0; i < 4; ++i)
            args_.colour_scale_[i] = rgba[i];
        colour_changed(this);
    }
}

// 0x004963B0
void Sparkle::set_emit_script(uint32_t script, uint32_t object_id)
{
    if (args_.emit_script_)
        script::stop_scripts_by_tag(args_.emit_script_);
    args_.emit_script_ = script;
    args_.entity_key_ = object_id;
    script::Struct* params = spawn_param_table();
    params->put_name_key(kObjectIdParam, args_.entity_key_); // (not checked for null, as the original)
    script::spawn_script(args_.emit_script_, params, 0, nullptr, 0, 0, false, false, false, false);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

void Sparkle::render(bool advance) { reinterpret_cast<PcParticleSystem*>(this)->engine_render(advance); }
void Sparkle::tick_frame() { reinterpret_cast<PcParticleSystem*>(this)->engine_update(); }
void Sparkle::get_release() { reinterpret_cast<PcParticleSystem*>(this)->engine_release(); }

// --- the manager ---

// 0x00496770
EffectsDirector* EffectsDirector::make_new()
{
    vtable = kManagerVtable;
    systems_.size = 0;
    auto* head = static_cast<objects::PointerListNode*>(memory::alloc(sizeof(objects::PointerListNode)));
    if (head) {
        head->next = head;
        head->prev = head;
    }
    systems_.head = head;
    for (float& c : shade_)
        c = 1.0f;
    return this;
}

// 0x004967D0
void EffectsDirector::destroy_body()
{
    vtable = kManagerVtable;
    objects::PointerListNode* result;
    for (int pass = 0; pass < 2; ++pass) {
        objects::PointerListNode* head = systems_.head;
        systems_.get_erase(&result, head ? head->next : nullptr, head);
    }
    memory::dealloc(systems_.head);
}

// 0x00496860
EffectsDirector* EffectsDirector::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x00496510
Sparkle* EffectsDirector::spawn_particle(const SparkleConfig* params)
{
    Sparkle* particle = reinterpret_cast<Sparkle*(__thiscall*)(EffectsDirector*)>(vtable[1])(this);
    if (particle) {
        particle->prepare_all(params);
        if (particle->args_.flag158_) {
            for (int i = 0; i < 4; ++i)
                particle->args_.colour_scale_[i] = shade_[i];
            particle->set_colour_scale(shade_);
        }
        systems_.splice(systems_.head, reinterpret_cast<void* const*>(&particle));
    }
    return particle;
}

// 0x00496590
void EffectsDirector::retire_particle(Sparkle* particle)
{
    if (particle->args_.emit_script_) {
        script::stop_scripts_by_tag(particle->args_.emit_script_);
        particle->args_.emit_script_ = 0;
    }
    objects::PointerListNode* node = systems_.head ? systems_.head->next : nullptr;
    if (node == systems_.head)
        return;
    while (static_cast<Sparkle*>(node->data)->args_.label_ != particle->args_.label_) {
        node = node->next;
        if (node == systems_.head)
            return;
    }
    auto* found = static_cast<Sparkle*>(node->data);
    found->get_release();
    if (found)
        erase_one(found, 1);
    if (node != systems_.head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        memory::dealloc(node);
        --systems_.size;
    }
}

// 0x00496640
void EffectsDirector::draw_particles()
{
    if (!systems_.size)
        return;
    for (objects::PointerListNode* node = systems_.head ? systems_.head->next : nullptr; node != systems_.head;
         node = node->next)
        static_cast<Sparkle*>(node->data)->render(!is_game_halted());
}

// 0x00496680
void EffectsDirector::tick_particles()
{
    for (objects::PointerListNode* node = systems_.head ? systems_.head->next : nullptr; node != systems_.head;
         node = node->next)
        static_cast<Sparkle*>(node->data)->tick_frame();
}

// 0x004966B0
void EffectsDirector::change_colour(const float* rgba)
{
    for (objects::PointerListNode* node = systems_.head ? systems_.head->next : nullptr; node != systems_.head;
         node = node->next)
        static_cast<Sparkle*>(node->data)->set_colour_scale(rgba);
    for (int i = 0; i < 4; ++i)
        shade_[i] = rgba[i];
}

// 0x00496700
void EffectsDirector::kill_every_particle()
{
    for (objects::PointerListNode* node = systems_.head ? systems_.head->next : nullptr; node != systems_.head;
         node = node->next) {
        auto* particle = static_cast<Sparkle*>(node->data);
        particle->get_release();
        if (particle)
            erase_one(particle, 1);
    }
    objects::PointerListNode* result;
    objects::PointerListNode* head = systems_.head;
    systems_.get_erase(&result, head ? head->next : nullptr, head);
}

namespace {
static VtableSlot g_manager_vtable_slots[] = {&render::EffectsDirector::teardown, stubs::return_zero};
void** const kManagerVtable = reinterpret_cast<void**>(g_manager_vtable_slots);
} // namespace

} // namespace render

namespace objects {

// 0x004964A0
void PointerList::splice(PointerListNode* position, void* const* value)
{
    auto* node = static_cast<PointerListNode*>(memory::alloc(sizeof(PointerListNode)));
    if (node) {
        node->data = *value;
        node->prev = position->prev;
        node->next = position;
    }
    ++size;
    position->prev = node;
    node->prev->next = node; // (with no node, the original reads through null here)
}

} // namespace objects

namespace vt {
static VtableSlot g_engine_particles_slots[] = {&render::PcParticleSystem::engine_destroy,
                                                stubs::do_nothing,
                                                stubs::do_nothing,
                                                stubs::ignore_argument,
                                                stubs::ignore_argument,
                                                stubs::do_nothing,
                                                stubs::do_nothing,
                                                stubs::return_true};
void** const kEngineParticles = reinterpret_cast<void**>(g_engine_particles_slots);
} // namespace vt
