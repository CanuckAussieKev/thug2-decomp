// Self-tests for the game functions rewritten in src/game/gfx/camera.cpp (Gfx::Camera's setters and getters) and
// src/game/gfx/scene.cpp (the LoadScene script command): originals against ours; and for the data overlay's file names
// and the folders it searches.

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/screen.h"
#include "game/obj/movie_manager.h"
#include "game/obj/object.h"
#include "game/core/str.h"
#include "game/front/data_overlay.h"
#include "game/gfx/camera.h"
#include "game/gfx/scene.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Gfx::Camera;
using Script::CStruct;

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

std::vector<Saved> g_menulvl_saved;
void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_menulvl_saved);
    g_menulvl_saved.clear();
}

// --- Gfx::Camera -------------------------------------------------------------------------------------------------

constexpr size_t kCameraSize = 0xC4;
float g_cam_factor = 1.0f, g_cam_defaultAngle = 72.0f;
float g_cam_scriptRadians;
bool g_cam_scriptHasFloat;
uint32_t g_cam_base; // the camera of the run (recorded as offsets from it)

uint32_t CamRel(uint32_t p) { return p - g_cam_base < 0x1000 ? 0xCA000000u + (p - g_cam_base) : p; }

// The screen's angle factor and the cameras' default angle return floats (ST0): C++ stand-ins jumped to.
float __cdecl StandInAngleFactor()
{
    Record({0x7C0});
    return g_cam_factor;
}
float __cdecl StandInScreenAngle()
{
    Record({0x7B0});
    return g_cam_defaultAngle;
}

