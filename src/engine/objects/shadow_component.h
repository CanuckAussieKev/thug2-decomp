#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>

#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

namespace render {
class ShadowBase;
}

namespace objects {

// The shadow component (type 0x8A897DD2, "shadow"; vtable 0x00646498, 0x48 bytes; made by 0x00431800): the shadow
// under its object - one of the renderer's shadow objects (engine_shadows.h), chosen by ShadowType: detailed (type 1,
// 0x004A19C0: from the model, cast along (0, -1, 0) over 72), simple (type 2, 0x004A1750: a shadow model -
// ShadowModel=, "Ped_Shadow" by default - at the ground, scaled by ShadowScale=) or Volume (type 3, 0x004A1F80), or
// none. Each update it follows the object (unless the structure had the flag 0x7E773A11) and is shown while the
// model is visible.
//
// The component's vtable: 0 the scalar deleting destructor; 1 Update; 2 configure_from_data; 3 Finalize; 4 the empty
// 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 Hide; 9 0x004A1090 (Teleport, shared);
// 10 the empty 0x005BAB90; 11 the base's member function table entry 0x0045D720; 12 invoke_method_by_name.
class GroundShadowModule final : public ObjectModule {
public:
    static constexpr uint32_t kNoShadow = 0x806FFF30; // ShadowType none

    GroundShadowModule(const GroundShadowModule&) = delete;
    GroundShadowModule& operator=(const GroundShadowModule&) = delete;
    static constexpr uint32_t kType = 0x8A897DD2;

    // 0x00431800 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x00431380: its type; no shadow, at the origin (w 1) with the ground normal
    // (0, 1, 0, 1), type none, following its object.
    GroundShadowModule();
    // 0x00431300: the shadow deleted; the base's body.
    ~GroundShadowModule() override;
    void jump_to() override { tick_frame(); } // (slot 9: a jump to slot 1)
    // 0x00431860 (vtable slot 2): ShadowType (simple by default) and the 0x7E773A11 flag (don't follow the object);
    // the old shadow removed and the new one made (see the class). For a simple shadow, its scale (ShadowScale=, 1)
    // and a first Update.
    void configure_from_data(script::Struct* params) override;
    // 0x00431400 (vtable slot 3): the model component found and kept; the shadow (if any) given the model and its
    // asset data (the asset manager's, by the model's checksum +0x30).
    void seal() override;
    // 0x004319D0 (vtable slot 1): following the object: the shadow's position = the object's, and the shadow given
    // the display matrix at that position - or, when the model has a hierarchy (0x00493040), the root's matrix times
    // the display matrix at the object's position plus the root's translation; then refresh_shadow.
    void tick_frame() override;
    // 0x00431570 (vtable slot 8): the shadow hidden or shown.
    void conceal(bool hide) override;
    // 0x00431B10 (vtable slot 12): Obj_ShadowOff (the shadow removed) and Obj_ShadowOn (configure_from_data with
    // ShadowType simple when not given; a simple shadow's offset= (0.2) set). 1 for those, 2 for other names.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x00431450: with a shadow, a model component and a model: while the model is visible (its flags +0x44 have
    // 0x2 and not 0x4) the shadow updated for its type (detailed: placed 36 above the object, along its up; simple:
    // its scale = the model's +0x1C, then as Volume: Update with the position, the object's matrix and the normal)
    // and shown; otherwise hidden.
    void refresh_shadow();
    // 0x004315A0: a shadow of `type` (1-3; see the class) made unless there is one; types 2 and 3 updated at once.
    void build_blob_shadow(int type, const char* model);
    // 0x00431780: the shadow (if any) deleted - the model told it has none first (0x004949B0(false)).
    void destroy_shadow();
    // 0x004317C0 / 0x004317E0: the shadow's position / the ground normal under it.
    void change_place(const maths::Vec4* position);
    void set_normal(const maths::Vec4* normal);
    // 0x00431360 (not a Ghidra function): the light's direction handed to the shadow (if any).
    void set_light_direction(const maths::Vec4* direction);

    // The fields other code reads and sets.
    void set_follow_object(bool value) { follow_object_ = value; }

private:
    std::unique_ptr<render::ShadowBase> shadow_handle_; // +0x18 the renderer's shadow object
    maths::Vec4 at_point_{};                            // +0x1C where it is
    maths::Vec4 surface_dir_{};                         // +0x2C the ground's normal under it
    uint32_t shadow_kind_{kNoShadow};                   // +0x3C ShadowType (none 0x806FFF30, simple, detailed, Volume)
    ObjectModule* mesh_instance_{};                     // +0x40 the model component (Finalize)
    bool follow_object_{true};                          // +0x44 each update puts it at the object's position
    std::array<uint8_t, 3> pad45{};
};

} // namespace objects
