#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// Graffiti tags and stickers: the tagging component holds a skater's two textures and puts a tag or sticker on the
// world (a decal, told to the server in a network game); the tagging control component decides when - a walking
// skater tagging a wall it faces (the tag button), or slapping a sticker on a wall from vert.
namespace Obj {

// The tagging component (Tagging, type 0x5B512EDC, vtable 0x0064C330, 0x24 bytes; made by 0x0051DA80).
// Vtable: 0 Destroy, 1 Update 0x0051B970 (shared: suspends itself), 2 InitFromStructure, 12 CallMemberFunction.
class CTaggingComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x5B512EDC;

    // 0x0051DA80 (registered with the composite object manager): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0051D760: the destructor body: both textures given back, then the base's.
    void DestroyBody();
    // 0x0051D860 (vtable slot 0)
    CTaggingComponent* Destroy(unsigned int flags);
    // 0x0051D890 (vtable slot 2): its own copies of tags\cas_01 and tags\sticker, keyed by the skater number
    // (0xA2B033FD of `params`; without it, what its slot held: `params` itself).
    void InitFromStructure(Script::CStruct* params);
    // 0x0051DB00 (vtable slot 12): SetGraffitiTexture / SetStickerTexture (the unnamed string), HasCustomTagTexture,
    // ApplyCustomTagTexture. Other names: 2 (not mine).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x0051D830: the custom tag texture (0xAA286543) put on the graffiti texture.
    void ApplyCustomTagTexture();
    // 0x0051D850 (4 bytes: source only): whether the skater has a custom tag texture.
    bool HasCustomTagTexture();
    // 0x0051D9C0 / 0x0051DA20: a texture loaded from `name` and put on the graffiti / sticker texture.
    void SetGraffitiTexture(const char* name);
    void SetStickerTexture(const char* name);
    // 0x0051DBC0: a tag (or with `sticker` a sticker, turned by up to 45 degrees at random) placed from `start`
    // towards `end`, `size` big; with `send`, told to the server in a network game (message 0x99).
    void PlaceTag(bool sticker, Mth::Vector start, Mth::Vector end, int size, bool send);

    bool m_customTag;     // +0x18
    uint8_t pad19[3];
    void* m_graffiti;     // +0x1C the graffiti texture
    void* m_sticker;      // +0x20 the sticker texture
};
static_assert(offsetof(CTaggingComponent, m_graffiti) == 0x1C && sizeof(CTaggingComponent) == 0x24,
              "layout must match the original");

// The tagging control component (TaggingControl, type 0x380D909A, vtable 0x0064C394, 0x38 bytes; made by 0x0051E280).
// Vtable: 0 Destroy, 1 Update (0x0051EA30, a jump to 0x0051E390), 3 Finalize, 12 CallMemberFunction.
class CTaggingControlComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x380D909A;

    // 0x0051E280 (registered with the composite object manager): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0051DD50: the destructor body (the base's).
    void DestroyBody();
    // 0x0051DD60 (vtable slot 0)
    CTaggingControlComponent* Destroy(unsigned int flags);
    // 0x0051DD90 (vtable slot 3): the skater's walk, input, core physics, trigger and tagging components.
    void Finalize();
    // 0x0051E300 (vtable slot 12): TagNow, VertStickerSlap, TagClearFlags, BlockTaggingOn / Off. Other names: 2.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x0051E390 (vtable slot 1, through 0x0051EA30): a walking skater (not blocked, out of its cooldown) tagging:
    // the tag button tapped - feelers from the tagging bone in six directions ahead find the nearest wall; the first
    // tap starts the tag (script 0x8C92DEA3 / 0x5213AFD6, 30 frames), the next one places it (bigger with a bigger
    // score) and starts the cooldown. Returns whether it got past the checks.
    bool Update();
    // 0x0051DDF0: a sticker slapped (or with `sticker` false, a tag sprayed) on the wall the skater faces from the
    // air: a feeler along its facing (`walking`: the walk component's heading; `low`: the lower spray height) finds
    // the wall; a sticker is only marked the first time and placed the next. False before the cooldown or the
    // countdown are over, or without a wall.
    bool SlapSticker(bool sticker, bool walking, bool low);

    int m_countdown;                // +0x18 frames until a pending tag may be placed
    bool m_pending;                 // +0x1C a tag or sticker started
    bool m_blocked;                 // +0x1D BlockTaggingOn
    uint8_t pad1E[2];
    float m_cooldown;               // +0x20 frames until the next tag (counted down by 1)
    CBaseComponent* m_walk;         // +0x24
    CBaseComponent* m_input;        // +0x28
    CBaseComponent* m_corePhysics;  // +0x2C
    CBaseComponent* m_trigger;      // +0x30
    CTaggingComponent* m_tagging;   // +0x34
};
static_assert(offsetof(CTaggingControlComponent, m_cooldown) == 0x20 &&
                  offsetof(CTaggingControlComponent, m_tagging) == 0x34 && sizeof(CTaggingControlComponent) == 0x38,
              "layout must match the original");

} // namespace Obj
