// Script commands (CFunctions) and helpers 0x005A9D80-0x005AA92F: textures and a few neighbours (see
// texture_cfuncs.h).

#include "game/gfx/texture_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/gfx/texture_dict.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace CFuncs {

GAME_GLOBAL(bool, g_textureFlagCe441, 0x007CE441);
GAME_GLOBAL(char[128], g_parkName, 0x007CED58);

namespace {

using Script::CComponent;
using Script::CScript;
using Script::CStruct;

// The names of the texture commands' parameters.
constexpr uint32_t kWidth = 0x73E5BAD0;  // "width"
constexpr uint32_t kHeight = 0x0AB21AF0; // "height"
constexpr uint32_t kSrc = 0x9FBBDB72;    // "src"
constexpr uint32_t kNew = 0x941CBBBA;    // "new"
constexpr uint32_t kSetsPaletteFlag = 0xC164842A; // (name unknown) brightness / HSV: an integer, 1 = the flag on

// The texture dictionaries (Nx): the level's / scripts' (0x00700F1C) and the particles' (0x00700F20).
void* Textures() { return GameVariable<void*>(0x00700F1C); }
void* ParticleTextures() { return GameVariable<void*>(0x00700F20); }

// A dictionary's calls (no source yet).
void* LoadTextureFile(void* dictionary, const char* file, bool flag1, bool allocateVram, bool flag3)
{
    return static_cast<Nx::CTexDict*>(dictionary)->LoadTexture(file, flag1, allocateVram, flag3);
}
void* GetTexture(void* dictionary, uint32_t checksum)
{
    return static_cast<Nx::CTexDict*>(dictionary)->GetTexture(checksum);
}
void* GetTextureByName(void* dictionary, const char* file)
{
    return static_cast<Nx::CTexDict*>(dictionary)->GetTextureByName(file);
}
void UnloadTextureFrom(void* dictionary, void* texture)
{
    static_cast<Nx::CTexDict*>(dictionary)->UnloadTexture(static_cast<Nx::CTexture*>(texture));
}

// A texture's own calls (thunks into its vtable).
template <typename Fn, typename... Args>
void TextureCall(uint32_t slot, void* texture, Args... args)
{
    // (the texture's virtual slot `slot`: the original goes through a jump stub for each)
    reinterpret_cast<Fn>((*static_cast<void***>(texture))[slot])(texture, args...);
}

// A component's raw value.
uint32_t RawValue(const CComponent* component) { return component->raw; }

} // namespace

// 0x005A9D80
bool __cdecl CFunc_SceAltPt(CStruct*, CScript*)
{
    g_textureFlagCe441 = true;
    return true;
}

// 0x005A9D90
uint32_t __cdecl FindNameOfValue(CStruct* structure, uint32_t value, int depth)
{
    if (depth > 10)
        return 0;
    uint32_t found = 0;
    for (CComponent* component = structure->GetNextComponent(nullptr); component;) {
        // (the global structure the component's value names, looked up for every component)
        CStruct* named = Script::GetStructure(RawValue(component));
        CComponent* next = structure->GetNextComponent(component);
        if (!found) {
            if (component->Type() == Script::ESYMBOLTYPE_STRUCTURE)
                found = FindNameOfValue(component->structure, value, depth + 1);
            else if (named)
                found = FindNameOfValue(named, value, depth + 1);
        }
        if (component->Type() == Script::ESYMBOLTYPE_NAME && component->checksum == value)
            found = component->name;
        component = next;
    }
    return found;
}

// 0x005A9E30
uint32_t FindNameOfInteger(CStruct* structure, uint32_t value)
{
    uint32_t found = 0;
    for (CComponent* component = structure->GetNextComponent(nullptr); component;) {
        CComponent* next = structure->GetNextComponent(component);
        if (component->Type() == Script::ESYMBOLTYPE_INTEGER && component->raw == value) {
            Dbg::Printf(reinterpret_cast<const char*>(0x00657ADC), value); // "s_find_cat_in_mapping found %i"
            found = component->name;
        }
        component = next;
    }
    return found;
}

namespace {
uint32_t __cdecl FindNameOfIntegerRegisters(CStruct* structure, uint32_t value)
{
    return FindNameOfInteger(structure, value);
}
} // namespace

// 0x005A9E30: the structure in EBX, the value on the stack (the caller pops it).
__declspec(naked) void FindNameOfIntegerStub()
{
    __asm {
        push dword ptr [esp + 4]
        push ebx
        call FindNameOfIntegerRegisters
        add esp, 8
        ret
    }
}

