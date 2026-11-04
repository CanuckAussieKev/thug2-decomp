// The engine's flexible particle manager: see flexible_particle_manager.h. From the disassembly of 0x0049A8E0-0x0049B05E
// (0x0049A8E0 isn't a Ghidra function).

#include "engine/gfx/flexible_particle_manager.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <array>
#include <span>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/maths/vec4.h"
#include "engine/script/param_struct.h"
#include "engine/objects/movie_manager.h"
#include "platform/crt.h"
#include "engine/memory/arena_alloc.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/view_setup.h"
#include "engine/core/checksums.h"

namespace render {

constexpr std::size_t kEmitterSystemSlots = 256;
constexpr uint32_t kSystemFlagLive = 0x10000U;

namespace {

const char* const kHeapName = "FlxParticles";     // "FlxParticles"
const char* const kTempHeapName = "TEMPFlxPrtcl"; // "TEMPFlxPrtcl"
constexpr uint32_t kHeapSizeSymbol = 0x91CB7552;
constexpr uint32_t kFrameRateSymbol = 0x3214C818;
constexpr int kDefaultHeapSize = 0x5000;
constexpr float kFramesPerSecond = 60.0F; // 0x00645EA0

// The list's head deleted (0x0049AEF0).
void delete_head(memory::ListNode* head) { memory::ListNode::destroy(head); }

// The list's systems: the first after the head, the one after `system` - null at the head (its data -1) or a null link.
EmitterInstance* system_of(memory::ListNode* node)
{
    if (node->data() == memory::ListNode::head_marker() || !node) {
        return nullptr;
    }
    return static_cast<EmitterInstance*>(node->data()); // (its node's data: the system - the original took its address)
}
EmitterInstance* first_system(memory::ListNode* head) { return head ? system_of(head->next()) : nullptr; }
EmitterInstance* next_system(EmitterInstance* system) { return system_of(system->list_link().next()); }

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0049AF20
FlexibleParticleManager::FlexibleParticleManager()
{
    int size = script::fetch_int_value(kHeapSizeSymbol);
    if (size == 0) {
        size = kDefaultHeapSize;
    }
    zone_ = new memory::MemoryArena(static_cast<uint32_t>(size));
    arena_ = memory::Hub::get_pull()->add_heap(zone_, 1, kHeapName);
    temp_region_ = nullptr;
    temp_heap_ = nullptr;
    systems_ptr_ = memory::ListNode::create_head();
    core::at(colour_scale_, 0) = core::at(colour_scale_, 1) = core::at(colour_scale_, 2) = core::at(colour_scale_, 3) =
        1.0F;
    const float rate = script::fetch_float(kFrameRateSymbol);
    // (0 gives 60; anything else, NaN too, 60 / it)
    view_setup().set_frame_time_scale(rate == 0.0F ? kFramesPerSecond
                                                   : static_cast<float>(kFramesPerSecond / static_cast<double>(rate)));
}

// 0x0049AA10
FlexibleParticleManager::~FlexibleParticleManager()
{
    if (systems_ptr_) {
        delete_head(systems_ptr_);
    }
    delete_temp_heap();
    memory::Hub::get_pull()->delete_heap(static_cast<memory::MemSource*>(arena_));
    delete zone_;
}

// 0x0049A9D0
void FlexibleParticleManager::delete_temp_heap()
{
    if (!temp_heap_) {
        return;
    }
    memory::Hub::get_pull()->delete_heap(static_cast<memory::MemSource*>(temp_heap_));
    delete temp_region_;
    temp_heap_ = nullptr;
    temp_region_ = nullptr;
}

// 0x0049AA80
bool FlexibleParticleManager::create_temp_heap(uint32_t size)
{
    delete_temp_heap();
    temp_region_ = new memory::MemoryArena(size);
    if (!temp_region_) {
        return false;
    }
    temp_heap_ = memory::Hub::get_pull()->add_heap(temp_region_, 1, kTempHeapName);
    return temp_heap_ != nullptr;
}

// 0x0049A8E0
bool FlexibleParticleManager::has_temp_heap() { return temp_heap_ != nullptr; }

// 0x0049A8F0
int FlexibleParticleManager::compare_distance(const void* a, const void* b)
{
    auto* camera = fetch_live_camera(view_setup().drawn_viewport());
    const maths::Vec4& at = *camera->fetch_location();
    const EmitterInstance* first = *static_cast<EmitterInstance* const*>(a);
    const EmitterInstance* second = *static_cast<EmitterInstance* const*>(b);
    // (the original keeps some differences as floats and others at 53 bits)
    auto x1 = static_cast<float>(static_cast<double>(first->at_point().x) - at.x);
    auto y1 = static_cast<float>(static_cast<double>(first->at_point().y) - at.y);
    auto z1 = static_cast<float>(static_cast<double>(first->at_point().z) - at.z);
    auto x2 = static_cast<float>(static_cast<double>(second->at_point().x) - at.x);
    const double y2 = static_cast<double>(second->at_point().y) - at.y;
    const double z2 = static_cast<double>(second->at_point().z) - at.z;
    const double distance2 = ((z2 * z2) + (y2 * y2)) + (static_cast<double>(x2) * x2);
    const double distance1 =
        ((static_cast<double>(z1) * z1) + (static_cast<double>(y1) * y1)) + (static_cast<double>(x1) * x1);
    const int order = truncate(distance2 - distance1);
    if (order != 0) {
        return order;
    }
    return static_cast<int>(second->rank()) - static_cast<int>(first->rank());
}

// 0x0049AB50
void FlexibleParticleManager::render_systems(bool force)
{
    std::array<EmitterInstance*, kEmitterSystemSlots> systems = {};
    int count = 0;
    for (EmitterInstance* system = first_system(systems_ptr_); system; system = next_system(system)) {
        if (system->wants_time()) {
            core::at(systems, count++) = system; // (no bound, as the original)
        }
    }
    if (count == 0) {
        return;
    }
    crt::get_qsort(static_cast<void*>(systems.data()), static_cast<size_t>(count), sizeof(EmitterInstance*),
                   &compare_distance); // (the game's qsort)
    const bool always = force || objects::simulation_halted();
    for (int i = 0; i < count; ++i) {
        core::at(systems, i)->render_if_visible(always);
    }
}

// 0x0049AC20
EmitterInstance* FlexibleParticleManager::find_system(uint32_t name)
{
    for (EmitterInstance* system = first_system(systems_ptr_); system; system = next_system(system)) {
        if (system->label() == name) {
            return system;
        }
    }
    return nullptr;
}

// 0x0049AC60
EmitterInstance* FlexibleParticleManager::start_subsystem(uint32_t name, uint32_t type, int particle_cap,
                                                          int max_streams, uint32_t texture, uint32_t blend_mode,
                                                          int fixed_alpha, int segments, float unknown, int history,
                                                          bool perm, bool use_level_colour, const char* model)
{
    memory::Hub::get_pull()->enter_heap_scope(static_cast<memory::MemSource*>(temp_heap_ ? temp_heap_ : arena_));
    gsl::owner<EmitterInstance*> made = create_flexible_particles(checksums::Name{name}, checksums::Name{type}, particle_cap, max_streams, texture,
                                                                  blend_mode, fixed_alpha, segments, unknown, history, model);
    memory::Hub::get_pull()->leave_heap_scope();
    if (!made) {
        return nullptr;
    }
    if ((made->flag_bits() & 4U) == 0U) { // (not everything allocated)
        delete made;
        return nullptr;
    }
    EmitterInstance* const system = made; // (the manager's ring's, below)
    system->set_unknown12e(static_cast<uint8_t>(perm));
    system->flag_bits() |= 1U;
    system->set_wants_time(1);
    system->flag_bits() |= kSystemFlagLive;
    system->set_spewing(false);
    if (use_level_colour) {
        system->flag_bits() |= kSystemFlagLive;
        system->set_colour_scale(colour_scale_);
    }
    memory::ListNode* node = &system->list_link();
    node->set_prev(systems_ptr_->prev());
    node->set_next(systems_ptr_);
    systems_ptr_->prev()->set_next(node);
    systems_ptr_->set_prev(node);
    return system;
}

// 0x0049AD60
void FlexibleParticleManager::remove_system(EmitterInstance* system)
{
    if (!system) {
        return;
    }
    memory::ListNode* node = &system->list_link();
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

// 0x0049AD90
void FlexibleParticleManager::destroy_system_now(uint32_t name)
{
    if (const EmitterInstance* system = find_system(name)) {
        gsl::owner<const EmitterInstance*> owned = memory::owned_element(system); // (the manager's ring's)
        delete owned;
    }
}

// 0x0049ADB0
void FlexibleParticleManager::destroy_system_when_empty(uint32_t name)
{
    if (EmitterInstance* system = find_system(name)) {
        system->set_fading_out(true);
        system->flag_bits() |= 1U;
    }
}

// 0x0049ADE0
void FlexibleParticleManager::destroy_group(uint32_t group)
{
    EmitterInstance* system = first_system(systems_ptr_);
    while (system) {
        EmitterInstance* next = next_system(system);
        if (system->group_id() == group) {
            gsl::owner<EmitterInstance*> owned = memory::owned_element(system); // (the manager's ring's)
            delete owned;
        }
        system = next;
    }
}

// 0x0049AE30
void FlexibleParticleManager::destroy_non_persistent()
{
    EmitterInstance* system = first_system(systems_ptr_);
    while (system) {
        EmitterInstance* next = next_system(system);
        if (system->unknown12e() == 0U) {
            gsl::owner<EmitterInstance*> owned = memory::owned_element(system); // (the manager's ring's)
            delete owned;
        }
        system = next;
    }
}

// 0x0049AE80
void FlexibleParticleManager::set_colour_scale(std::span<const float, 4> rgba)
{
    if (!systems_ptr_) {
        return;
    }
    for (EmitterInstance* system = first_system(systems_ptr_); system; system = next_system(system)) {
        if ((system->flag_bits() & kSystemFlagLive) != 0U) {
            system->set_colour_scale(rgba);
        }
    }
    for (int i = 0; i < 4; ++i) {
        core::at(colour_scale_, i) = core::at(rgba, i);
    }
}

} // namespace render
