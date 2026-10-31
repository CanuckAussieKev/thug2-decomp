// The engine's flexible particle manager: see flexible_particle_manager.h. From the disassembly of 0x0049A8E0-0x0049B05E
// (0x0049A8E0 isn't a Ghidra function).

#include "engine/gfx/flexible_particle_manager.h"

#include <cstddef>
#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/objects/movie_manager.h"
#include "platform/crt.h"

namespace render {

float g_frame_time_scale;
int g_drawn_viewport;
namespace {

const char* const kHeapName = "FlxParticles";     // "FlxParticles"
const char* const kTempHeapName = "TEMPFlxPrtcl"; // "TEMPFlxPrtcl"
constexpr uint32_t kHeapSizeSymbol = 0x91CB7552;
constexpr uint32_t kFrameRateSymbol = 0x3214C818;
constexpr int kDefaultHeapSize = 0x5000;
constexpr float kFramesPerSecond = 60.0f; // 0x00645EA0

float& frame_time_scale() { return g_frame_time_scale; }
bool is_game_halted() { return objects::g_simulation_halted; }
int drawn_viewport() { return g_drawn_viewport; }

// The list's head deleted (0x0049AEF0).
void delete_head(memory::ListNode* head) { memory::ListNode::destroy(head); }

// The list's systems: the first after the head, the one after `system` - null at the head (its data -1) or a null link.
EmitterInstance* system_of(memory::ListNode* node)
{
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return nullptr;
    return reinterpret_cast<EmitterInstance*>(reinterpret_cast<uint8_t*>(node) - offsetof(EmitterInstance, list_link_));
}
EmitterInstance* first_system(memory::ListNode* head) { return head ? system_of(head->next) : nullptr; }
EmitterInstance* next_system(EmitterInstance* system) { return system_of(system->list_link_.next); }

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0049AF20
FlexibleParticleManager::FlexibleParticleManager()
{
    int size = script::fetch_int_value(kHeapSizeSymbol);
    if (!size)
        size = kDefaultHeapSize;
    zone_ = new memory::MemoryArena(static_cast<uint32_t>(size));
    arena_ = memory::Hub::get_pull()->add_heap(zone_, 1, kHeapName);
    temp_region_ = nullptr;
    temp_heap_ = nullptr;
    systems_ptr_ = memory::ListNode::create_head();
    colour_scale_[0] = colour_scale_[1] = colour_scale_[2] = colour_scale_[3] = 1.0f;
    float rate = script::fetch_float(kFrameRateSymbol);
    // (0 gives 60; anything else, NaN too, 60 / it)
    frame_time_scale() =
        rate == 0.0f ? kFramesPerSecond : static_cast<float>(kFramesPerSecond / static_cast<double>(rate));
}

// 0x0049AA10
FlexibleParticleManager::~FlexibleParticleManager()
{
    if (systems_ptr_)
        delete_head(systems_ptr_);
    delete_temp_heap();
    memory::Hub::get_pull()->delete_heap(reinterpret_cast<memory::MemSource*>(arena_));
    delete zone_;
}

// 0x0049A9D0
void FlexibleParticleManager::delete_temp_heap()
{
    if (!temp_heap_)
        return;
    memory::Hub::get_pull()->delete_heap(reinterpret_cast<memory::MemSource*>(temp_heap_));
    delete temp_region_;
    temp_heap_ = nullptr;
    temp_region_ = nullptr;
}

// 0x0049AA80
bool FlexibleParticleManager::create_temp_heap(uint32_t size)
{
    delete_temp_heap();
    temp_region_ = new memory::MemoryArena(size);
    if (!temp_region_)
        return false;
    temp_heap_ = memory::Hub::get_pull()->add_heap(temp_region_, 1, kTempHeapName);
    return temp_heap_ != nullptr;
}

// 0x0049A8E0
bool FlexibleParticleManager::has_temp_heap() { return temp_heap_ != nullptr; }

// 0x0049A8F0
int __cdecl FlexibleParticleManager::compare_distance(const void* a, const void* b)
{
    auto* camera = fetch_live_camera(drawn_viewport());
    const maths::Vec4& at = *camera->fetch_location();
    const EmitterInstance* first = *static_cast<EmitterInstance* const*>(a);
    const EmitterInstance* second = *static_cast<EmitterInstance* const*>(b);
    // (the original keeps some differences as floats and others at 53 bits)
    float x1 = static_cast<float>(static_cast<double>(first->at_point_.x) - at.x);
    float y1 = static_cast<float>(static_cast<double>(first->at_point_.y) - at.y);
    float z1 = static_cast<float>(static_cast<double>(first->at_point_.z) - at.z);
    float x2 = static_cast<float>(static_cast<double>(second->at_point_.x) - at.x);
    double y2 = static_cast<double>(second->at_point_.y) - at.y;
    double z2 = static_cast<double>(second->at_point_.z) - at.z;
    double distance2 = (z2 * z2 + y2 * y2) + static_cast<double>(x2) * x2;
    double distance1 = (static_cast<double>(z1) * z1 + static_cast<double>(y1) * y1) + static_cast<double>(x1) * x1;
    int order = truncate(distance2 - distance1);
    if (order)
        return order;
    return static_cast<int>(second->rank_) - static_cast<int>(first->rank_);
}

// 0x0049AB50
void FlexibleParticleManager::render_systems(bool force)
{
    EmitterInstance* systems[256] = {};
    int count = 0;
    for (EmitterInstance* system = first_system(systems_ptr_); system; system = next_system(system)) {
        if (system->wants_time_)
            systems[count++] = system; // (no bound, as the original)
    }
    if (!count)
        return;
    crt::get_qsort(systems, static_cast<size_t>(count), sizeof(systems[0]), &compare_distance); // (the game's qsort)
    bool always = force || is_game_halted();
    for (int i = 0; i < count; ++i)
        systems[i]->render_if_visible(always);
}

// 0x0049AC20
EmitterInstance* FlexibleParticleManager::find_system(uint32_t name)
{
    for (EmitterInstance* system = first_system(systems_ptr_); system; system = next_system(system)) {
        if (system->label_ == name)
            return system;
    }
    return nullptr;
}

// 0x0049AC60
EmitterInstance* FlexibleParticleManager::start_subsystem(uint32_t name, uint32_t type, int particle_cap,
                                                          int max_streams, uint32_t texture, uint32_t blend_mode,
                                                          int fixed_alpha, int segments, float unknown, int history,
                                                          int perm, bool use_level_colour, const char* model)
{
    memory::Hub::get_pull()->enter_heap_scope(reinterpret_cast<memory::MemSource*>(temp_heap_ ? temp_heap_ : arena_));
    EmitterInstance* system = (create_flexible_particles(name, type, particle_cap, max_streams, texture, blend_mode,
                                                         fixed_alpha, segments, unknown, history, model));
    memory::Hub::get_pull()->leave_heap_scope();
    if (!system)
        return nullptr;
    if (!(system->flag_bits_ & 4u)) { // (not everything allocated)
        delete system;
        return nullptr;
    }
    system->unknown12e_ = perm != 0;
    system->flag_bits_ |= 1u;
    system->set_wants_time(1);
    system->flag_bits_ |= 0x10000u;
    system->spewing_ = false;
    if (use_level_colour) {
        system->flag_bits_ |= 0x10000u;
        system->set_colour_scale(colour_scale_);
    }
    memory::ListNode* node = &system->list_link_;
    node->prev = systems_ptr_->prev;
    node->next = systems_ptr_;
    systems_ptr_->prev->next = node;
    systems_ptr_->prev = node;
    return system;
}

// 0x0049AD60
void FlexibleParticleManager::remove_system(EmitterInstance* system)
{
    if (!system)
        return;
    memory::ListNode* node = &system->list_link_;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// 0x0049AD90
void FlexibleParticleManager::destroy_system_now(uint32_t name)
{
    if (EmitterInstance* system = find_system(name))
        delete system;
}

// 0x0049ADB0
void FlexibleParticleManager::destroy_system_when_empty(uint32_t name)
{
    if (EmitterInstance* system = find_system(name)) {
        system->fading_out_ = true;
        system->flag_bits_ |= 1u;
    }
}

// 0x0049ADE0
void FlexibleParticleManager::destroy_group(uint32_t group)
{
    EmitterInstance* system = first_system(systems_ptr_);
    while (system) {
        EmitterInstance* next = next_system(system);
        if (system->group_id_ == group)
            delete system;
        system = next;
    }
}

// 0x0049AE30
void FlexibleParticleManager::destroy_non_persistent()
{
    EmitterInstance* system = first_system(systems_ptr_);
    while (system) {
        EmitterInstance* next = next_system(system);
        if (!system->unknown12e_)
            delete system;
        system = next;
    }
}

// 0x0049AE80
void FlexibleParticleManager::set_colour_scale(const float* rgba)
{
    if (!systems_ptr_)
        return;
    for (EmitterInstance* system = first_system(systems_ptr_); system; system = next_system(system)) {
        if (system->flag_bits_ & 0x10000u)
            system->set_colour_scale(rgba);
    }
    for (int i = 0; i < 4; ++i)
        colour_scale_[i] = rgba[i];
}

} // namespace render
