// Script commands (CFunctions) 0x005B3A90-0x005B4260: create-a-graphic, 2D positions, head bone groups (see
// cag_cfuncs.h).

#include "game/gfx/cag_cfuncs.h"

#include <cstdint>

#include "game/mth/math.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/tagging.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CArray;
using Script::CStruct;

constexpr uint32_t kInfoArray = 0x54AAD4BD;      // "info_array"
constexpr uint32_t kCustomTag = 0xAA286543;      // (name unknown) the custom tag texture (in the dictionary 0x00700F20)
constexpr uint32_t kWorkspace = 0xFDA19704;      // (name unknown) the layer workspace texture
constexpr uint32_t kTextureId = 0xDFB43C53;      // "texture_id"
constexpr uint32_t kScale = 0x13B9DA7B;          // "scale"
constexpr uint32_t kFlipH = 0xA0C7507F;          // "flip_h"
constexpr uint32_t kFlipV = 0x5AC86D1C;          // "flip_v"
constexpr uint32_t kRot = 0xE2C6589E;            // "rot"
constexpr uint32_t kPosX = 0x960061E4;           // "pos_x"
constexpr uint32_t kPosY = 0xE1075172;           // "pos_y"
constexpr uint32_t kRgba = 0x3F6BCDBA;           // "rgba"
constexpr uint32_t kLayerId = 0x15910232;        // "layer_id"
constexpr uint32_t kPlayer = 0x67E6859A;         // "player"
constexpr uint32_t kTest = 0x278081F3;           // "Test"
constexpr uint32_t kWorkspaceSprite = 0xAFFFDBCA; // "cag_workspace_sprite"
constexpr uint32_t kSkater = 0x5B8AB877;         // "Skater"
constexpr uint32_t kMagnitude = 0xCA7882FE;      // "magnitude"
constexpr uint32_t kTheta = 0xF4E775F6;          // "theta"
constexpr uint32_t kX = 0x7323E97C;              // "x"
constexpr uint32_t kY = 0x0424D9EA;              // "Y"
constexpr uint32_t kSkaterMHead = 0x650FAB6D;    // "skater_m_head"
constexpr uint32_t kSkaterFHead = 0x0FC85BAE;    // "skater_f_head"
constexpr uint32_t kHeadBoneGroups[4] = {
    0xA28FAB7F, // "head_bone_group"
    0x1DBCFAE8, // "nose_bone_group"
    0x0442D3C1, // "jaw_bone_group"
    0xB90F08D0, // "headtop_bone_group"
};

// The texture dictionaries (0x00700F1C the game's, 0x00700F20 the skater's) and a texture by checksum (0x0049F540),
// a new texture (0x0049FAA0: name, width, height, then 4 / 8, 0x20, 1, 1) and one taken out (0x0049F500).
void* Textures() { return GameVariable<void*>(0x00700F1C); }
void* SkaterTextures() { return GameVariable<void*>(0x00700F20); }
void* FindTexture(void* dictionary, uint32_t name)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x0049F540)(dictionary, name);
}
void* NewTexture(uint32_t name, int width, int height, int a, int b)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t, int, int, int, int, bool, bool)>(0x0049FAA0)(
        Textures(), name, width, height, a, b, true, true);
}

// A texture's methods (each a jump through its vtable).
template <typename R, typename... A>
R TextureCall(uint32_t thunk, void* texture, A... args)
{
    return GameFunction<R(__thiscall*)(void*, A...)>(thunk)(texture, args...);
}

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

Mdl::Skate* Skate() { return Mdl::Skate::Instance(); }

} // namespace