// 0x005A9E80
float __cdecl WrapAngle(float degrees)
{
    double angle = degrees; // (kept in an x87 register, double precision)
    while (angle < 0.0)
        angle += 360.0;
    while (angle > 360.0)
        angle -= 360.0;
    return static_cast<float>(angle);
}

// 0x005A9ED0
bool __cdecl CFunc_CompositeObjectExists(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0xA1DC81F9u, &id, 1); // name (asserted: the id is used as it is)
    return Obj::FindObject(id) != nullptr;
}

// 0x005A9F00
bool __cdecl CFunc_LoadTexture(CStruct* params, CScript*)
{
    const char* file;
    params->GetString(0u, &file, 0);
    bool allocateVram = !params->ContainsFlag(0x3955FF2Eu); // no_vram_alloc
    return LoadTextureFile(Textures(), file, true, allocateVram, true) != nullptr;
}

bool TextureLoaded(const char* file) { return GetTextureByName(Textures(), file) != nullptr; }

// 0x005A9F60
bool __cdecl CFunc_CreateTexture(CStruct* params, CScript*)
{
    uint32_t name;
    params->GetChecksum(0u, &name, 0);
    int width;
    int height;
    int bitDepth;
    int paletteBitDepth = 0; // (not initialised in the original: only read when needed)
    bool valid = params->GetInteger(kWidth, &width, 0);
    valid = params->GetInteger(kHeight, &height, 0) && valid;
    valid = params->GetInteger(0xBA0F2AC5u, &bitDepth, 0) && valid; // bitdepth
    if (bitDepth <= 8 && !params->GetInteger(0x51533ECBu, &paletteBitDepth, 0)) // palette_bitdepth
        return false;
    if (!valid)
        return false;
    return static_cast<Nx::CTexDict*>(Textures())->CreateTexture(name, width, height, bitDepth, paletteBitDepth, 1, true) !=
           nullptr;
}

// 0x005AA040
bool __cdecl CFunc_UnloadTexture(CStruct* params, CScript*)
{
    void* texture = nullptr;
    const char* file;
    uint32_t checksum;
    if (params->GetString(0u, &file, 0))
        texture = GetTextureByName(Textures(), file);
    else if (params->GetChecksum(0u, &checksum, 0))
        texture = GetTexture(Textures(), checksum);
    if (texture) {
        UnloadTextureFrom(Textures(), texture);
        return true;
    }
    params->ContainsFlag(0x3D92465Eu); // dont_assert (the assert is compiled out)
    return true;
}

// 0x005AA0C0
bool __cdecl CFunc_Generate32BitImage(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        bool renderable = false;
        bool storeOriginal = false;
        int value;
        if (params->GetInteger(0xA5D7CFAAu, &value, 0) && value == 1) // renderable
            renderable = true;
        if (params->GetInteger(0xEEFAF080u, &value, 0) && value == 1) // store_original
            storeOriginal = true;
        TextureCall<void(__thiscall*)(void*, bool, bool)>(8, texture, renderable, storeOriginal);
    }
    return true;
}

// 0x005AA160
bool __cdecl CFunc_Put32BitImageIntoTexture(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        bool paletteGen = false;
        int value = 0;
        params->GetInteger(0xC313FDFFu, &value, 0); // palette_gen
        if (value == 1)
            paletteGen = true;
        TextureCall<void(__thiscall*)(void*, bool, bool)>(9, texture, paletteGen, false);
    }
    return true;
}

// 0x005AA1E0
bool __cdecl CFunc_FlipTexture(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    int horizontal = 0;
    int vertical = 0;
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        params->GetInteger(0xA04027C9u, &horizontal, 0); // horiz
        params->GetInteger(0x94927839u, &vertical, 0);   // vert
        if (horizontal)
            TextureCall<void(__thiscall*)(void*)>(13, texture);
        if (vertical)
            TextureCall<void(__thiscall*)(void*)>(14, texture);
    }
    return true;
}

// 0x005AA270
bool __cdecl CFunc_ResizeTextureInto(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    uint32_t source; // (not initialised without src=, as in the original)
    params->GetChecksum(kSrc, &source, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        if (void* sourceTexture = GetTexture(Textures(), source))
            TextureCall<void(__thiscall*)(void*, void*, int)>(15, texture, sourceTexture, 0);
    }
    return true;
}

// 0x005AA2E0
bool __cdecl CFunc_ScaleTexture(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    float scale; // (not initialised without scale=, as in the original)
    params->GetFloat(0x13B9DA7Bu, &scale, 0);
    if (void* texture = GetTexture(Textures(), id))
        TextureCall<void(__thiscall*)(void*, float, float)>(16, texture, scale, scale);
    return true;
}

