// Helpers of the models and animations code (see model_utils.h). From the disassembly of 0x00487770-0x00487D54.

#include "engine/gfx/model_utils.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <array>
#include <span>

#include "engine/core/checked_index.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/text_store.h"
#include "engine/gfx/model.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/core/down_cast.h"
#include <gsl/pointers>
#include "platform/crt.h"
#include "engine/core/no_destroy.h"
#include "engine/core/checksums.h"

namespace gfx {

constexpr float kHalf = 0.5F;
constexpr std::size_t kModelPathSize = 512;

namespace {

using script::Struct;

constexpr uint32_t kCycle = 0x4F792E6C;     // cycle
constexpr uint32_t kPingPong = 0x3153E314;  // pingpong
constexpr uint32_t kWobble = 0x6D941203;    // wobble
constexpr uint32_t kFrom = 0x46E55E8F;      // from
constexpr uint32_t kTo = 0x28782D3B;        // to
constexpr uint32_t kStart = 0x6086AA70;     // start
constexpr uint32_t kMiddle = 0x617FE530;    // middle
constexpr uint32_t kCurrent = 0x230CCBF4;   // current
constexpr uint32_t kEnd = 0xFF03CC4E;       // end
constexpr uint32_t kSeconds = 0xD029F619;   // seconds
constexpr uint32_t kBackwards = 0xF8CFD515; // backwards
constexpr uint32_t kScale = 0x13B9DA7B;     // scale
constexpr uint32_t kX = 0x7323E97C;         // x
constexpr uint32_t kY = 0x0424D9EA;         // y
constexpr uint32_t kZ = 0x9D2D8850;         // z
constexpr float kSixtieth = 0x1.111112p-6F; // 0x006463B8

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A time now and the length it runs to.
struct TimeRange {
    float current{};
    float duration{};
};

// A time given by name (others leave `time` as it is).
void named_time(Struct* params, checksums::Name key, TimeRange time_range, float* time)
{
    const auto current = time_range.current;
    const auto duration = time_range.duration;
    const uint32_t name = key.value();
    uint32_t which = 0;
    if (!params->read_hash_value(name, &which, 0)) {
        return;
    }
    if (which == kMiddle) {
        *time = duration * kHalf;
    } else if (which == kCurrent) {
        *time = current;
    } else if (which == kStart) {
        *time = 0.0F;
    } else if (which == kEnd) {
        *time = duration;
    }
}

// A time given as a number: seconds, or frames (whole ones, at 60 a second).
void number_time(Struct* params, uint32_t name, float* time)
{
    float value = 0.0F;
    if (!params->fetch_float(name, &value, 0)) {
        return;
    }
    if (params->has_bare_flag(kSeconds)) {
        *time = value;
    } else {
        *time = static_cast<float>(ftol(value) * static_cast<double>(kSixtieth));
    }
}

// A model component's object's id and its model.
uint32_t current_object_id(objects::MeshInstanceModule* component)
{
    return static_cast<objects::Entity*>(component->entity())->ident();
}
bool hideable(objects::MeshInstanceModule* component)
{
    return (current_object_id(component) != 0U) &&
           ((component->mesh_instance()->flag_bits() & render::kModelHidden) == 0);
}

} // namespace

// 0x00487770
bool get_anim_loop_type(int* loop_type, Struct* params)
{
    *loop_type = 0;
    if (params->has_bare_flag(kCycle)) {
        *loop_type = 1;
        return true;
    }
    if (params->has_bare_flag(kPingPong)) {
        *loop_type = 2;
        return true;
    }
    if (params->has_bare_flag(kWobble)) {
        *loop_type = 3;
    }
    return true;
}

// 0x004877E0
bool fetch_anim_durations(float* start, float* end, float current, float duration, Struct* params)
{
    float from = 0.0F;
    float to = duration;
    named_time(params, checksums::Name{kFrom}, {.current = current, .duration = duration}, &from);
    named_time(params, checksums::Name{kTo}, {.current = current, .duration = duration}, &to);
    number_time(params, kFrom, &from);
    number_time(params, kTo, &to);
    if (params->has_bare_flag(kBackwards)) {
        *start = to;
        *end = from;
    } else {
        *start = from;
        *end = to;
    }
    return true;
}

// 0x004879D0
text::Chars make_model_path(const char* name, const char* extension)
{
    std::array<char, kModelPathSize> path{};
    make_model_file_name(name, extension, path.data());
    return {path.data()};
}

// 0x00487A70
void make_model_file_name(const char* name, const char* extension, char* out)
{
    if (std::strstr(name, "/") || std::strstr(name, "\\")) {
        crt::get_sprintf(out, "models\\%s", name);
    } else {
        crt::get_sprintf(out, "models\\%s\\%s%s", name, name, extension);
    }
}

// 0x00487AD0
float least_of(float a, float b, float third)
{
    const float least = b < third ? b : third;
    return a < least ? a : least;
}

// 0x00487B00
bool scale_from_args(maths::Vec4* scale, Struct* params)
{
    if (params->has_entry_named(kScale)) {
        float uniform = 0.0F;
        if (params->fetch_float(kScale, &uniform, 0)) {
            scale->x = uniform;
            scale->y = uniform;
            scale->z = uniform;
            scale->w = 1.0F;
            return true;
        }
        if (params->fetch_vec(kScale, scale, 0)) {
            return true;
        }
    }
    const bool x = params->fetch_float(kX, &scale->x, 0);
    const bool y = params->fetch_float(kY, &scale->y, 0);
    const bool z = params->fetch_float(kZ, &scale->z, 0);
    return x || y || z;
}

// 0x00487BD0
void conceal(HiddenModels& models)
{
    auto* manager = objects::object_hub_slot().get();
    auto first = [manager] {
        return core::down_cast<objects::MeshInstanceModule>(
            manager->find_first_module_of_kind(objects::MeshInstanceModule::kType));
    };
    int count = 0;
    for (auto* component = first(); component;
         component = core::down_cast<objects::MeshInstanceModule>(component->next_of_type())) {
        if (hideable(component)) {
            ++count;
        }
    }
    if (count == 0) {
        return;
    }
    if (!memory::manager_ready_flag()) {
        memory::ensure_manager();
    }
    models.ids_ptr_ = static_cast<uint32_t*>(memory::Hub::get_pull()->carve(count * 4, 1, nullptr));
    models.num_ = 0;
    const std::span<uint32_t> ids(models.ids_ptr_, static_cast<size_t>(count));
    for (auto* component = first(); component;
         component = core::down_cast<objects::MeshInstanceModule>(component->next_of_type())) {
        if (hideable(component)) {
            core::at(ids, models.num_) = current_object_id(component);
            component->mesh_instance()->conceal(1);
            ++models.num_;
        }
    }
    debug::printf("Hid %d objects\n", models.num_);
}

// 0x00487CB0
void get_show(HiddenModels& models)
{
    if (models.ids_ptr_) {
        const std::span<const uint32_t> ids(models.ids_ptr_, static_cast<size_t>(models.num_));
        for (int i = 0; i < models.num_; ++i) {
            if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(core::at(ids, i)))) {
                auto* component =
                    core::down_cast<objects::MeshInstanceModule>(object->fetch_module(objects::MeshInstanceModule::kType));
                component->mesh_instance()->conceal(0);
            }
        }
        memory::Hub::dealloc(models.ids_ptr_);
        models.ids_ptr_ = nullptr;
    }
    models.num_ = 0;
}

namespace {
// The models hidden for a cutscene (the original's one instance).
HiddenModels& hidden_models()
{
    static core::NoDestroy<HiddenModels> hidden_models;
    return hidden_models.get();
}
} // namespace

// 0x00487D20
void hide_models_for_cutscene() { conceal(hidden_models()); }

// 0x00487D30
void show_models_after_cutscene() { get_show(hidden_models()); }

} // namespace gfx
