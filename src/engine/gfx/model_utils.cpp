// Helpers of the models and animations code (see model_utils.h). From the disassembly of 0x00487770-0x00487D54.

#include "engine/gfx/model_utils.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/text_store.h"
#include "engine/gfx/model.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"

namespace gfx {

namespace {

using script::Struct;

constexpr uint32_t kCycle = 0x4F792E6C;    // cycle
constexpr uint32_t kPingPong = 0x3153E314; // pingpong
constexpr uint32_t kWobble = 0x6D941203;   // wobble
constexpr uint32_t kFrom = 0x46E55E8F;     // from
constexpr uint32_t kTo = 0x28782D3B;       // to
constexpr uint32_t kStart = 0x6086AA70;    // start
constexpr uint32_t kMiddle = 0x617FE530;   // middle
constexpr uint32_t kCurrent = 0x230CCBF4;  // current
constexpr uint32_t kEnd = 0xFF03CC4E;      // end
constexpr uint32_t kSeconds = 0xD029F619;  // seconds
constexpr uint32_t kBackwards = 0xF8CFD515; // backwards
constexpr uint32_t kScale = 0x13B9DA7B;    // scale
constexpr uint32_t kX = 0x7323E97C;        // x
constexpr uint32_t kY = 0x0424D9EA;        // y
constexpr uint32_t kZ = 0x9D2D8850;        // z
constexpr float kSixtieth = 0x1.111112p-6f; // 0x006463B8

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A time given by name (others leave `time` as it is).
void named_time(Struct* params, uint32_t name, float current, float duration, float* time)
{
    uint32_t which = 0;
    if (!params->read_hash_value(name, &which, 0))
        return;
    if (which == kMiddle)
        *time = duration * 0.5f;
    else if (which == kCurrent)
        *time = current;
    else if (which == kStart)
        *time = 0.0f;
    else if (which == kEnd)
        *time = duration;
}

// A time given as a number: seconds, or frames (whole ones, at 60 a second).
void number_time(Struct* params, uint32_t name, float* time)
{
    float value = 0.0f;
    if (!params->fetch_float(name, &value, 0))
        return;
    if (params->has_bare_flag(kSeconds))
        *time = value;
    else
        *time = static_cast<float>(ftol(value) * static_cast<double>(kSixtieth));
}

// A model component's object's id and its model.
uint32_t current_object_id(objects::MeshInstanceModule* component)
{
    return reinterpret_cast<objects::Entity*>(component->entity_)->ident_;
}
bool hideable(objects::MeshInstanceModule* component)
{
    return current_object_id(component) && !(component->mesh_instance_->flag_bits_ & render::kModelHidden);
}

} // namespace

// 0x00487770
bool __cdecl get_anim_loop_type(int* loop_type, Struct* params)
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
    if (params->has_bare_flag(kWobble))
        *loop_type = 3;
    return true;
}

// 0x004877E0
bool __cdecl fetch_anim_durations(float* start, float* end, float current, float duration, Struct* params)
{
    float from = 0.0f;
    float to = duration;
    named_time(params, kFrom, current, duration, &from);
    named_time(params, kTo, current, duration, &to);
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
text::Chars* __cdecl make_model_path(text::Chars* out, const char* name, const char* extension)
{
    char path[512];
    make_model_file_name(name, extension, path);
    return new (out) text::Chars(path);
}

// 0x00487A70
void __cdecl make_model_file_name(const char* name, const char* extension, char* out)
{
#pragma warning(push)
#pragma warning(disable : 4996)
    if (std::strstr(name, "/") || std::strstr(name, "\\"))
        std::sprintf(out, "models\\%s", name);
    else
        std::sprintf(out, "models\\%s\\%s%s", name, name, extension);
#pragma warning(pop)
}

// 0x00487AD0
float __cdecl least_of(float a, float b, float c)
{
    float least = b < c ? b : c;
    return a < least ? a : least;
}

// 0x00487B00
bool __cdecl scale_from_args(maths::Vec4* scale, Struct* params)
{
    if (params->has_entry_named(kScale)) {
        float uniform;
        if (params->fetch_float(kScale, &uniform, 0)) {
            scale->x = uniform;
            scale->y = uniform;
            scale->z = uniform;
            scale->w = 1.0f;
            return true;
        }
        if (params->fetch_vec(kScale, reinterpret_cast<script::VectorValue*>(scale), 0))
            return true;
    }
    bool x = params->fetch_float(kX, &scale->x, 0);
    bool y = params->fetch_float(kY, &scale->y, 0);
    bool z = params->fetch_float(kZ, &scale->z, 0);
    return x || y || z;
}

// 0x00487BD0
void HiddenModels::conceal()
{
    auto* manager = objects::object_hub_slot();
    auto first = [manager] {
        return static_cast<objects::MeshInstanceModule*>(manager->find_first_module_of_kind(objects::MeshInstanceModule::kType));
    };
    int count = 0;
    for (auto* component = first(); component; component = static_cast<objects::MeshInstanceModule*>(component->next_of_type_)) {
        if (hideable(component))
            ++count;
    }
    if (!count)
        return;
    if (!memory::g_manager_ready)
        memory::ensure_manager();
    ids_ptr_ = static_cast<uint32_t*>(memory::Hub::get_pull()->carve(count * 4, 1, nullptr));
    num_ = 0;
    for (auto* component = first(); component; component = static_cast<objects::MeshInstanceModule*>(component->next_of_type_)) {
        if (hideable(component)) {
            ids_ptr_[num_] = current_object_id(component);
            component->mesh_instance_->conceal(1);
            ++num_;
        }
    }
    debug::printf("Hid %d objects\n", num_);
}

// 0x00487CB0
void HiddenModels::get_show()
{
    if (ids_ptr_) {
        for (int i = 0; i < num_; ++i) {
            if (auto* object = static_cast<objects::GameEntity*>(objects::find_object(ids_ptr_[i]))) {
                auto* component = static_cast<objects::MeshInstanceModule*>(object->fetch_module(objects::MeshInstanceModule::kType));
                component->mesh_instance_->conceal(0);
            }
        }
        memory::Hub::get_pull()->dealloc(ids_ptr_);
        ids_ptr_ = nullptr;
    }
    num_ = 0;
}

// The models hidden for a cutscene (the original's one instance).
HiddenModels g_hidden_models;

// 0x00487D20
void __cdecl hide_models_for_cutscene() { g_hidden_models.conceal(); }

// 0x00487D30
void __cdecl show_models_after_cutscene() { g_hidden_models.get_show(); }

// 0x00487D50
__declspec(naked) void virtual_slot4_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x10]
    }
}

} // namespace Gfx
