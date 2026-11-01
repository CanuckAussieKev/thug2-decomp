#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace script {
class Script;
class Struct;
} // namespace script

// Graffiti tags and stickers: the tagging component holds a skater's two textures and puts a tag or sticker on the
// world (a decal, told to the server in a network game); the tagging control component decides when - a walking
// skater tagging a wall it faces (the tag button), or slapping a sticker on a wall from vert.
namespace objects {
class FootMovement;

// The tagging component (Tagging, type 0x5B512EDC, vtable 0x0064C330, 0x24 bytes; made by 0x0051DA80).
// Vtable: 0 Destroy, 1 Update 0x0051B970 (shared: suspends itself), 2 configure_from_data, 12 invoke_method_by_name.
class TaggingComponent final : public ObjectModule {
public:
    TaggingComponent(const TaggingComponent&) = delete;
    TaggingComponent& operator=(const TaggingComponent&) = delete;
    static constexpr uint32_t kType = 0x5B512EDC;

    // 0x0051DA80 (registered with the composite object manager): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    TaggingComponent();
    // 0x0051D760: the destructor body: both textures given back, then the base's.
    ~TaggingComponent() override;
    // 0x0051D890 (vtable slot 2): its own copies of tags\cas_01 and tags\sticker, keyed by the skater number
    // (0xA2B033FD of `params`; without it, what its slot held: `params` itself).
    void configure_from_data(script::Struct* params) override;
    // 0x0051DB00 (vtable slot 12): SetGraffitiTexture / SetStickerTexture (the unnamed string), HasCustomTagTexture,
    // ApplyCustomTagTexture. Other names: 2 (not mine).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x0051D830: the custom tag texture (0xAA286543) put on the graffiti texture.
    void apply_custom_tag_texture();
    // 0x0051D850 (4 bytes: source only): whether the skater has a custom tag texture.
    bool has_custom_tag_texture();
    // 0x0051D9C0 / 0x0051DA20: a texture loaded from `name` and put on the graffiti / sticker texture.
    void set_graffiti_texture(const char* name);
    void set_sticker_texture(const char* name);
    // 0x0051DBC0: a tag (or with `sticker` a sticker, turned by up to 45 degrees at random) placed from `start`
    // towards `end`, `size` big; with `send`, told to the server in a network game (message 0x99).
    void place_tag(bool sticker, maths::Vec4 start, maths::Vec4 end, int size, bool send);

    bool custom_tag_{false}; // +0x18
    uint8_t pad19[3]{};
    void* graffiti_{nullptr}; // +0x1C the graffiti texture
    void* sticker_{nullptr};  // +0x20 the sticker texture
};

// The tagging control component (TaggingControl, type 0x380D909A, vtable 0x0064C394, 0x38 bytes; made by 0x0051E280).
// Vtable: 0 Destroy, 1 Update (0x0051EA30, a jump to 0x0051E390), 3 Finalize, 12 invoke_method_by_name.
class TaggingControlComponent final : public ObjectModule {
public:
    TaggingControlComponent(const TaggingControlComponent&) = delete;
    TaggingControlComponent& operator=(const TaggingControlComponent&) = delete;
    static constexpr uint32_t kType = 0x380D909A;

    // 0x0051E280 (registered with the composite object manager): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    TaggingControlComponent();
    // 0x0051DD50: the destructor body (the base's).
    ~TaggingControlComponent() override;
    // 0x0051DD90 (vtable slot 3): the skater's walk, input, core physics, trigger and tagging components.
    void seal() override;
    // 0x0051E300 (vtable slot 12): TagNow, VertStickerSlap, TagClearFlags, BlockTaggingOn / Off. Other names: 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x0051EA30 (vtable slot 1): a jump to try_tagging.
    void tick_frame() override { try_tagging(); }
    // 0x0051E390: a walking skater (not blocked, out of its cooldown) tagging:
    // the tag button tapped - feelers from the tagging bone in six directions ahead find the nearest wall; the first
    // tap starts the tag (script 0x8C92DEA3 / 0x5213AFD6, 30 frames), the next one places it (bigger with a bigger
    // score) and starts the cooldown. Returns whether it got past the checks.
    bool try_tagging();
    // 0x0051DDF0: a sticker slapped (or with `sticker` false, a tag sprayed) on the wall the skater faces from the
    // air: a feeler along its facing (`walking`: the walk component's heading; `low`: the lower spray height) finds
    // the wall; a sticker is only marked the first time and placed the next. False before the cooldown or the
    // countdown are over, or without a wall.
    bool slap_sticker(bool sticker, bool walking, bool low);

    int countdown_{}; // +0x18 frames until a pending tag may be placed
    bool pending_{false}; // +0x1C a tag or sticker started
    bool blocked_{false}; // +0x1D BlockTaggingOn
    uint8_t pad1e_[2]{};
    float cooldown_{};                            // +0x20 frames until the next tag (counted down by 1)
    FootMovement* walk_{};                        // +0x24
    ObjectModule* input_{};                       // +0x28
    skater::SkaterPhysicsModule* core_physics_{}; // +0x2C
    ObjectModule* trigger_{};                     // +0x30
    TaggingComponent* tagging_{};                 // +0x34
};

} // namespace objects
