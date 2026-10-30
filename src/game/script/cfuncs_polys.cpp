// Script commands of the engine, 0x004472D0-0x00447F30: spawned polygons, vertex colours of the scenes' sectors
// (CompressVC / NudgeVC), the light direction and the renderer's metrics (see cfuncs_engine.h).

#include "game/script/cfuncs_engine.h"

#include <cstdint>

#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/lst/hashtable.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/sector.h"
#include "game/gfx/spawned_polys.h"
#include "game/gfx/texture_dict.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

using Script::CArray;
using Script::CScript;
using Script::CStruct;

namespace {

constexpr uint32_t kPos = 0x7F261953;          // "pos"
constexpr uint32_t kSpawnVector1 = 0xC2B31CF0; // (name unknown) SpawnPoly's first vector, (0, 0, 1, 1)
constexpr uint32_t kSpawnVector2 = 0x70A38CFD; // (name unknown) SpawnPoly's second vector, (0, 1, 0, 1)
constexpr uint32_t kStartDims = 0x0B1FE8B6;    // "start_dims"
constexpr uint32_t kEndDims = 0x1A640930;      // "end_dims"
constexpr uint32_t kLifetime = 0xC218CF77;     // "lifetime"
constexpr uint32_t kStartColors = 0xF78EABF0;  // "start_colors"
constexpr uint32_t kEndColors = 0x4D5A57A8;    // "end_colors"
constexpr uint32_t kStartColor = 0xECFCADE7;   // "start_color"
constexpr uint32_t kEndColor = 0xE836F013;     // "end_color"
constexpr uint32_t kSpawnFlag2 = 0x9C367E9B;   // (name unknown) SpawnPoly's flag 2
constexpr uint32_t kJust = 0x8B60022F;         // "just"
constexpr uint32_t kRight = 0x4B358AEB;        // "right"
constexpr uint32_t kLeft = 0x85981897;         // "left"
constexpr uint32_t kCenter = 0xBF0F14DB;       // "center"
constexpr uint32_t kTop = 0xE126E035;          // "top"
constexpr uint32_t kBottom = 0x76A08D5B;       // "bottom"
constexpr uint32_t kBlendMode = 0xDD6BB3D5;    // "BlendMode"
constexpr uint32_t kTexture = 0x7D99F28D;      // "texture"
constexpr uint32_t kTarget = 0xB990D003;       // "target"
constexpr uint32_t kPercent = 0x9E497FC6;      // "percent"
constexpr uint32_t kLightGroup = 0x7E4813A7;   // "LightGroup"
constexpr uint32_t kIndex = 0x7F8C98FE;        // "index"
constexpr uint32_t kDirection = 0xC1B52E4C;    // "direction"
constexpr uint32_t kHeading = 0xFD4BC03E;      // "heading"
constexpr uint32_t kPitch = 0xD8604126;        // "pitch"
constexpr uint32_t kMetrics = 0xDD755518;      // "Metrics"

// A spawned polygon's settings (0xB8 bytes, from the pool pair 0x00700500; constructed by 0x00491990: zeroed,
// blend mode 0x54628ED7). The fields not named are set elsewhere (0x00492450).
struct SpawnedPolyDetails {
    Script::VectorValue pos;   // +0x00
    uint8_t unknown10[0x48];
    float startDims[2];        // +0x58
    float endDims[2];          // +0x60
    uint8_t startColours[4][4]; // +0x68 (r, g, b, a per corner)
    uint8_t endColours[4][4];  // +0x78
    uint8_t unknown88[0x10];
    uint32_t flags;            // +0x98
    uint32_t unknown9C;
    float lifetime;            // +0xA0
    void* texture;             // +0xA4
    uint32_t unknownA8;
    uint32_t blendMode;        // +0xAC
    float justX;               // +0xB0 (-1 left, 0 centre, 1 right)
    float justY;               // +0xB4 (-1 top, 0 centre, 1 bottom)
};
static_assert(sizeof(SpawnedPolyDetails) == 0xB8, "layout must match the original");

Mem::PoolPair& SpawnedPolyPools() { return GameVariable<Mem::PoolPair>(0x00700500); }

// The loaded scenes' list (0x006FA7EC): nodes {?, scene +4, ?, next +0xC}, ended by a node whose scene is -1.
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene;
    uint32_t unknown8;
    SceneNode* next;
};
SceneNode* FirstSceneNode() { return reinterpret_cast<SceneNode*>(Nx::FirstSceneNode()); }
SceneNode* NextSceneNode(SceneNode* node)
{
    SceneNode* next = node->next;
    return next->scene == reinterpret_cast<uint8_t*>(-1) ? nullptr : next;
}
// A scene's flag at +0x8D: a sky. 0x0048C7B0: the first scene that is one.
bool IsSky(const uint8_t* scene) { return scene[0x8D] != 0; }
uint8_t* FirstSkyScene() { return reinterpret_cast<uint8_t*>(Nx::GetSkyScene()); }

