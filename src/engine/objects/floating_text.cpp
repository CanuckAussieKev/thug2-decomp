// The floating text component (GameObjects::FloatingTextComponent): see floating_text.h. From the disassembly of
// 0x0041E270-0x0041E6DE.

#include "engine/objects/floating_text.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <gsl/pointers>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/model.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"

namespace frontend {

// 0x0041E2A0
UiTree* fetch_ui_tree() { return UiTree::sole(); }

} // namespace frontend

namespace objects {

namespace {

constexpr uint32_t kId = 0x40C698AF;        // id
constexpr uint32_t kString = 0x61414D56;    // string
constexpr uint32_t kColor = 0x99A9B716;     // color
constexpr uint32_t kYOffset = 0x14975800;   // y_offset
constexpr uint32_t kElementId = 0x727F9552; // (the element's id)
constexpr uint32_t kDuration = 0x79A07F3F;  // duration
constexpr uint32_t kText = 0xC4745838;      // text
constexpr uint32_t kPos3d = 0x4B491900;     // pos3D
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kCreateScript = 0x6A060CF0;  // makes the element
constexpr uint32_t kDestroyScript = 0x2575B406; // destroys it
constexpr uint32_t kReinit = 0xE357E3CB;
constexpr uint32_t kIdBase = 0x1525E9DF;       // (subtracted from the object's id)
constexpr float kMinusMilliseconds = -1000.0f; // 0x0064604C

// strncpy: at most `size` characters, the rest of `out` zeroed (not terminated when `in` is that long or longer).
void copy_text(char* out, const char* in, size_t size)
{
    size_t i = 0;
    for (; i < size && in[i]; ++i)
        out[i] = in[i];
    for (; i < size; ++i)
        out[i] = 0;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

script::Struct* new_params()
{
    script::Struct* params = script::spawn_param_bag();
    return params ? memory::construct_in<script::Struct>(params) : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear(); // (the destructor)
        script::delete_struct(params);
    }
}

} // namespace

// 0x0041E3C0
gsl::owner<ObjectModule*> FloatingTextComponent::fabricate() { return new FloatingTextComponent; }

// (the constructor: inlined in fabricate in the original)
FloatingTextComponent::FloatingTextComponent()
{
    text_[0] = 0;
    kind_tag_ = kType;
}

// 0x0041E5E0 (0x0041E6C0 the deleting one)
FloatingTextComponent::~FloatingTextComponent()
{
    script::Struct* params = new_params();
    params->put_name_key(kId, ident_);
    script::launch_script(kDestroyScript, params, nullptr, false, nullptr);
    delete_params(params);
}

// 0x0041E2B0
void FloatingTextComponent::configure_from_data(script::Struct* params)
{
    if (!ident_)
        ident_ = entity_->ident_ - kIdBase;
    const char* text = nullptr;
    if (params->fetch_text(kString, &text, 0)) {
        if (!text[0]) {
            end_time_ = static_cast<uint32_t>(frame_clock::game_clock_elapsed() + g_clock_offset);
        } else {
            copy_text(text_.data(), text, sizeof(text_)); // (the original: the game's strncpy, 0x006239E0)
            end_time_ = 0;
        }
    }
    params->fetch_int_value(kColor, &tint_, 0);
    params->fetch_float(kYOffset, &y_offset_, 0);
    params->read_hash_value(kElementId, &ident_, 0);
    if (params->has_entry_named(kDuration)) {
        float duration = 0.0f;
        params->fetch_float(kDuration, &duration, 0);
        const int elapsed = frame_clock::game_clock_elapsed();
        const int milliseconds = get_truncated(static_cast<double>(duration) * kMinusMilliseconds);
        end_time_ = static_cast<uint32_t>(elapsed + (g_clock_offset - milliseconds));
    }
}

// 0x0041E3A0
void FloatingTextComponent::seal() { mesh_instance_ = entity_->fetch_module(kModel); }

// 0x0041E450
void FloatingTextComponent::tick_frame()
{
    auto* model =
        static_cast<render::RenderModel*>(core::down_cast<objects::MeshInstanceModule>(mesh_instance_)->mesh_instance_);
    if (!model || !(model->flag_bits_ & 2) || (model->flag_bits_ & 4))
        return;
    if (!text_[0])
        return;
    if (end_time_ && end_time_ < ints::as_unsigned(frame_clock::game_clock_elapsed() + g_clock_offset)) {
        text_[0] = 0;
        end_time_ = 0;
    }
    script::Struct* params = new_params();
    params->put_name_key(kId, ident_);
    // (the original: the game's sprintf, 0x006234FE, into 64 bytes - a text of more than about 60 characters overran
    // them into the stack cookie, ending the game; here it's cut short)
    std::array<char, 0x40> text{};
    std::snprintf(text.data(), sizeof(text), "\\c%d%s", tint_, text_.data());
    params->append_word(kText, text.data());
    const maths::Vec4& pos = entity_->at_point_;
    params->put_vec3(kPos3d, pos.x, pos.y + y_offset_, pos.z);
    frontend::ElementPtr found;
    frontend::UiTree::sole()->fetch_part(&found, ident_, 0);
    frontend::UiWidget* element = found.get_pull();
    found.get_release();
    if (element)
        element->change_traits(params);
    else
        script::launch_script(kCreateScript, params, nullptr, false, nullptr);
    delete_params(params);
}

// 0x0041E270
int FloatingTextComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    if (name != kReinit)
        return 2;
    reapply_from_data(params);
    return 1;
}

} // namespace objects