void InstallCameraStandIns(StandIns& s)
{
    for (uint32_t at : {0x004A07C0u, AddressOf(&Nx::GetScreenAngleFactor)})
        s.Bytes(at, Branch(0xE9, at, reinterpret_cast<void*>(&StandInAngleFactor)));
    for (uint32_t at : {0x004A07B0u, AddressOf(&Nx::GetScreenAngle)})
        s.Bytes(at, Branch(0xE9, at, reinterpret_cast<void*>(&StandInScreenAngle)));
    s.At({0x00460B80, AddressOf(&Obj::CObject::Construct)}, 0, [](Regs& r) {
        Record({0xB80, CamRel(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x3C, 0x4C);
        return r.ecx;
    });
    s.At({0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, [](Regs& r) {
        Record({0x5D0, CamRel(r.ecx), *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xBA0, r.ecx, CamRel(r.args[0])});
        return 0u;
    });
    s.At({0x004766D0,
          AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(&Script::CStruct::GetFloat))},
         12, [](Regs& r) {
             Record({0x6D0, r.ecx, r.args[0], r.args[2] & 0xFF});
             if (!g_cam_scriptHasFloat)
                 return 0u;
             std::memcpy(reinterpret_cast<void*>(r.args[1]), &g_cam_scriptRadians, 4);
             return 1u;
         });
    s.At({0x004608F0, AddressOf(static_cast<bool (Obj::CObject::*)(uint32_t, Script::CStruct*, Script::CScript*)>(
                          &Obj::CObject::CallMemberFunction))},
         12, [](Regs& r) {
             Record({0x8F0, CamRel(r.ecx), r.args[0], r.args[1], r.args[2]});
             return 0x5Au;
         });
}

void FillCamera(uint8_t* camera, uint32_t seed)
{
    for (size_t i = 0; i < kCameraSize; i += 4) {
        float f = static_cast<float>(static_cast<int>((seed * 2654435761u + i * 40503u) % 2000u) - 1000) * 0.37f;
        std::memcpy(camera + i, &f, 4);
    }
    float fov = 50.0f + static_cast<float>(seed % 40u);
    std::memcpy(camera + 0xAC, &fov, 4);
    float c0 = 1.0f + 0.125f * static_cast<float>(seed % 3u); // (UpdateAdjustedHFOV divides by +0xC0)
    std::memcpy(camera + 0xC0, &c0, 4);
}

// Every method on one camera (the original's or ours): the trace, with the camera's words after the steps.
std::vector<uint32_t> RunCamera(bool original, uint32_t seed)
{
    alignas(16) static uint8_t memory[kCameraSize];
    FillCamera(memory, seed);
    g_cam_base = reinterpret_cast<uint32_t>(memory);
    auto* camera = reinterpret_cast<Camera*>(memory);
    g_trace.clear();
    auto snapshot = [&] {
        for (size_t i = 0; i < kCameraSize; i += 4) {
            uint32_t w;
            std::memcpy(&w, memory + i, 4);
            g_trace.push_back(w);
        }
    };
    auto rel = [&](const void* p) { return CamRel(reinterpret_cast<uint32_t>(p)); };
    using Fn0 = void(__thiscall*)(Camera*);
    // the factor, the default angle, the distance clipping vary with the seed
    g_cam_factor = 1.0f + 0.3333333f * static_cast<float>(seed % 3u);
    g_cam_defaultAngle = 60.0f + 4.5f * static_cast<float>(seed);
    GameVariable<bool>(0x007D643A) = (seed & 1) != 0;
    const int distances[] = {100, -5, 0x7FFFFFFF, 595, 0, 33};
    GameVariable<int>(0x007D6444) = distances[seed % 6u];

    if (original)
        Record({rel(GameFunction<Camera*(__thiscall*)(Camera*)>(0x00485BA0)(camera))});
    else
        Record({rel(camera->Construct())});
    snapshot();
    camera->m_unknownC0 = 1.0f + 0.125f * static_cast<float>(seed % 3u); // (the divisor of the angle's tangent)
    Record({rel(original ? GameFunction<Mth::Vector*(__thiscall*)(Camera*)>(0x00485A00)(camera) : camera->GetPos())});
    Record({rel(original ? GameFunction<Mth::Matrix*(__thiscall*)(Camera*)>(0x00485A10)(camera) : camera->GetMatrix())});

    Mth::Vector pos = {12.5f * seed, -3.75f, 1000.0f / seed, seed & 1 ? 1.0f : 0.5f};
    Mth::Vector pos2 = {-7.0f, 8.0f * seed, 9.5f, 1.0f};
    Mth::Matrix m;
    for (int r = 0; r < 4; ++r)
        m.row[r] = {0.1f * r + seed, -0.2f * r, 0.3f * seed, r == 3 ? 1.0f : 0.0f};
    float fov = 40.0f + 9.0f * static_cast<float>(seed);
    if (original) {
        GameFunction<void(__thiscall*)(Camera*, const Mth::Vector*)>(0x00485A70)(camera, &pos);
        GameFunction<Fn0>(0x00485A50)(camera);
        GameFunction<void(__thiscall*)(Camera*, const Mth::Vector*)>(0x00485A70)(camera, &pos2);
        Record({rel(GameFunction<Mth::Matrix*(__thiscall*)(Camera*, const Mth::Matrix*)>(0x00485A90)(camera, &m))});
        snapshot();
        GameFunction<void(__thiscall*)(Camera*, float)>(0x00485B20)(camera, fov);
        snapshot();
        Record({Bits(GameFunction<float(__thiscall*)(Camera*)>(0x00485B10)(camera))});
    } else {
        camera->SetPos(pos);
        camera->StoreOldPos();
        camera->SetPos(pos2);
        Record({rel(&camera->SetMatrix(m))});
        snapshot();
        camera->SetHFOV(fov);
        snapshot();
        Record({Bits(camera->GetAdjustedHFOV())});
    }
    // UpdateAdjustedHFOV over angles
    for (float angle : {1.0f, 45.0f, 72.0f, 90.0f, 119.5f, 170.0f}) {
        camera->m_hFov = angle + 0.01f * static_cast<float>(seed);
        if (original)
            GameFunction<Fn0>(0x00485AA0)(camera);
        else
            camera->UpdateAdjustedHFOV();
        Record({Bits(camera->m_adjustedHFov), Bits(camera->m_unknownB4)});
        camera->m_hFov = angle;
        Record({Bits(original ? GameFunction<float(__thiscall*)(Camera*)>(0x00485B10)(camera)
                              : camera->GetAdjustedHFOV())});
    }
    // the member function: the angle in radians, without one, another name
    using Member = uint32_t(__thiscall*)(Camera*, uint32_t, void*, void*);
    auto call = [&](uint32_t name, void* params, void* script) {
        uint32_t result = original ? GameFunction<Member>(0x00485B40)(camera, name, params, script)
                                   : camera->CallMemberFunction(name, static_cast<Script::CStruct*>(params),
                                                                static_cast<Script::CScript*>(script));
        Record({0x2E5, result & 0xFF});
        snapshot();
    };
    g_cam_scriptHasFloat = true;
    g_cam_scriptRadians = 0.25f * static_cast<float>(seed);
    call(0x44EA2B6D, reinterpret_cast<void*>(0x5150), reinterpret_cast<void*>(0x5C21));
    g_cam_scriptHasFloat = false;
    call(0x44EA2B6D, reinterpret_cast<void*>(0x3F400000), reinterpret_cast<void*>(0x5C21));
    call(0x12345678, reinterpret_cast<void*>(0x5150), reinterpret_cast<void*>(0x5C21));
    // the destructors (the vtable spoiled first: the body puts it back)
    *reinterpret_cast<uint32_t*>(memory) = 0x11111111;
    if (original) {
        GameFunction<Fn0>(0x004859F0)(camera);
        snapshot();
        Record({rel(GameFunction<Camera*(__thiscall*)(Camera*, uint32_t)>(0x00485A20)(camera, 0x100))});
        Record({rel(GameFunction<Camera*(__thiscall*)(Camera*, uint32_t)>(0x00485A20)(camera, 0x101))});
    } else {
        camera->DestroyBody();
        snapshot();
        Record({rel(camera->ScalarDelete(0))});
        Record({rel(camera->ScalarDelete(1))});
    }
    snapshot();
    return g_trace;
}

void TestCameraMethods()
{
    g_menulvl_saved = {{0x007D643A, 1, {}}, {0x007D6444, 4, {}}};
    SaveAll(g_menulvl_saved);
    auto* s = new StandIns;
    InstallCameraStandIns(*s);
    bool ok = true;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        std::vector<uint32_t> expected = RunCamera(true, seed);
        std::vector<uint32_t> actual = RunCamera(false, seed);
        if (expected != actual) {
            ok = false;
            Log("  camera seed %u:", seed);
            Compare(expected, actual, "Gfx::Camera");
        }
    }
    Check(ok, "Gfx::Camera: the constructor, destructors, member function, getters, setters, the adjusted angle");
    Cleanup();
}

// --- LoadScene ---------------------------------------------------------------------------------------------------

// A model of the structures the command reads and builds: strings and flags by structure.
struct ModelStruct {
    std::map<uint32_t, std::string> strings;
    std::set<uint32_t> flags;
};
std::map<uint32_t, ModelStruct> g_ls_structs;
std::map<uint32_t, uint32_t> g_ls_roles; // structure -> 1, 2... in the order made ('P' the test's)
uint32_t g_ls_params;
uint32_t g_ls_texDicts;
uint32_t g_ls_inits, g_ls_clears;
// The engine loads: name, texture path, the four flags.
struct Load {
    std::string name, texture;
    uint32_t texDict;
    bool addSuperSectors, sky, dictionary, net;
};
std::vector<Load> g_ls_loads;
std::map<uint32_t, std::string> g_ls_texturePaths; // dictionary -> its path

uint32_t Role(uint32_t structure)
{
    if (structure == g_ls_params)
        return 0x50;
    auto it = g_ls_roles.find(structure);
    return it == g_ls_roles.end() ? 0xBAD : it->second;
}

bool CaselessContains(const char* haystack, const char* needle)
{
    for (; *haystack; ++haystack) {
        size_t i = 0;
        while (needle[i] && haystack[i] &&
               std::tolower(static_cast<uint8_t>(haystack[i])) == std::tolower(static_cast<uint8_t>(needle[i])))
            ++i;
        if (!needle[i])
            return true;
    }
    return false;
}

void InstallLoadSceneStandIns(StandIns& s)
{
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0xB40, Role(r.ecx), r.args[0]});
        return g_ls_structs[r.ecx].flags.count(r.args[0]) ? 1u : 0u;
    });
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             Record({0xC60, Role(r.ecx), r.args[0], r.args[2] & 0xFF});
             auto& strings = g_ls_structs[r.ecx].strings;
             auto it = strings.find(r.args[0]);
             if (it == strings.end())
                 return 0u;
             *reinterpret_cast<const char**>(r.args[1]) = it->second.c_str();
             return 1u;
         });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        g_ls_roles[r.ecx] = static_cast<uint32_t>(++g_ls_inits);
        g_ls_structs[r.ecx] = {};
        Record({0x410, Role(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0x1717;
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0;
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        ++g_ls_clears;
        Record({0x130, Role(r.ecx)});
        g_ls_structs[r.ecx] = {};
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0xD40, Role(r.ecx), r.args[0], r.args[1]});
             if (r.args[0] == 0)
                 g_ls_structs[r.ecx].flags.insert(r.args[1]);
             return 0u;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             const char* text = reinterpret_cast<const char*>(r.args[1]);
             Record({0x9D0, Role(r.ecx), r.args[0], Hash(text)});
             g_ls_structs[r.ecx].strings[r.args[0]] = text;
             return 0u;
         });
    s.At({0x00472E60, AddressOf(&Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        uint32_t hash = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0xE60, hash});
        return hash ^ 0x5A5A5A5Au;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Record({0xC30, Hash(format), std::strstr(format, "%s") ? Hash(reinterpret_cast<const char*>(r.args[1])) : 0u});
        return 0xFFFFFFFFu;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // the game's sprintf (the original's: not recorded)
    s.At({0x00401FF0, AddressOf(&Str::FindCaseless)}, 0, [](Regs& r) {
        const char* haystack = reinterpret_cast<const char*>(r.args[0]);
        const char* needle = reinterpret_cast<const char*>(r.args[1]);
        Record({0xFF0, Hash(haystack), Hash(needle)});
        return CaselessContains(haystack, needle) ? r.args[0] : 0u;
    });
    s.At({0x0049F0C0}, 0, [](Regs& r) {
        const char* path = reinterpret_cast<const char*>(r.args[0]);
        Record({0xF0C0, Hash(path), r.args[1], r.args[2], r.args[3], r.args[4]});
        uint32_t texDict = 0x7E000000u + ++g_ls_texDicts * 0x10;
        g_ls_texturePaths[texDict] = path;
        return texDict;
    });
    s.At({0x0048CCB0}, 0, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0xCCB0, Hash(name), r.args[1], r.args[2] & 0xFF, r.args[3] & 0xFF, r.args[4] & 0xFF,
                r.args[5] & 0xFF});
        g_ls_loads.push_back({name, g_ls_texturePaths[r.args[1]], r.args[1], (r.args[2] & 0xFF) != 0,
                              (r.args[3] & 0xFF) != 0, (r.args[4] & 0xFF) != 0, (r.args[5] & 0xFF) != 0});
        return 0x7D000000u;
    });
}

