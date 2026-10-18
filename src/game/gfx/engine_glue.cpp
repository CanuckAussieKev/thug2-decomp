// The engine's platform calls: see engine_glue.h.

#include "game/gfx/engine_glue.h"

#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

// 0x004B2270
void __cdecl ToggleMemView()
{
    GameVariable<uint32_t>(0x00701588) ^= 1;
}

// 0x004B2280 (the device 0x007D69E0's BeginScene, vtable slot 41; device lost: 0x00786D04)
void __cdecl StartRender()
{
    if (GameVariable<bool>(0x00786D04))
        return;
    void* device = GameVariable<void*>(0x007D69E0);
    reinterpret_cast<long(__stdcall*)(void*)>((*static_cast<void***>(device))[41])(device);
}

// 0x004B22A0
void __cdecl DeleteObject(void* object)
{
    if (object)
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(object))[0])(object, 1);
}

// 0x004B22B0
void __cdecl SetScreenClip(const void* rect)
{
    GameFunction<void(__cdecl*)(const void*)>(0x004D3CA0)(rect);
}

// 0x004B22C0
void __cdecl AddDebugShape(int type, const Mth::Vector* points, uint32_t colour)
{
    if (type == 4)
        GameFunction<void(__cdecl*)(const Mth::Vector*, const Mth::Vector*, const Mth::Vector*, const Mth::Vector*,
                                    uint32_t)>(0x004D8D70)(points, points + 1, points + 2, points + 3, colour);
}

// 0x004B22F0
void __cdecl FlagDebugLines(uint32_t id, bool flag)
{
    GameFunction<void(__cdecl*)(uint32_t, bool)>(0x004D8C90)(id, flag);
}

// 0x004B2300
void __cdecl ClearDebugLines()
{
    GameFunction<void(__cdecl*)()>(0x004D8CC0)();
}

// 0x004B2310
int __cdecl SphereVisible(const Mth::Vector* centre, float radius)
{
    return GameFunction<int(__cdecl*)(const Mth::Vector*, float)>(0x004DB570)(centre, radius);
}

// 0x004B2320
const char* __cdecl PlatformExtension()
{
    return "Xbx";
}

} // namespace Nx

REPLACE(0x004B2270, Nx::ToggleMemView);
REPLACE(0x004B2280, Nx::StartRender);
REPLACE(0x004B22A0, Nx::DeleteObject);
REPLACE_SOURCE_ONLY(0x004B22B0, Nx::SetScreenClip); // a jump to 0x004D3CA0
REPLACE(0x004B22C0, Nx::AddDebugShape);
REPLACE_SOURCE_ONLY(0x004B22F0, Nx::FlagDebugLines);  // a jump to 0x004D8C90
REPLACE_SOURCE_ONLY(0x004B2300, Nx::ClearDebugLines); // a jump to 0x004D8CC0
REPLACE_SOURCE_ONLY(0x004B2310, Nx::SphereVisible);   // a jump to 0x004DB570
REPLACE(0x004B2320, Nx::PlatformExtension);
