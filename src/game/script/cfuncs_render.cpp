// Script commands of the engine, 0x00446460-0x00447220: render debugging, the animation system, the light manager,
// the screen, texture splats, gamma and light values for scripts, clipping and fog distances, the fog colour, render
// modes, scene lights and bone positions (see cfuncs_engine.h).

#include "game/script/cfuncs_engine.h"

#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/gfx/debug_lines.h"
#include "game/gfx/screen.h"
#include "game/mth/geometry.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/skeleton_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/gfx/anim_cache.h"
#include "game/gfx/anim_blend.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/viewport.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/settings.h"
#include "game/gfx/device.h"

namespace CFuncs {

using Script::CScript;
using Script::CStruct;

GAME_GLOBAL(float, g_renderDefaultFarPlane, 0x006487D0, 12.0f);
GAME_GLOBAL(float, g_renderDefaultNearPlane, 0x006487D4, 12000.0f);
GAME_GLOBAL(int, g_renderMode, 0x006FA730);

namespace {

constexpr uint32_t kIgnore1 = 0xD12347A6;         // "ignore_1"
constexpr uint32_t kIgnore0 = 0xA6247730;         // "ignore_0"
constexpr uint32_t kT48 = 0xC06EAD08;             // "t48"
constexpr uint32_t kQ48 = 0xC6A56FE3;             // "q48"
constexpr uint32_t kAnimHeapSize = 0x83DFF28A;    // "AnimHeapSize"
constexpr uint32_t kAnimCacheSize = 0x7AA8C11C;   // (name unknown) the animation cache's second size
constexpr uint32_t kDefCacheType = 0x891A96A9;    // "DefCacheType"
constexpr uint32_t kCacheType3 = 0x50428FBD;      // (name unknown) cache type 3
constexpr uint32_t kCacheType4 = 0x7ADB1481;      // (name unknown) cache type 4
constexpr uint32_t kStandard = 0xEF082878;        // "standard": cache type 2 (others 0)
constexpr uint32_t kMaxAnimBlends = 0x416BC96B;   // (name unknown) the first of 0x0047EB30's sizes
constexpr uint32_t kMaxAnimSubsets = 0xD14E9A11;  // "MaxAnimSubsets"
constexpr uint32_t kDisable = 0xB0BAB2AE;         // "disable"
constexpr uint32_t kEnable = 0x0C5F37CD;          // "enable"
constexpr uint32_t kDefrag = 0x6A8251D1;          // "defrag"
constexpr uint32_t kClear = 0x1A4E0EF9;           // "clear"
constexpr uint32_t kMaxLights = 0x50228D9E;       // "max_lights"
constexpr uint32_t kMaxModelLights = 0x701C70AC;  // (name unknown) the light manager's second count
constexpr uint32_t kAspect = 0x0F7C3CAA;          // "aspect"
constexpr uint32_t kAngle = 0xFF7EBAF6;           // "angle"
constexpr uint32_t kLetterbox = 0xD4328DD3;       // "letterbox"
constexpr uint32_t kAllow = 0x81B6BF17;           // "allow"
constexpr uint32_t kStart = 0x6086AA70;           // "start"
constexpr uint32_t kEnd = 0xFF03CC4E;             // "end"
constexpr uint32_t kColor = 0x99A9B716;           // "color"
constexpr uint32_t kFrames = 0x019176C5;          // "frames"
constexpr uint32_t kBox = 0xF756B7C5;             // "box"
constexpr uint32_t kHeight = 0x0AB21AF0;          // "height"
constexpr uint32_t kDist = 0x7E832F08;            // "dist"
constexpr uint32_t kWidth = 0x73E5BAD0;           // "width"
constexpr uint32_t kOffset = 0xA6F5352F;          // "offset"
constexpr uint32_t kTexture = 0x7D99F28D;         // "texture"
constexpr uint32_t kRed = 0x059EA070;             // "red"
constexpr uint32_t kGreen = 0x2F6511DE;           // "green"
constexpr uint32_t kBlue = 0x61C9354B;            // "blue"
constexpr uint32_t kAmbient = 0xBCEE1D37;         // "ambient"
constexpr uint32_t kDirectional = 0xB8F8BBE6;     // "directional"
constexpr uint32_t kModValueLo = 0xF2BB5523;      // "mod_value_lo"
constexpr uint32_t kModValueHi = 0x7FB43512;      // "mod_value_hi"
constexpr uint32_t kMode = 0x6835B854;            // "mode"
constexpr uint32_t kIndex = 0x7F8C98FE;           // "index"
constexpr uint32_t kHeading = 0xFD4BC03E;         // "heading"
constexpr uint32_t kPitch = 0xD8604126;           // "pitch"
constexpr uint32_t kUpperR = 0x93F60062;          // "R"
constexpr uint32_t kUpperG = 0xFE2BE489;          // "G"
constexpr uint32_t kUpperB = 0x8E411006;          // "B"
constexpr uint32_t kAmbientRed = 0xED664897;      // "ambient_red"
constexpr uint32_t kAmbientGreen = 0x908BF647;    // "ambient_green"
constexpr uint32_t kAmbientBlue = 0x5F4FBA78;     // "ambient_blue"
constexpr uint32_t kRed0 = 0xE213AEB7;            // "red_0"
constexpr uint32_t kGreen0 = 0x6EF19631;          // "green_0"
constexpr uint32_t kBlue0 = 0x5EC44213;           // "blue_0"
constexpr uint32_t kRed1 = 0x95149E21;            // "red_1"
constexpr uint32_t kGreen1 = 0x19F6A6A7;          // "green_1"
constexpr uint32_t kBlue1 = 0x29C37285;           // "blue_1"
constexpr uint32_t kId = 0x40C698AF;              // "id"
constexpr uint32_t kBone = 0xCAB94088;            // "bone"
constexpr uint32_t kSkeleton = 0x222756D5;        // "skeleton" (the component type)
constexpr uint32_t kX = 0x7323E97C;               // "x"
constexpr uint32_t kY = 0x0424D9EA;               // "y"
constexpr uint32_t kZ = 0x9D2D8850;               // "z"

bool DistanceClipping() { return Nx::g_distanceClipping; }

// Compiled out of this build - jumps to the empty function 0x005480A0 (0x0048C1B0) or calls of it.
inline void FinishRendering() {}
inline void AllowBoneLOD(bool /*allow*/) {}
inline void PrintAnimCacheInfo(void* /*cache*/) {}

// The animation cache manager (0x006F92B0 through its getter 0x00480660).
Gfx::CAnimCache* AnimCache() { return Gfx::CAnimCache::Get(); }

// The viewport's camera `index` (0x004A0950: null if the viewport has none).
uint8_t* ViewportCamera(int index) { return reinterpret_cast<uint8_t*>(Nx::GetCamera(index)); }

// A colour of four bytes (r, g, b, a in memory order).
uint32_t PackBytes(int r, int g, int b, int a)
{
    return static_cast<uint8_t>(r) | static_cast<uint8_t>(g) << 8 | static_cast<uint8_t>(b) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24;
}

} // namespace

// 0x00446460: DebugRenderIgnore ignore_0=<n> ignore_1=<n> (0x0048C110). Always true.
bool __cdecl CFunc_DebugRenderIgnore(CStruct* params, CScript*)
{
    int ignore1 = 0;
    int ignore0 = 0;
    params->GetInteger(kIgnore1, &ignore1, 0);
    params->GetInteger(kIgnore0, &ignore0, 0);
    Nx::Engine::SetRenderIgnore(ignore1, ignore0);
    return true;
}

// 0x004464C0: InitAnimCompressTable "<file>" t48 | q48: the animation compression table loaded from the file
// (0x00482A30 for t48, 0x004829F0 for q48; nothing for another type). Always true.
bool __cdecl CFunc_InitAnimCompressTable(CStruct* params, CScript*)
{
    const char* file = nullptr; // (both asserted; uninitialised in the original)
    uint32_t type = 0;
    params->GetString(0u, &file, 1);
    params->GetChecksum(0u, &type, 1);
    char name[256];
    std::snprintf(name, sizeof(name), "%s", file); // (the original's sprintf isn't bounded)
    if (type == kT48)
        Gfx::LoadTransTable(name);
    else if (type == kQ48)
        Gfx::LoadQuatTable(name);
    return true;
}

// 0x00446570: InitAnimSystem AnimHeapSize <size 2> DefCacheType <counts>: the animation cache (0x00480D10: the
// sizes and the type - 2 standard, 3 / 4 two others, 0 anything else) and the blend system (0x0047EB30). Always true.
bool __cdecl CFunc_InitAnimSystem(CStruct* params, CScript*)
{
    int heapSize = 0;
    int cacheSize = 0;
    uint32_t cacheType = 0;
    params->GetInteger(kAnimHeapSize, &heapSize, 1);
    params->GetInteger(kAnimCacheSize, &cacheSize, 1);
    params->GetChecksum(kDefCacheType, &cacheType, 1);
    int type;
    if (cacheType == kCacheType3)
        type = 3;
    else if (cacheType == kCacheType4)
        type = 4;
    else
        type = cacheType == kStandard ? 2 : 0;
    Gfx::CAnimCache::Create(heapSize, cacheSize, type);
    int blends = 0;
    int subsets = 0;
    params->GetInteger(kMaxAnimBlends, &blends, 1);
    params->GetInteger(kMaxAnimSubsets, &subsets, 1);
    Gfx::CBlendChannel::InitBuffers(blends, subsets);
    return true;
}

// 0x00446650: UpdateAnimCache clear | defrag (0x00480DB0(0) / 0x00480F20 on the cache); with neither, the cache's
// information (compiled out). With enable or disable it does nothing (their code is compiled out). Always true.
bool __cdecl CFunc_UpdateAnimCache(CStruct* params, CScript*)
{
    bool disable = params->ContainsComponentNamed(kDisable);
    bool enable = params->ContainsComponentNamed(kEnable);
    bool defrag = params->ContainsComponentNamed(kDefrag);
    bool clear = params->ContainsComponentNamed(kClear);
    if (disable || enable)
        return true;
    if (clear)
        AnimCache()->Flush(false);
    else if (defrag)
        AnimCache()->Defragment();
    else
        PrintAnimCacheInfo(AnimCache());
    return true;
}

// 0x004466F0: FinishRendering (does nothing in this build). Always true.
bool __cdecl CFunc_FinishRendering(CStruct*, CScript*)
{
    FinishRendering();
    return true;
}

// 0x00446700: InitLightManager max_lights=<n> <n> (0x0048EB70). Always true.
bool __cdecl CFunc_InitLightManager(CStruct* params, CScript*)
{
    int maxLights = 0;
    int maxModelLights = 0;
    params->GetInteger(kMaxLights, &maxLights, 1);
    params->GetInteger(kMaxModelLights, &maxModelLights, 1);
    Nx::InitLightManager(static_cast<uint32_t>(maxLights), static_cast<uint32_t>(maxModelLights));
    return true;
}

// 0x00446760: UninitLightManager (0x0048FB60). Always true.
bool __cdecl CFunc_UninitLightManager(CStruct*, CScript*)
{
    Nx::UninitLightManager();
    return true;
}

// 0x00446770: PushWorldLights: the world lights saved on their stack (0x0048EA00). Always true.
bool __cdecl CFunc_PushWorldLights(CStruct*, CScript*)
{
    Nx::PushWorldLights();
    return true;
}

// 0x00446780 (a jump to 0x0048EA40): PopWorldLights: the world lights taken back from their stack; false if it's
// empty.
bool __cdecl CFunc_PopWorldLights(CStruct*, CScript*)
{
    return Nx::PopWorldLights();
}

// 0x00446790: SetScreen [aspect=<f>] [angle=<f>] [letterbox=<n>]: those given are set. Always true.
bool __cdecl CFunc_SetScreen(CStruct* params, CScript*)
{
    float aspect;
    if (params->GetFloat(kAspect, &aspect, 0))
        Nx::SetScreenAspectRatio(aspect);
    float angle;
    if (params->GetFloat(kAngle, &angle, 0))
        Nx::SetScreenFieldOfView(angle);
    int letterbox;
    if (params->GetInteger(kLetterbox, &letterbox, 0))
        Nx::SetLetterbox(letterbox != 0);
    return true;
}

// 0x00446810: AllowBoneLOD allow=<n> (does nothing in this build). Always true.
bool __cdecl CFunc_AllowBoneLOD(CStruct* params, CScript*)
{
    int allow = 1;
    params->GetInteger(kAllow, &allow, 0);
    AllowBoneLOD(allow > 0);
    return true;
}

// 0x00446850: DebugLine start=<vector> end=<vector> [color=<n> (0xFF0000FF)] [frames=<n>] (drawn by compiled-out
// code). Always true.
bool __cdecl CFunc_DebugLine(CStruct* params, CScript*)
{
    Script::VectorValue start{};
    Script::VectorValue end{};
    int colour = static_cast<int>(0xFF0000FF);
    int frames = 0;
    params->GetVector(kStart, &start, 1);
    params->GetVector(kEnd, &end, 1);
    params->GetInteger(kColor, &colour, 0);
    params->GetInteger(kFrames, &frames, 0);
    Gfx::AddDebugLine(reinterpret_cast<const Mth::Vector*>(&start), reinterpret_cast<const Mth::Vector*>(&end),
                      static_cast<uint32_t>(colour), 0, frames);
    return true;
}

// 0x004468E0: GetPausePosByResolution: pos=(50, 240) at a 640-pixel-wide back buffer, (20, 240) otherwise, into
// the script's params. Always true.
bool __cdecl CFunc_GetPausePosByResolution(CStruct*, CScript* script)
{
    float x = Nx::g_presentParameters.BackBufferWidth == 640 ? 50.0f : 20.0f; // the back buffer's width
    script->m_params->AddPair(Crc::GenerateCRCFromString("pos"), x, 240.0f);
    return true;
}

// 0x00446930: KillAllTextureSplats [box={height dist width [offset=<vector>]}] | [texture=<name>]: the splats in
// the box (feet; the box around the offset: width along x, height along y, dist along z, each centred - 0x00491630),
// those of a texture (0x004916F0), or all of them (0x004915A0). Always true.
bool __cdecl CFunc_KillAllTextureSplats(CStruct* params, CScript*)
{
    Mth::BBox box;
    CStruct* boxParams = nullptr;
    uint32_t texture = 0;
    if (!params->GetStructure(kBox, &boxParams, 0)) {
        if (params->GetChecksum(kTexture, &texture, 0))
            Nx::Engine::KillTextureSplatsOfTexture(texture);
        else
            Nx::Engine::KillAllTextureSplats();
        return true;
    }
    float height = 0.0f;
    float dist = 0.0f;
    float width = 0.0f;
    Mth::Vector low = {0.0f, 0.0f, 0.0f, 0.0f};  // the box's corners around the offset (w unused)
    Mth::Vector high = {0.0f, 0.0f, 0.0f, 0.0f};
    Mth::Vector offset = {0.0f, 0.0f, 0.0f, 0.0f}; // (x, y, z uninitialised in the original when not given)
    // Each size in feet to inches; the corners at minus / plus half of it (the low one not above 0, the high one not
    // below 0 - NaN goes through). x87: the low half from the unrounded product, the high one from it rounded to a
    // float.
    auto extent = [](float size, float& lowSide, float& highSide) {
        double inches = static_cast<double>(size) * 12.0;
        double half = inches * -0.5;
        lowSide = 0.0 < half ? 0.0f : static_cast<float>(half);
        half = static_cast<double>(static_cast<float>(inches)) * 0.5;
        highSide = half < 0.0 ? 0.0f : static_cast<float>(half);
    };
    if (boxParams->GetFloat(kHeight, &height, 1))
        extent(height, low.y, high.y);
    if (boxParams->GetFloat(kDist, &dist, 1))
        extent(dist, low.z, high.z);
    if (boxParams->GetFloat(kWidth, &width, 1))
        extent(width, low.x, high.x);
    if (boxParams->GetVector(kOffset, reinterpret_cast<Script::VectorValue*>(&offset), 1))
        offset.FeetToInches();
    box.min = {offset.x + low.x, offset.y + low.y, offset.z + low.z, offset.w + 1.0f};
    box.max = {offset.x + high.x, offset.y + high.y, offset.z + high.z, offset.w + 1.0f};
    Nx::Engine::KillTextureSplatsInBox(reinterpret_cast<const float*>(&box));
    return true;
}

// 0x00446C20: GetGammaValues: the gamma's red, green and blue as percentages (truncated) into the script's params.
// Always true.
bool __cdecl CFunc_GetGammaValues(CStruct*, CScript* script)
{
    float red, green, blue;
    Nx::GetGamma(&red, &green, &blue);
    CStruct* out = script->m_params;
    out->RemoveComponent(kRed);
    out->RemoveComponent(kGreen);
    out->RemoveComponent(kBlue);
    // x87: the product isn't rounded to a float before it's truncated
    out->AddInteger(kRed, static_cast<int>(static_cast<double>(red) * 100.0));
    out->AddInteger(kGreen, static_cast<int>(static_cast<double>(green) * 100.0));
    out->AddInteger(kBlue, static_cast<int>(static_cast<double>(blue) * 100.0));
    return true;
}

// 0x00446CD0: GetDynamicLightModulationFactor ambient | directional=<index>: the two modulation factors
// (0x0048E9B0 / 0x0048E9D0, the second first) into the script's params as mod_value_lo and mod_value_hi. Always
// true.
bool __cdecl CFunc_GetDynamicLightModulationFactor(CStruct* params, CScript* script)
{
    int directional = 0;
    float low, high;
    if (params->ContainsFlag(kAmbient)) {
        low = Nx::GetAmbientLightModulationFactor(true);
        high = Nx::GetAmbientLightModulationFactor(false);
    } else {
        params->GetInteger(kDirectional, &directional, 0);
        low = Nx::GetDiffuseLightModulationFactor(directional, true);
        high = Nx::GetDiffuseLightModulationFactor(directional, false);
    }
    script->m_params->AddFloat(kModValueLo, low);
    script->m_params->AddFloat(kModValueHi, high);
    return true;
}

// 0x00446D70: SetClippingDistances Near=<f> Far=<f>: both viewport cameras' near and far planes (+0xB8 / +0xBC).
// The defaults are the globals 0x006487D4 for Near and 0x006487D0 for Far - 12000 and 12, the wrong way round (as
// in the original). Always true.
bool __cdecl CFunc_SetClippingDistances(CStruct* params, CScript*)
{
    float nearPlane = g_renderDefaultNearPlane;
    float farPlane = g_renderDefaultFarPlane;
    params->GetFloat("Near", &nearPlane, 0);
    params->GetFloat("Far", &farPlane, 0);
    for (int i = 0; i < 2; ++i) {
        if (uint8_t* camera = ViewportCamera(i)) {
            *reinterpret_cast<float*>(camera + 0xB8) = nearPlane;
            *reinterpret_cast<float*>(camera + 0xBC) = farPlane;
        }
    }
    return true;
}

// 0x00446DF0: SetFogDistance distance=<f> (0x00490E00); both viewport cameras are looked up and left alone (their
// use is compiled out). False (and nothing) while distance clipping is on.
bool __cdecl CFunc_SetFogDistance(CStruct* params, CScript*)
{
    if (DistanceClipping())
        return false;
    float distance = 0.0f;
    params->GetFloat("distance", &distance, 0);
    Nx::Engine::SetFogDistance(distance);
    for (int i = 0; i < 2; ++i)
        ViewportCamera(i);
    return true;
}

// 0x00446E50: SetFogColor r g b a (255, 255, 255, 128): the fog colour (0x00490E20, bytes r g b a). While distance
// clipping is on, the clear colour used with it (0x0078733C, 0xAARRGGBB) instead - and false.
bool __cdecl CFunc_SetFogColor(CStruct* params, CScript*)
{
    int r = 0xFF, g = 0xFF, b = 0xFF, a = 0x80;
    params->GetInteger("r", &r, 0);
    params->GetInteger("g", &g, 0);
    params->GetInteger("b", &b, 0);
    params->GetInteger("a", &a, 0);
    if (DistanceClipping()) {
        Nx::g_fogColour = static_cast<uint32_t>(a) << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 |
                                             (b & 0xFF);
        return false;
    }
    Nx::Engine::SetFogColor(PackBytes(r, g, b, a));
    return true;
}

// 0x00446F40: SetRenderMode mode=<n> (0x006FA734). Always true.
bool __cdecl CFunc_SetRenderMode(CStruct* params, CScript*)
{
    int mode = 0;
    params->GetInteger(kMode, &mode, 0);
    Nx::g_renderMode = mode;
    return true;
}

// 0x00446F70: SetWireframeMode mode=<n> (0x006FA730). Always true.
bool __cdecl CFunc_SetWireframeMode(CStruct* params, CScript*)
{
    int mode = 0;
    params->GetInteger(kMode, &mode, 0);
    g_renderMode = mode;
    return true;
}

// 0x00446FA0: SetLightAmbientColor r g b (alpha 128, 0x0048E8F0). Returns what that returns (its AL).
bool __cdecl CFunc_SetLightAmbientColor(CStruct* params, CScript*)
{
    int r = 0, g = 0, b = 0; // (uninitialised in the original)
    params->GetInteger("r", &r, 0);
    params->GetInteger("g", &g, 0);
    params->GetInteger("b", &b, 0);
    return Nx::SetLightAmbientColor(PackBytes(r, g, b, 0x80));
}

// 0x00447010: GetLightDirection index=<n>: the light's direction (0x0048E920) in degrees into the script's params:
// heading (its y) and pitch (its x). Always true.
bool __cdecl CFunc_GetLightDirection(CStruct* params, CScript* script)
{
    int index = 0; // (uninitialised in the original)
    params->GetInteger(kIndex, &index, 0);
    Mth::Vector direction = *Nx::GetLightDirection(index);
    direction.RadiansToDegrees();
    script->m_params->AddFloat(kHeading, direction.y);
    script->m_params->AddFloat(kPitch, direction.x);
    return true;
}

// 0x004470A0: SetLightDiffuseColor index=<n> R G B (alpha 128, 0x0048E930). Returns what that returns (its AL).
bool __cdecl CFunc_SetLightDiffuseColor(CStruct* params, CScript*)
{
    int index = 0, r = 0, g = 0, b = 0; // (uninitialised in the original)
    params->GetInteger(kIndex, &index, 0);
    params->GetInteger(kUpperR, &r, 0);
    params->GetInteger(kUpperG, &g, 0);
    params->GetInteger(kUpperB, &b, 0);
    return Nx::SetLightDiffuseColor(index, PackBytes(r, g, b, 0x80));
}

// 0x00447130: GetLightCurrentColor: the current ambient colour (0x0048E900) and the two directional lights'
// (0x0048E950) into the script's params as ambient_red .. blue_1. Always true.
bool __cdecl CFunc_GetLightCurrentColor(CStruct*, CScript* script)
{
    CStruct* out = script->m_params;
    uint32_t colour;
    auto add = [&](uint32_t red, uint32_t green, uint32_t blue) {
        out->AddInteger(red, static_cast<int>(colour & 0xFF));
        out->AddInteger(green, static_cast<int>(colour >> 8 & 0xFF));
        out->AddInteger(blue, static_cast<int>(colour >> 16 & 0xFF));
    };
    uint32_t buffer;
    colour = *Nx::GetLightAmbientColor(&buffer);
    add(kAmbientRed, kAmbientGreen, kAmbientBlue);
    colour = *Nx::GetLightDiffuseColor(&buffer, 0);
    add(kRed0, kGreen0, kBlue0);
    colour = *Nx::GetLightDiffuseColor(&buffer, 1);
    add(kRed1, kGreen1, kBlue1);
    return true;
}

// 0x00447220: GetBonePosition id=<object> bone=<name>: the bone's position (the skeleton component's 0x00432050)
// into the script's params as x, y, z; whether the bone was found. Without the object, x / y / z are added anyway
// (uninitialised in the original; zero here).
bool __cdecl CFunc_GetBonePosition(CStruct* params, CScript* script)
{
    uint32_t id = 0;
    params->GetChecksum(kId, &id, 1);
    bool found = false;
    Mth::Vector position = {0.0f, 0.0f, 0.0f, 0.0f};
    auto* manager = reinterpret_cast<Obj::CBaseManager*>(Obj::CompositeObjectManager());
    if (Obj::CObject* object = manager->GetObjectByID(id)) {
        uint32_t bone = 0;
        params->GetChecksum(kBone, &bone, 1);
        Obj::CBaseComponent* skeleton = static_cast<Obj::CCompositeObject*>(object)->GetComponent(kSkeleton);
        found = static_cast<Obj::CSkeletonComponent*>(skeleton)->GetBonePosition(bone, &position);
    }
    script->m_params->AddFloat(kX, position.x);
    script->m_params->AddFloat(kY, position.y);
    script->m_params->AddFloat(kZ, position.z);
    return found;
}

} // namespace CFuncs