constexpr uint32_t kScene = 0x26861025, kIsDictionary = 0x3D559E7D, kIsSky = 0xB5BD28D8, kIsNet = 0x417149C8,
                   kNoSuperSectors = 0x8943DBD8;

// One LoadScene call by the original or ours on the test's parameters: the trace.
std::vector<uint32_t> RunLoadScene(bool original, const char* name, uint32_t flags)
{
    g_trace.clear();
    g_ls_structs.clear();
    g_ls_roles.clear();
    g_ls_loads.clear();
    g_ls_texturePaths.clear();
    g_ls_inits = g_ls_clears = g_ls_texDicts = 0;
    alignas(4) static uint8_t params[8];
    g_ls_params = reinterpret_cast<uint32_t>(params);
    ModelStruct& model = g_ls_structs[g_ls_params];
    if (name)
        model.strings[kScene] = name;
    const uint32_t all[] = {kIsDictionary, kIsSky, kIsNet, kNoSuperSectors};
    for (int i = 0; i < 4; ++i)
        if (flags & (1u << i))
            model.flags.insert(all[i]);
    uint32_t result;
    if (original)
        result = GameFunction<uint32_t(__cdecl*)(void*, void*)>(0x00445130)(params, nullptr) & 0xFF;
    else
        result = Nx::CFunc_LoadScene(reinterpret_cast<CStruct*>(params), nullptr) ? 1u : 0u;
    Record({0x2E5, result});
    return g_trace;
}

