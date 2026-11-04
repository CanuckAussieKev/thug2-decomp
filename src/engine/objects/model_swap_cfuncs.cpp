// Script commands (CFunctions) 0x005B34A0-0x005B3A90: swapping objects' models (see model_swap_cfuncs.h).

#include "engine/objects/model_swap_cfuncs.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/shadow_component.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_cfuncs.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace commands {

namespace {

using objects::GameEntity;
using script::Struct;

constexpr uint32_t kObj1 = 0x9A2DD8A1;              // "obj_1"
constexpr uint32_t kObj2 = 0x0324891B;              // "obj_2"
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29; // "SkaterCorePhysics"
constexpr uint32_t kModel = 0x286A8D26;             // "model"
constexpr uint32_t kShadow = 0x8A897DD2;            // "shadow"
constexpr uint32_t kSkeleton = 0x222756D5;          // "skeleton"
constexpr uint32_t kSuspend = 0xCE0CA665;           // "suspend"
constexpr uint32_t kSkater = 0x5B8AB877;            // "Skater"
const char* const kPedShadow = "Ped_Shadow";        // "Ped_Shadow"

// The swaps made (0x007CEDE0, five entries - profile_cfuncs.cpp owns them): each object's id (key) and the id of the
// object whose model it wears now (value).

constexpr int kSwapCount = 5;

// 0x005AA930 (commands::swap_mapping, profile_cfuncs.cpp): the swap of the objects `a` and `b` recorded in the table.
void record_swap(uint32_t a, uint32_t b) { commands::swap_mapping(a, b); }

// A skeleton (the skeleton component's +0x18): 0x004A3D30 constructs one in `memory` from the asset at another's +8,
// 0x004A2E20 copies one into another, 0x004A2CB0 is its destructor body.
gfx::Rig* skeleton_of(objects::ObjectModule* component)
{
    return core::down_cast<objects::BoneRigModule>(component)->bone_rig();
}
void copy_skeleton(gfx::Rig* to, gfx::Rig* from)
{
    Expects(to != nullptr);
    to->copy_bone_scales(from, true);
}

// A model's +0xC (0x00492E50 / 0x00493460: the setter tells the model's parts) and its lights (+0x2C), whose
// position follows a pointer (0x0048E4D0).
maths::Mat4* model_get(render::RenderModel* model) { return model->fetch_skinning_palette(); }
void model_set(render::RenderModel* model, maths::Mat4* palette) { model->upload_skinning_palette(palette); }
void follow_position(render::RenderModel* model, maths::Vec4* position)
{
    model->lights_ptr()->bind_position_source(position);
}

// The model component's change_mesh (the original's slot 14).
void set_model_virtual(objects::MeshInstanceModule* component, render::RenderModel* model)
{
    component->change_mesh(model);
}

// An object's shadow made again: a skater's in its skater heap (type 1), anything else's type 2.
void remake_shadow(GameEntity* object, objects::GroundShadowModule* shadow, objects::Rider* skater)
{
    if (object->fetch_module(kSkaterCorePhysics)) {
        memory::Hub* manager = memory::Hub::get_pull();
        Expects(skater != nullptr);
        manager->enter_heap_scope(core::at(manager->skater_heaps(), skater->get_skater_number()));
        shadow->build_blob_shadow(1, kPedShadow);
        memory::Hub::get_pull()->leave_heap_scope();
    } else {
        shadow->build_blob_shadow(2, kPedShadow);
    }
}

} // namespace