REPLACE(0x00446460, CFuncs::CFunc_DebugRenderIgnore);
REPLACE(0x004464C0, CFuncs::CFunc_InitAnimCompressTable);
REPLACE(0x00446570, CFuncs::CFunc_InitAnimSystem);
REPLACE(0x00446650, CFuncs::CFunc_UpdateAnimCache);
REPLACE(0x004466F0, CFuncs::CFunc_FinishRendering);
REPLACE(0x00446700, CFuncs::CFunc_InitLightManager);
REPLACE(0x00446760, CFuncs::CFunc_UninitLightManager);
REPLACE(0x00446770, CFuncs::CFunc_PushWorldLights);
REPLACE(0x00446780, CFuncs::CFunc_PopWorldLights);
REPLACE(0x00446790, CFuncs::CFunc_SetScreen);
REPLACE(0x00446810, CFuncs::CFunc_AllowBoneLOD);
REPLACE(0x00446850, CFuncs::CFunc_DebugLine);
REPLACE(0x004468E0, CFuncs::CFunc_GetPausePosByResolution);
REPLACE(0x00446930, CFuncs::CFunc_KillAllTextureSplats);
REPLACE(0x00446C20, CFuncs::CFunc_GetGammaValues);
REPLACE(0x00446CD0, CFuncs::CFunc_GetDynamicLightModulationFactor);
REPLACE(0x00446D70, CFuncs::CFunc_SetClippingDistances);
REPLACE(0x00446DF0, CFuncs::CFunc_SetFogDistance);
REPLACE(0x00446E50, CFuncs::CFunc_SetFogColor);
REPLACE(0x00446F40, CFuncs::CFunc_SetRenderMode);
REPLACE(0x00446F70, CFuncs::CFunc_SetWireframeMode);
REPLACE(0x00446FA0, CFuncs::CFunc_SetLightAmbientColor);
REPLACE(0x00447010, CFuncs::CFunc_GetLightDirection);
REPLACE(0x004470A0, CFuncs::CFunc_SetLightDiffuseColor);
REPLACE(0x00447130, CFuncs::CFunc_GetLightCurrentColor);
REPLACE(0x00447220, CFuncs::CFunc_GetBonePosition);