// 0x005B3A90: GenerateCAGTexture info_array=[{texture_id scale flip_h flip_v rot pos_x pos_y rgba layer_id}...]
// player=n [Test=1]: the custom tag texture composed from its layers: cleared, then each layer's texture drawn into a
// workspace texture (as big as the tag), scaled, flipped, turned, moved (positions doubled for a 128-wide tag) and
// tinted, and blended onto the tag; the tag's palette made again (kept to opaque when no layer was translucent and
// it's an 8-bit tag); the workspace thrown away. Player 0's skater's tag is then marked custom and put on its graffiti
// texture; with Test, the result also shown on cag_workspace_sprite. Always true.
bool __cdecl CFunc_GenerateCAGTexture(Script::CStruct* params, Script::CScript*)
{
    uint8_t palette[0x400]; // (filled by the tag's palette call, not here)
    CArray* layers = nullptr;
    params->GetArray(kInfoArray, &layers, 1);
    void* tag = FindTexture(SkaterTextures(), kCustomTag);
    TextureCall<void>(0x0048D060, tag, 0, 0);
    TextureCall<void>(0x0048DE50, tag, 0u, false);
    uint32_t size = TextureCall<uint32_t>(0x0049F3C0, tag);
    int width = static_cast<uint16_t>(size);
    void* workspace = NewTexture(kWorkspace, width, width, 4, 0x20);
    TextureCall<void>(0x0048D060, workspace, 0, 0);

    uint32_t count = layers->m_size;
    bool translucent = false;
    int lastLayer = -1;
    for (uint32_t i = 0; i < count; ++i) {
        auto* info = reinterpret_cast<CStruct*>(layers->GetElement(i));
        uint32_t textureId;
        info->GetChecksum(kTextureId, &textureId, 1);
        void* source = FindTexture(Textures(), textureId);
        TextureCall<void>(0x0048D060, source, 0, 0);
        TextureCall<void>(0x0048E4C0, workspace);
        TextureCall<void>(0x0048D060, workspace, 0, 0);
        TextureCall<void>(0x0048DE80, workspace, source, 0);
        TextureCall<void>(0x0048E4C0, source);
        TextureCall<void>(0x0048DED0, workspace, 0u);
        float scale;
        info->GetFloat(kScale, &scale, 1);
        TextureCall<void>(0x0048DEB0, workspace, scale, scale);
        int flipH;
        info->GetInteger(kFlipH, &flipH, 1);
        int flipV;
        info->GetInteger(kFlipV, &flipV, 1);
        if (flipH)
            TextureCall<void>(0x0048DE60, workspace);
        if (flipV)
            TextureCall<void>(0x0048DE70, workspace);
        float rot;
        info->GetFloat(kRot, &rot, 1);
        float radians = static_cast<float>(static_cast<double>(rot) * GameVariable<float>(0x00645464)); // pi / 180
        TextureCall<void>(0x0049F360, workspace, radians, -1.0f, -1.0f);
        float x;
        info->GetFloat(kPosX, &x, 1);
        float y;
        info->GetFloat(kPosY, &y, 1);
        if (static_cast<uint16_t>(size) == 0x80) {
            x = x + x;
            y = y + y;
        }
        TextureCall<void>(0x0048DEC0, workspace, Ftol(x), Ftol(y), 0, 0u);
        CArray* rgba;
        info->GetArray(kRgba, &rgba, 1);
        uint8_t a = static_cast<uint8_t>(Ftol(rgba->GetFloat(3)));
        uint8_t b = static_cast<uint8_t>(Ftol(rgba->GetFloat(2)));
        uint8_t g = static_cast<uint8_t>(Ftol(rgba->GetFloat(1)));
        uint8_t r = static_cast<uint8_t>(Ftol(rgba->GetFloat(0)));
        uint32_t colour = r | g << 8 | b << 16 | static_cast<uint32_t>(a) << 24;
        TextureCall<void>(0x0048DE50, workspace, colour, false);
        if (a < 0x80)
            translucent = true;
        int layer;
        info->GetInteger(kLayerId, &layer, 1);
        if (lastLayer != layer)
            lastLayer = layer; // (kept, never used)
        TextureCall<void>(0x0048DE30, workspace, 1, static_cast<void*>(nullptr));
        TextureCall<void>(0x0049F420, tag, workspace, 1);
    }

    TextureCall<void>(0x0048DED0, tag, 0u);
    if (!translucent && TextureCall<uint8_t>(0x0049C6F0, tag) == 8)
        TextureCall<void>(0x0048DE30, tag, 0, static_cast<void*>(palette));
    else
        TextureCall<void>(0x0048DE30, tag, 1, static_cast<void*>(nullptr));
    GameFunction<bool(__thiscall*)(void*, void*)>(0x0049F500)(Textures(), workspace);

    int player;
    params->GetInteger(kPlayer, &player, 1);
    if (player == 0) {
        auto* tagging = static_cast<Obj::CTaggingComponent*>(Skate()->GetSkater(0)->GetComponent(Obj::CTaggingComponent::kType));
        tagging->m_customTag = true; // (0x004E4000, a setter folded with others)
        tagging->ApplyCustomTagTexture();
    }
    int test = 0;
    params->GetInteger(kTest, &test, 0);
    if (test) {
        void* sprite = NewTexture(kWorkspaceSprite, 0x80, 0x80, 8, 0x20);
        TextureCall<void>(0x00487D50, sprite, tag);
    }
    return true;
}

// 0x005B3ED0: SyncCAGWorkspace Skater=n: the custom tag texture copied from skater n's graffiti texture, if it has one
// (the current profile is fetched and not used). Always true.
bool __cdecl CFunc_SyncCAGWorkspace(Script::CStruct* params, Script::CScript*)
{
    int index;
    params->GetInteger(kSkater, &index, 1);
    Obj::CCompositeObject* skater = Skate()->GetSkater(index);
    GameFunction<Obj::CSkaterProfile*(__thiscall*)(Mdl::Skate*, CStruct*)>(0x00564F80)(Skate(), nullptr);
    auto* tagging = static_cast<Obj::CTaggingComponent*>(skater->GetComponent(Obj::CTaggingComponent::kType));
    void* graffiti = tagging->m_graffiti; // (0x00553E90, a getter folded with others)
    if (graffiti)
        TextureCall<void>(0x00487D50, FindTexture(SkaterTextures(), kCustomTag), graffiti);
    return true;
}