// 0x005AA340
bool __cdecl CFunc_AdjustTextureRegion(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        int x = 0;
        int y = 0;
        // (its width and height: slots 33 and 34, through the jumps 0x0049F3C0 / 0x0049F3D0)
        auto** slots = *static_cast<void***>(texture);
        int width = reinterpret_cast<uint16_t(__thiscall*)(void*)>(slots[33])(texture);
        int height = reinterpret_cast<uint16_t(__thiscall*)(void*)>(slots[34])(texture);
        int value5 = 0;
        int value6 = 0;
        int value7 = 0;
        params->GetInteger(0x0FA8972Eu, &x, 0); // xpos
        params->GetInteger(0xB714F04Bu, &y, 0); // ypos
        params->GetInteger(kWidth, &width, 0);
        params->GetInteger(kHeight, &height, 0);
        params->GetInteger(0xE679982Eu, &value5, 0); // (names unknown)
        params->GetInteger(0x3D0F162Au, &value6, 0);
        params->GetInteger(0x39C54BDEu, &value7, 0);
        TextureCall<void(__thiscall*)(void*, int, int, int, int, int, int, int)>(20, texture, x, y, width,
                                                                                 height, value5, value6, value7);
    }
    return true;
}

// 0x005AA460
bool __cdecl CFunc_PullTextureToEdge(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        int edge = 0;
        int axis = 0;
        int pixels = 0;
        params->GetInteger(0x485A0CDBu, &edge, 0);   // (name unknown)
        params->GetInteger(0x7AF07905u, &axis, 0);   // Axis
        params->GetInteger(0x88CF948Cu, &pixels, 0); // num_pixels
        TextureCall<void(__thiscall*)(void*, int, int, int)>(21, texture, edge, axis, pixels);
    }
    return true;
}

// 0x005AA500
bool __cdecl CFunc_AdjustTextureBrightness(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        float brightness = 1.0f;
        int value = 0;
        bool flag = false;
        params->GetFloat(0x2689291Cu, &brightness, 1); // Brightness
        params->GetInteger(kSetsPaletteFlag, &value, 0);
        if (value == 1)
            flag = true;
        TextureCall<void(__thiscall*)(void*, float, bool)>(23, texture, brightness, flag);
    }
    return true;
}

// 0x005AA590
bool __cdecl CFunc_AdjustTextureHSV(CStruct* params, CScript*)
{
    uint32_t id;
    params->GetChecksum(0u, &id, 0);
    void* texture = GetTexture(Textures(), id);
    if (texture) {
        float hue = 0.0f;
        float saturation = 1.0f;
        float value = 1.0f;
        int flagValue = 0;
        bool flag = false;
        params->GetFloat(0x6E94F918u, &hue, 0);        // h
        params->GetFloat(0xE4F130F4u, &saturation, 0); // s
        params->GetFloat(0x949BC47Bu, &value, 0);      // v
        params->GetInteger(kSetsPaletteFlag, &flagValue, 0);
        if (flagValue == 1)
            flag = true;
        TextureCall<void(__thiscall*)(void*, float, float, float, bool)>(24, texture, hue, saturation, value,
                                                                         flag);
        TextureCall<void(__thiscall*)(void*, bool, bool)>(9, texture, false, false);
    }
    return true;
}

// 0x005AA670
bool __cdecl CFunc_CopyTexture(CStruct* params, CScript*)
{
    uint32_t source;
    params->GetChecksum(kSrc, &source, 0);
    void* texture = GetTexture(Textures(), source);
    uint32_t copy;
    if (texture && params->GetChecksum(kNew, &copy, 0))
        static_cast<Nx::CTexDict*>(Textures())->CopyTexture(copy, static_cast<Nx::CTexture*>(texture));
    return true;
}

// 0x005AA6E0
bool __cdecl CFunc_CombineTextures(CStruct* params, CScript*)
{
    uint32_t source; // (src and top are not initialised when missing, as in the original)
    uint32_t top;
    uint32_t combined = 0;
    bool paletteGen = true;
    params->GetChecksum(kSrc, &source, 0);
    params->GetChecksum(0xE126E035u, &top, 0); // top
    params->GetChecksum(kNew, &combined, 0);
    if (params->ContainsFlag(0x5905256Bu)) // no_palette_gen
        paletteGen = false;
    void* sourceTexture = GetTexture(Textures(), source);
    if (!sourceTexture)
        return true;
    void* topTexture = GetTexture(Textures(), top);
    if (!topTexture)
        return true;
    if (combined) {
        // a new texture of the two
        static_cast<Nx::CTexDict*>(Textures())->CopyTextureWithOverlay(
            combined, static_cast<Nx::CTexture*>(sourceTexture), static_cast<Nx::CTexture*>(topTexture), paletteGen);
        return true;
    }
    TextureCall<void(__thiscall*)(void*, void*, bool)>(41, sourceTexture, topTexture, paletteGen);
    return true;
}