void TestLoadScene()
{
    auto* s = new StandIns;
    InstallLoadSceneStandIns(*s);
    const char* names[] = {"mainmenu", "MainMenu", "LA", "AU_Sky", "la_SKY_x", "Sk5ed_shell", "skyline",
                           "thps4_skateshop"};
    bool ok = true;
    for (const char* name : names) {
        for (uint32_t flags = 0; flags < 16; ++flags) {
            std::vector<uint32_t> expected = RunLoadScene(true, name, flags);
            std::vector<uint32_t> actual = RunLoadScene(false, name, flags);
            if (expected != actual) {
                ok = false;
                Log("  LoadScene %s flags %u:", name, flags);
                Compare(expected, actual, "LoadScene");
            }
        }
    }
    Check(ok, "LoadScene: original vs ours");
    Cleanup();
}

} // namespace

// --- the data overlay (src/game/front/data_overlay.*) -------------------------------------------------------------

void TestDataOverlay()
{
    namespace DO = Front::DataOverlay;
    auto name = [](const char* in, const char* want) {
        char out[260];
        DO::OverlayName(in, out);
        if (std::strcmp(out, want) != 0)
            Log("  data overlay: '%s' -> '%s' (expected '%s')", in, out, want);
        return std::strcmp(out, want) == 0;
    };
    bool ok = name("levels\\mainmenu\\mainmenu.qb", "levels\\mainmenu\\mainmenu.qb") &&
              name("Data/Scripts/Game/Levels.qb", "scripts\\game\\levels.qb") &&
              name("c:\\ports\\THUG2\\THUG2XB\\Skate6\\data\\scripts\\game\\Levels.qb", "scripts\\game\\levels.qb") &&
              name("models\\metadata\\x.qb", "models\\metadata\\x.qb") && name("", "");
    Check(ok, "data overlay: file names");

    // Where a file is looked for: Anteara Mod's folders first while anteara_mod is on (its front_end first while
    // the new front end is on), then the front end's, then the overlay's top - every combination.
    struct Case {
        bool anteara, frontEnd;
        std::vector<std::string> want;
    };
    const Case cases[] = {
        {false, false, {""}},
        {false, true, {"front_end\\", ""}},
        {true, false, {"anteara\\", ""}},
        {true, true, {"anteara\\front_end\\", "anteara\\", "front_end\\", ""}},
    };
    ok = true;
    for (const Case& c : cases) {
        const char* folders[4] = {};
        int count = DO::SearchFolders(c.anteara, c.frontEnd, folders);
        bool same = count == static_cast<int>(c.want.size());
        for (int i = 0; same && i < count; ++i)
            same = c.want[i] == folders[i];
        if (!same)
            Log("  data overlay: anteara=%d front_end=%d -> %d folders, first '%s'", c.anteara, c.frontEnd, count,
                count ? folders[0] : "");
        ok = ok && same;
    }
    Check(ok, "data overlay: the folders searched, anteara_mod and the front end on and off");
}

SELFTEST(Early, TestCameraMethods, Cleanup);
SELFTEST(Early, TestLoadScene, Cleanup);
SELFTEST(Early, TestDataOverlay);