// 0x005B3F40: Get2DPosFrom2DVec magnitude=m theta=degrees: x and Y, the vector's components (theta brought into the
// first quadrant first, the signs put back after; the axes exactly for 0, 90, 180, 270 and 360; otherwise the fast
// sine and cosine). Always true.
bool __cdecl CFunc_Get2DPosFrom2DVec(Script::CStruct* params, Script::CScript* script)
{
    float magnitude;
    params->GetFloat(kMagnitude, &magnitude, 1);
    float theta;
    params->GetFloat(kTheta, &theta, 1);
    bool negateX = false;
    bool negateY = false;
    if (theta > 90.0f && theta < 180.0f) {
        negateX = true;
        theta = static_cast<float>(90.0 - (static_cast<double>(theta) - 90.0));
    } else if (theta > 180.0f && theta < 270.0f) {
        negateX = true;
        theta = static_cast<float>(static_cast<double>(theta) - 180.0);
        negateY = true;
    } else if (theta > 270.0f && theta < 360.0f) {
        theta = static_cast<float>(360.0 - theta);
        negateY = true;
    }
    float x;
    float y;
    if (theta == 0.0f || theta == 360.0f) {
        x = magnitude;
        y = 0.0f;
    } else if (theta == 90.0f) {
        x = 0.0f;
        y = magnitude;
    } else if (theta == 180.0f) {
        x = magnitude;
        y = 0.0f;
        negateX = true; // (the original goes straight to the negation)
    } else if (theta == 270.0f) {
        x = 0.0f;
        y = magnitude;
        negateY = true;
    } else {
        float radians = static_cast<float>(static_cast<double>(theta) * GameVariable<float>(0x00645464)); // pi / 180
        y = static_cast<float>(static_cast<double>(Mth::FastSin(radians)) * magnitude);
        x = static_cast<float>(static_cast<double>(Mth::FastCos(radians)) * magnitude);
    }
    if (negateX)
        x = x * -1.0f;
    if (negateY)
        y = y * -1.0f;
    script->m_params->AddFloat(kX, x);
    script->m_params->AddFloat(kY, y);
    return true;
}

// 0x005B4150: ResolveHeadBoneScale player=n: player n's profile's appearance given each head bone group (head, nose,
// jaw, headtop) it lacks, from its head part (skater_m_head, else skater_f_head). False without a head part; true
// otherwise (also without a profile).
bool __cdecl CFunc_ResolveHeadBoneScale(Script::CStruct* params, Script::CScript*)
{
    int player = 0;
    params->GetInteger(kPlayer, &player, 1);
    auto* profile = GameFunction<Obj::CSkaterProfile*(__thiscall*)(Mdl::Skate*, int)>(0x00564FC0)(Skate(), player);
    if (!profile)
        return true;
    // the appearance's part structures (0x00489050: a part's, through its description; 0x004890B0: the appearance's
    // own component of that name)
    auto partOf = [&](uint32_t name) {
        return GameFunction<CStruct*(__thiscall*)(Obj::CSkaterProfile*, uint32_t)>(0x00489050)(profile, name);
    };
    CStruct* head = partOf(kSkaterMHead);
    if (!head) {
        head = partOf(kSkaterFHead);
        if (!head)
            return false;
    }
    void* has[4];
    for (int i = 0; i < 4; ++i)
        has[i] = GameFunction<void*(__thiscall*)(Obj::CSkaterProfile*, uint32_t)>(0x004890B0)(profile, kHeadBoneGroups[i]);
    auto* appearance = reinterpret_cast<CStruct*>(profile->m_appearance + 0x4C);
    for (int i = 0; i < 4; ++i) {
        if (has[i])
            continue;
        CStruct* group;
        if (head->GetStructure(kHeadBoneGroups[i], &group, 0))
            appearance->AddStructure(kHeadBoneGroups[i], group);
    }
    return true;
}

} // namespace CFuncs

REPLACE(0x005B3A90, CFuncs::CFunc_GenerateCAGTexture);
REPLACE(0x005B3ED0, CFuncs::CFunc_SyncCAGWorkspace);
REPLACE(0x005B3F40, CFuncs::CFunc_Get2DPosFrom2DVec);
REPLACE(0x005B4150, CFuncs::CFunc_ResolveHeadBoneScale);