// 0x005AA7B0
bool __cdecl CFunc_LoadParticleTexture(CStruct* params, CScript*)
{
    const char* file;
    params->GetString(0u, &file, 0);
    if (file[0] == '\0')
        return false;
    void* texture = GetTextureByName(ParticleTextures(), file);
    bool permanent = params->ContainsFlag(0xD5928F25u); // perm
    if (!texture)
        texture = LoadTextureFile(ParticleTextures(), file, true, true, permanent);
    return texture != nullptr;
}

// 0x005AA830
bool __cdecl CFunc_UnloadParticleTexture(CStruct* params, CScript*)
{
    const char* file;
    params->GetString(0u, &file, 0);
    if (void* texture = GetTextureByName(ParticleTextures(), file)) {
        TextureCall<void(__thiscall*)(void*)>(32, texture); // (its vtable +0x80 first)
        UnloadTextureFrom(ParticleTextures(), texture);
    }
    return true;
}

// 0x005AA880
bool __cdecl CFunc_LoadSFPTexture(CStruct* params, CScript*)
{
    const char* file;
    params->GetString(0u, &file, 0);
    return LoadTextureFile(ParticleTextures(), file, false, true, true) != nullptr;
}

// 0x005AA8B0
bool __cdecl CFunc_UnloadSFPTexture(CStruct* params, CScript*)
{
    const char* file;
    params->GetString(0u, &file, 0);
    if (void* texture = GetTextureByName(ParticleTextures(), file))
        UnloadTextureFrom(ParticleTextures(), texture);
    return true;
}

// 0x005AA8F0
bool __cdecl CFunc_SetParkName(CStruct* params, CScript*)
{
    const char* name = reinterpret_cast<const char*>(0x006469B4); // ""
    params->GetString(0u, &name, 0);
    char* parkName = g_parkName;
    while ((*parkName++ = *name++) != '\0') { // (no length limit, as in the original)
    }
    return false;
}

} // namespace CFuncs

REPLACE(0x005A9D80, CFuncs::CFunc_SceAltPt);
REPLACE(0x005A9D90, CFuncs::FindNameOfValue);
REPLACE_UNCHECKED(0x005A9E30, CFuncs::FindNameOfIntegerStub); // the structure in EBX, the value on the stack
REPLACE(0x005A9E80, CFuncs::WrapAngle);
REPLACE(0x005A9ED0, CFuncs::CFunc_CompositeObjectExists);
REPLACE(0x005A9F00, CFuncs::CFunc_LoadTexture);
REPLACE(0x005A9F60, CFuncs::CFunc_CreateTexture);
REPLACE(0x005AA040, CFuncs::CFunc_UnloadTexture);
REPLACE(0x005AA0C0, CFuncs::CFunc_Generate32BitImage);
REPLACE(0x005AA160, CFuncs::CFunc_Put32BitImageIntoTexture);
REPLACE(0x005AA1E0, CFuncs::CFunc_FlipTexture);
REPLACE(0x005AA270, CFuncs::CFunc_ResizeTextureInto);
REPLACE(0x005AA2E0, CFuncs::CFunc_ScaleTexture);
REPLACE(0x005AA340, CFuncs::CFunc_AdjustTextureRegion);
REPLACE(0x005AA460, CFuncs::CFunc_PullTextureToEdge);
REPLACE(0x005AA500, CFuncs::CFunc_AdjustTextureBrightness);
REPLACE(0x005AA590, CFuncs::CFunc_AdjustTextureHSV);
REPLACE(0x005AA670, CFuncs::CFunc_CopyTexture);
REPLACE(0x005AA6E0, CFuncs::CFunc_CombineTextures);
REPLACE(0x005AA7B0, CFuncs::CFunc_LoadParticleTexture);
REPLACE(0x005AA830, CFuncs::CFunc_UnloadParticleTexture);
REPLACE(0x005AA880, CFuncs::CFunc_LoadSFPTexture);
REPLACE(0x005AA8B0, CFuncs::CFunc_UnloadSFPTexture);
REPLACE(0x005AA8F0, CFuncs::CFunc_SetParkName);