// 0x005B34A0: SwapObjectModels obj_1=<id> obj_2=<id>: the two objects (a skater among them, if either has skater
// core physics - the second if both) swap their renderable models, skeletons (through a copy made in the top-down
// heap) and shadows; the swap is recorded; a skater's model-swapped value (+0x31C) is the id of an entry whose object
// wears no other model now (0 if none). The objects are asked for core physics before they're checked for null, as in
// the original. False if either is missing, they're the same object or either has no model.
bool cfunc_swap_object_models(script::Struct* params, script::Script* /*script*/)
{
    objects::Rider* skater = nullptr;
    uint32_t id1 = 0;
    params->read_hash_value(kObj1, &id1, 1);
    uint32_t id2 = 0;
    params->read_hash_value(kObj2, &id2, 1);
    auto* a = core::down_cast<GameEntity>(objects::EntityHub::look_up_entity(id1));
    auto* b = core::down_cast<GameEntity>(objects::EntityHub::look_up_entity(id2));
    if (a->fetch_module(kSkaterCorePhysics)) {
        skater = core::down_cast<objects::Rider>(a);
    }
    if (b->fetch_module(kSkaterCorePhysics)) {
        skater = core::down_cast<objects::Rider>(b);
    }
    if (!a || !b || a == b) {
        return false;
    }
    record_swap(a->ident(), b->ident());

    auto* model_a = core::down_cast<objects::MeshInstanceModule>(a->fetch_module(kModel));
    auto* model_b = core::down_cast<objects::MeshInstanceModule>(b->fetch_module(kModel));
    auto* shadow_a = core::down_cast<objects::GroundShadowModule>(a->fetch_module(kShadow));
    auto* shadow_b = core::down_cast<objects::GroundShadowModule>(b->fetch_module(kShadow));
    if (!model_a || !model_b) {
        return false;
    }
    shadow_a->destroy_shadow();
    shadow_b->destroy_shadow();

    // the skeletons swapped through a copy
    gfx::Rig* skeleton_a = skeleton_of(a->fetch_module(kSkeleton));
    gfx::Rig* skeleton_b = skeleton_of(b->fetch_module(kSkeleton));
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    void* memory = memory::alloc(sizeof(gfx::Rig));
    gfx::Rig* temp = memory ? memory::construct_in<gfx::Rig>(memory, skeleton_a->payload()) : nullptr;
    memory::Hub::get_pull()->leave_heap_scope();
    copy_skeleton(temp, skeleton_a);
    copy_skeleton(skeleton_a, skeleton_b);
    copy_skeleton(skeleton_b, temp);
    if (temp) {
        temp->~Rig();
        memory::Hub::dealloc(temp);
    }

    // the renderable models swapped
    render::RenderModel* nx_a = model_a->mesh_instance().get();
    render::RenderModel* nx_b = model_b->mesh_instance().get();
    maths::Mat4* value_a = model_get(nx_a);
    model_set(nx_a, model_get(nx_b));
    model_set(nx_b, value_a);
    set_model_virtual(model_a, nx_b);
    set_model_virtual(model_b, nx_a);

    auto* suspend_a = core::down_cast<objects::DormancyModule>(a->fetch_module(kSuspend));
    auto* suspend_b = core::down_cast<objects::DormancyModule>(b->fetch_module(kSuspend));
    suspend_a->reset_counts();
    suspend_b->reset_counts();
    a->hold_off(false);
    b->hold_off(false);

    if (skater) {
        // (each component's model as it is now)
        follow_position(model_a->mesh_instance().get(), &a->at_point());
        follow_position(model_b->mesh_instance().get(), &b->at_point());
        bool found = false;
        for (int i = 0; i < kSwapCount; ++i) {
            if (core::at(commands::swap_table(), i).used && core::at(commands::swap_table(), i).value == 0) {
                skater->set_unknown31c(core::at(commands::swap_table(), i).key);
                found = true;
            }
        }
        if (!found) {
            skater->set_unknown31c(0);
        }
    }
    remake_shadow(a, shadow_a, skater);
    remake_shadow(b, shadow_b, skater);
    return true;
}

// 0x005B3890: RestoreAllSwappedObjectModels: each recorded swap undone (SwapObjectModels on the entry's object and the
// one whose model it wears), the table gone over again while it still holds swaps - at most as many times as there
// were. Always true.
bool cfunc_restore_all_swapped_object_models(script::Struct* /*params*/, script::Script* script)
{
    int remaining = 0;
    for (int i = 0; i < kSwapCount; ++i) {
        if (core::at(commands::swap_table(), i).used) {
            ++remaining;
        }
    }
    if (remaining == 0) {
        return true;
    }
    for (;;) {
        bool any = false;
        for (int i = 0; i < kSwapCount; ++i) {
            const commands::SwapSlot& entry = core::at(commands::swap_table(), i);
            if (!entry.used) {
                continue;
            }
            any = true;
            gsl::owner<Struct*> swap = script::new_struct();
            swap->put_name_key(checksums::Name{kObj1}, entry.key);
            swap->put_name_key(checksums::Name{kObj2}, entry.value);
            cfunc_swap_object_models(swap, script);
            if (swap) {
                // (delete inlined: the structure pool pair's Free)
                swap->get_clear();
                script::struct_pools().dealloc(swap);
            }
        }
        if (!any || --remaining == 0) {
            break;
        }
    }
    return true;
}

// 0x005B3A30: GetSkaterModelIsSwapped [Skater=n]: whether skater n (or the local skater) wears another model.
bool cfunc_get_skater_model_is_swapped(script::Struct* params, script::Script* /*script*/)
{
    int index = 0;
    const objects::Rider* skater = nullptr;
    if (params->fetch_int_value(kSkater, &index, 0)) {
        skater = core::down_cast<objects::Rider>(modules::Skate::sole()->get_rider_at(index));
    } else {
        skater = modules::Skate::sole()->get_home_rider();
    }
    return skater->unknown31c() != 0;
}

} // namespace commands