// A sector's geometry (0x004A2BA0: +0x14) and the geometry's vertex count / positions / colours (0x0048DF70,
// 0x0048DF80, 0x0048E150 into a buffer, 0x0048DFA0 back from one).
void* SectorGeometry(void* sector) { return static_cast<Nx::CSector*>(sector)->mp_geom; }
// (the geometry's virtual slots 36, 37 and 40, called through its vtable)
template <typename Fn>
Fn GeometrySlot(const void* geometry, int slot)
{
    return reinterpret_cast<Fn>((*static_cast<void* const* const*>(geometry))[slot]);
}
int VertexCount(void* geometry) { return GeometrySlot<int(__thiscall*)(void*)>(geometry, 36)(geometry); }
void GetPositions(void* geometry, Script::VectorValue* out)
{
    GeometrySlot<void(__thiscall*)(void*, Script::VectorValue*)>(geometry, 37)(geometry, out);
}
void GetColours(void* geometry, uint32_t* out) { static_cast<Nx::CGeom*>(geometry)->GetColors(out); }
void SetColours(void* geometry, uint32_t* colours)
{
    GeometrySlot<void(__thiscall*)(void*, uint32_t*)>(geometry, 40)(geometry, colours);
}

// The CRT's _ftol (0x006236FC): truncation towards zero; callers keep the low bits.
int64_t Ftol(double value) { return static_cast<int64_t>(value); }

// A colour channel limited to 0..255 (NaN goes through) and truncated to a byte.
uint8_t ChannelByte(double value)
{
    if (value < 0.0)
        value = 0.0;
    else if (value > 255.0)
        value = 255.0;
    return static_cast<uint8_t>(Ftol(value));
}

// A scene's sectors (its hash table at +0xD0) - those with geometry, of the light group if one is given.
template <typename Fn>
void ForEachSectorGeometry(uint8_t* scene, uint32_t lightGroup, Fn fn)
{
    auto* sectors = *reinterpret_cast<Lst::HashTable**>(scene + 0xD0);
    if (!sectors)
        return;
    sectors->m_iterBucket = -1;
    sectors->m_iterItem = nullptr;
    if (!sectors->IterateNext() || !sectors->m_iterItem)
        return;
    for (auto* sector = static_cast<uint8_t*>(sectors->m_iterItem->value); sector;) {
        void* geometry = SectorGeometry(sector);
        if (geometry && (!lightGroup || lightGroup == *reinterpret_cast<uint32_t*>(sector + 0xC)))
            fn(geometry);
        if (!sectors->IterateNext() || !sectors->m_iterItem)
            break;
        sector = static_cast<uint8_t*>(sectors->m_iterItem->value);
    }
}

// The x87's own FSIN / FCOS (the CRT's round differently); products with the result kept at extended precision,
// as the original keeps it on the x87 stack.
float X87Sin(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
float X87Cos(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
double X87SinTimes(double angle, float value)
{
    double result;
    __asm {
        fld angle
        fsin
        fmul value
        fstp result
    }
    return result;
}
double X87CosTimes(double angle, float value)
{
    double result;
    __asm {
        fld angle
        fcos
        fmul value
        fstp result
    }
    return result;
}

} // namespace

// 0x004472D0: a spawned polygon's details given back to their pool pair (0x00700500) - its operator delete.
void __cdecl FreeSpawnedPolyDetails(void* details) { SpawnedPolyPools().Free(details); }

// 0x00447330: SpawnPoly pos=<vector> <vector> <vector> start_dims=<pair> end_dims=<pair> lifetime=<f>
// start_colors=[[r g b a] x4] end_colors=[...] | start_color=[r g b a] end_color=[...] [flag] [just=[h v]]
// [BlendMode] [texture]: a new spawned polygon's details filled in and passed on (0x00492450) with the two vectors.
// Always true.
bool __cdecl CFunc_SpawnPoly(CStruct* params, CScript*)
{
    auto* details = static_cast<SpawnedPolyDetails*>(SpawnedPolyPools().Allocate(sizeof(SpawnedPolyDetails)));
    if (details)
        details = reinterpret_cast<SpawnedPolyDetails*>(reinterpret_cast<Nx::sSpawnedPoly*>(details)->Construct());
    // (a failed allocation writes through null below, as in the original)
    params->GetVector(kPos, &details->pos, 1);
    Script::VectorValue vector1 = {0.0f, 0.0f, 1.0f, 1.0f};
    Script::VectorValue vector2 = {0.0f, 1.0f, 0.0f, 1.0f};
    params->GetVector(kSpawnVector1, &vector1, 0);
    params->GetVector(kSpawnVector2, &vector2, 0);
    params->GetPair(kStartDims, &details->startDims[0], &details->startDims[1], 1);
    params->GetPair(kEndDims, &details->endDims[0], &details->endDims[1], 1);
    params->GetFloat(kLifetime, &details->lifetime, 1);
    CArray* start = nullptr; // (uninitialised in the original)
    CArray* end = nullptr;
    if (params->GetArray(kStartColors, &start, 0)) {
        // a colour per corner
        params->GetArray(kEndColors, &end, 1);
        for (uint32_t corner = 0; corner < 4; ++corner) {
            auto* startColour = reinterpret_cast<CArray*>(start->GetElement(corner));
            auto* endColour = reinterpret_cast<CArray*>(end->GetElement(corner));
            for (uint32_t i = 0; i < 4; ++i)
                details->startColours[corner][i] = static_cast<uint8_t>(startColour->GetElement(i));
            for (uint32_t i = 0; i < 4; ++i)
                details->endColours[corner][i] = static_cast<uint8_t>(endColour->GetElement(i));
        }
    } else if (params->GetArray(kStartColor, &start, 0)) {
        // one colour for every corner
        params->GetArray(kEndColor, &end, 1);
        for (uint32_t corner = 0; corner < 4; ++corner) {
            for (uint32_t i = 0; i < 4; ++i)
                details->startColours[corner][i] = static_cast<uint8_t>(start->GetElement(i));
            for (uint32_t i = 0; i < 4; ++i)
                details->endColours[corner][i] = static_cast<uint8_t>(end->GetElement(i));
        }
    }
    if (params->ContainsFlag(kSpawnFlag2))
        details->flags |= 2;
    CArray* just = nullptr;
    details->justX = 0.0f;
    details->justY = 0.0f;
    if (params->GetArray(kJust, &just, 0)) {
        switch (just->GetElement(0)) {
        case kRight: details->justX = 1.0f; break;
        case kLeft: details->justX = -1.0f; break;
        case kCenter: details->justX = 0.0f; break;
        default: break;
        }
        switch (just->GetElement(1)) {
        case kBottom: details->justY = 1.0f; break;
        case kCenter: details->justY = 0.0f; break;
        case kTop: details->justY = -1.0f; break;
        default: break;
        }
    }
    params->GetChecksum(kBlendMode, &details->blendMode, 0);
    uint32_t texture = 0;
    params->GetChecksum(kTexture, &texture, 0);
    details->texture = texture ? static_cast<Nx::CTexDict*>(GameVariable<void*>(0x00700F20))->GetTexture(texture) // the texture dictionary
                               : nullptr;
    Nx::Engine::SpawnPoly(reinterpret_cast<Nx::sSpawnedPoly*>(details), reinterpret_cast<Mth::Vector*>(&vector1),
                          reinterpret_cast<Mth::Vector*>(&vector2));
    return true;
}

// 0x00447710: the vertex colours of a scene's sectors (of `lightGroup`, if given) moved towards `target`: each
// channel c becomes (c - target) * factor + target, limited to 0..255 (alpha kept). Nothing when target and factor
// are both 0.
void __cdecl CompressSceneVertexColours(uint8_t* scene, float target, float factor, uint32_t lightGroup)
{
    ForEachSectorGeometry(scene, lightGroup, [&](void* geometry) {
        int count = VertexCount(geometry);
        if (!count)
            return;
        // (the original runs an empty constructor over the array: 0x005902E0)
        auto* colours = static_cast<uint8_t*>(Mem::New(static_cast<unsigned int>(count) * 4));
        GetColours(geometry, reinterpret_cast<uint32_t*>(colours));
        if (target != 0.0f || factor != 0.0f) {
            for (int i = 0; i < count; ++i) {
                uint8_t* colour = colours + 4 * i;
                // x87: in double, rounded only by the truncation
                for (int channel = 0; channel < 3; ++channel)
                    colour[channel] = ChannelByte((static_cast<double>(colour[channel]) - target) * factor + target);
            }
        }
        SetColours(geometry, reinterpret_cast<uint32_t*>(colours));
        Mem::Free(colours);
    });
}

// 0x00447980: CompressVC target=<n> percent=<n> [LightGroup=<name>]: every scene's (skies apart, then the first
// sky's) vertex colours compressed towards target, to (100 - percent)% of their distance. Always true.
bool __cdecl CFunc_CompressVC(CStruct* params, CScript*)
{
    float target = 0.0f;
    float percent = 0.0f;
    params->GetFloat(kTarget, &target, 1);
    params->GetFloat(kPercent, &percent, 0);
    auto factor = static_cast<float>((100.0 - percent) * static_cast<double>(0.01f)); // (x87: rounded once)
    uint32_t lightGroup = 0;
    params->GetChecksum(kLightGroup, &lightGroup, 0);
    for (SceneNode* node = FirstSceneNode(); node;) {
        SceneNode* next = NextSceneNode(node);
        if (!IsSky(node->scene))
            CompressSceneVertexColours(node->scene, target, factor, lightGroup);
        node = next;
    }
    if (uint8_t* sky = FirstSkyScene())
        CompressSceneVertexColours(sky, target, factor, lightGroup);
    return true;
}

// 0x00447A60: the vertex colours of a scene's sectors scaled by a pseudo-random amount per vertex: a 15-bit hash
// of the position (in 1/16ths) h gives the scale 1 - amount + 2 * amount * h / 32768 (colours limited to 0..255,
// alpha kept).
void __cdecl NudgeSceneVertexColours(uint8_t* scene, float amount)
{
    ForEachSectorGeometry(scene, 0, [&](void* geometry) {
        int count = VertexCount(geometry);
        if (!count)
            return;
        // (the original runs an empty constructor over both arrays: 0x005902E0)
        auto* positions = static_cast<Script::VectorValue*>(Mem::New(static_cast<unsigned int>(count) * 16));
        auto* colours = static_cast<uint8_t*>(Mem::New(static_cast<unsigned int>(count) * 4));
        GetPositions(geometry, positions);
        GetColours(geometry, reinterpret_cast<uint32_t*>(colours));
        for (int i = 0; i < count; ++i) {
            const Script::VectorValue& position = positions[i];
            uint8_t* colour = colours + 4 * i;
            auto hash = static_cast<uint32_t>(Ftol(static_cast<double>(position.x) * 16.0)) *
                        static_cast<uint32_t>(Ftol(static_cast<double>(position.y) * 16.0));
            hash = (hash ^ static_cast<uint32_t>(Ftol(static_cast<double>(position.z) * 16.0))) & 0x7FFF;
            // x87: the scale rounded to a float; red and green products rounded to floats, blue's kept in double
            auto scale = static_cast<float>(static_cast<double>(hash) * (1.0 / 32768.0) * amount * 2.0 + 1.0 - amount);
            auto red = static_cast<float>(colour[0] * static_cast<double>(scale));
            auto green = static_cast<float>(colour[1] * static_cast<double>(scale));
            double blue = colour[2] * static_cast<double>(scale);
            colour[0] = ChannelByte(red);
            colour[1] = ChannelByte(green);
            colour[2] = ChannelByte(blue);
        }
        SetColours(geometry, reinterpret_cast<uint32_t*>(colours));
        Mem::Free(positions);
        Mem::Free(colours);
    });
}

// 0x00447D90: NudgeVC percent=<n>: every scene's vertex colours (skies too) nudged by up to percent%. Always true.
bool __cdecl CFunc_NudgeVC(CStruct* params, CScript*)
{
    float percent = 0.0f;
    params->GetFloat(kPercent, &percent, 1);
    percent = percent * 0.01f;
    for (SceneNode* node = FirstSceneNode(); node;) {
        SceneNode* next = NextSceneNode(node);
        NudgeSceneVertexColours(node->scene, percent);
        node = next;
    }
    return true;
}

// 0x00447E00: SetLightDirection index=<n> direction=<vector> | heading=<degrees> pitch=<degrees>: the light's
// direction (0x0048ED70; w 0): the vector, or (0, 0, 1) turned by pitch about x and then by heading about y (both
// needed; otherwise (0, 0, 1)). Returns what 0x0048ED70 returns (its AL).
bool __cdecl CFunc_SetLightDirection(CStruct* params, CScript*)
{
    constexpr float kDegreesToRadians = 0.0174532924f; // (0x00645464)
    int index = 0; // (uninitialised in the original)
    params->GetInteger(kIndex, &index, 0);
    Script::VectorValue direction = {0.0f, 0.0f, 1.0f, 0.0f};
    float heading;
    float pitch;
    if (params->GetVector(kDirection, &direction, 0)) {
        direction.w = 0.0f;
    } else if (params->GetFloat(kHeading, &heading, 0) && params->GetFloat(kPitch, &pitch, 0)) {
        double p = static_cast<double>(pitch) * kDegreesToRadians;
        float y = direction.y;
        float z = direction.z;
        direction.y = static_cast<float>(X87CosTimes(p, y) - X87SinTimes(p, z));
        double turnedZ = X87CosTimes(p, z) + X87SinTimes(p, y);
        double h = static_cast<double>(heading) * kDegreesToRadians;
        float s = X87Sin(h);
        float c = X87Cos(h);
        float x = direction.x;
        direction.z = static_cast<float>(-(static_cast<double>(s) * x) + turnedZ * c);
        direction.x = static_cast<float>(turnedZ * s + static_cast<double>(c) * x);
    }
    return Nx::SetLightDirection(index, reinterpret_cast<const Mth::Vector*>(&direction));
}

// 0x00447F30: GetMetrics: the renderer's metrics (0x0048CB00 into a new structure) added to the script's params as
// Metrics. Always true.
bool __cdecl CFunc_GetMetrics(CStruct*, CScript* script)
{
    CStruct* metrics = Script::NewStruct();
    if (metrics)
        metrics = metrics->Init();
    Nx::GetSceneDebugInfo(metrics);
    script->m_params->AddStructure(kMetrics, metrics);
    if (metrics) {
        metrics->Clear();
        Script::DeleteStruct(metrics);
    }
    return true;
}

} // namespace CFuncs

REPLACE(0x004472D0, CFuncs::FreeSpawnedPolyDetails);
REPLACE(0x00447330, CFuncs::CFunc_SpawnPoly);
REPLACE(0x00447710, CFuncs::CompressSceneVertexColours);
REPLACE(0x00447980, CFuncs::CFunc_CompressVC);
REPLACE(0x00447A60, CFuncs::NudgeSceneVertexColours);
REPLACE(0x00447D90, CFuncs::CFunc_NudgeVC);
REPLACE(0x00447E00, CFuncs::CFunc_SetLightDirection);
REPLACE(0x00447F30, CFuncs::CFunc_GetMetrics);
