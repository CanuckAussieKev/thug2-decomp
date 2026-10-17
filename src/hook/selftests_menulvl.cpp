// Self-tests for the menu_backdrop feature (src/game/front/menu_backdrop.*) and the game functions rewritten for it:
// Gfx::Camera's setters and getters (src/game/gfx/camera.cpp) and the LoadScene script command
// (src/game/gfx/scene.cpp). Originals against ours with the feature off (and on but not in play), worked-out values
// for the views' matrices and the feature's decisions, and the feature driven on stand-in game state.

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
#include "game/obj/object.h"
#include "game/core/str.h"
#include "game/front/menu_backdrop.h"
#include "game/front/menu_level.h"
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
namespace MB = Front::MenuBackdrop;

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The setting for one test (put back after).
struct SettingScope {
    int saved;
    explicit SettingScope(int value) : saved(MB::Setting()) { MB::Setting() = value; }
    ~SettingScope() { MB::Setting() = saved; }
};

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
    SettingScope setting(1); // (on, but not in play offline: no skate module, no backdrop scene)
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

bool SameLoad(const Load& load, const char* name, const char* texture, bool addSuperSectors, bool sky, bool net,
              bool dictionary = false)
{
    return load.name == name && load.texture == texture && load.addSuperSectors == addSuperSectors &&
           load.sky == sky && load.dictionary == dictionary && load.net == net;
}

void TestLoadScene()
{
    auto* s = new StandIns;
    InstallLoadSceneStandIns(*s);
    const char* names[] = {"mainmenu", "MainMenu", "LA", "AU_Sky", "la_SKY_x", "Sk5ed_shell", "skyline"};
    bool ok = true;
    for (int setting : {0, 1, 2}) {
        SettingScope scope(setting);
        for (const char* name : names) {
            bool frontEnd = _stricmp(name, "mainmenu") == 0;
            if (setting != 0 && frontEnd)
                continue; // (below: ours loads more)
            for (uint32_t flags = 0; flags < 16; ++flags) {
                std::vector<uint32_t> expected = RunLoadScene(true, name, flags);
                std::vector<uint32_t> actual = RunLoadScene(false, name, flags);
                if (expected != actual) {
                    ok = false;
                    Log("  LoadScene %s flags %u setting %d:", name, flags, setting);
                    Compare(expected, actual, "LoadScene");
                }
            }
        }
    }
    Check(ok, "LoadScene: original vs ours (menu_backdrop off, or not the front end's scene)");

    // The front end's scene with the backdrop on: ours loads it as the original does, then the backdrop's sky and
    // scene (is_net passed on, nothing else), each from a structure of its own, freed after.
    ok = true;
    for (int setting : {1, 2}) {
        SettingScope scope(setting);
        for (uint32_t flags = 0; flags < 16; ++flags) {
            std::vector<uint32_t> expected = RunLoadScene(true, "MainMenu", flags);
            Load first = g_ls_loads.at(0);
            std::vector<uint32_t> actual = RunLoadScene(false, "MainMenu", flags);
            bool net = (flags & 4) != 0;
            // (structures: the front end's description, then for each backdrop scene its parameters and description)
            bool good = g_ls_loads.size() == 3 && g_ls_inits == 5 && g_ls_clears == 5 &&
                        actual.size() > expected.size() &&
                        std::equal(expected.begin(), expected.end() - 2, actual.begin()) &&
                        actual.back() == 1u && SameLoad(g_ls_loads[0], first.name.c_str(), first.texture.c_str(),
                                                        first.addSuperSectors, first.sky, first.net, first.dictionary) &&
                        SameLoad(g_ls_loads[1], "LA_Sky", "levels\\LA_Sky\\LA_Sky.tex", false, true, net) &&
                        SameLoad(g_ls_loads[2], "LA", "levels\\LA\\LA.tex", true, false, net);
            if (!good) {
                ok = false;
                Log("  backdrop setting %d flags %u: %u loads, %u structures made, %u cleared", setting, flags,
                    static_cast<unsigned>(g_ls_loads.size()), g_ls_inits, g_ls_clears);
                for (const Load& load : g_ls_loads)
                    Log("    %s (%s) super sectors %d sky %d dictionary %d net %d", load.name.c_str(),
                        load.texture.c_str(), load.addSuperSectors, load.sky, load.dictionary, load.net);
            }
        }
    }
    Check(ok, "LoadScene: the front end's scene brings the backdrop's sky and scene (menu_backdrop on)");
    Cleanup();
}

// --- the views ---------------------------------------------------------------------------------------------------

bool Near(float a, double b, double tolerance = 2e-6) { return std::fabs(static_cast<double>(a) - b) <= tolerance; }
bool RowIs(const Mth::Vector& v, double x, double y, double z, double w)
{
    return Near(v.x, x) && Near(v.y, y) && Near(v.z, z) && v.w == static_cast<float>(w);
}

void TestMenuBackdropViews()
{
    bool ok = MB::ViewFor(0) == nullptr && MB::ViewFor(3) == nullptr && MB::ViewFor(-1) == nullptr &&
              MB::ViewFor(1) != nullptr && MB::ViewFor(2) != nullptr;
    Check(ok, "menu_backdrop: views 1 and 2 only");
    const MB::View* v1 = MB::ViewFor(1);
    const MB::View* v2 = MB::ViewFor(2);
    ok = v1->pos[0] == -1808.255f && v1->pos[1] == -342.621f && v1->pos[2] == 5804.185f && v1->back[0] == 0.618382f &&
         v1->up[1] == 0.983850f && v2->pos[0] == -1995.710f && v2->pos[2] == 6016.290f && v2->back[2] == 0.707732f &&
         v2->up[2] == -0.128765f;
    Check(ok, "menu_backdrop: the views as captured");

    // Worked out (double precision, the captured floats): row 0 = up x back normalized.
    Mth::Matrix m1 = MB::ViewMatrix(*v1);
    ok = RowIs(m1.row[0], -0.777805, 0.000184, -0.628506, 0) && RowIs(m1.row[1], -0.112357, 0.983850, 0.139335, 0) &&
         RowIs(m1.row[2], 0.618382, 0.178992, -0.765223, 0) && RowIs(m1.row[3], 0, 0, 0, 1);
    Mth::Matrix m2 = MB::ViewMatrix(*v2);
    ok = ok && RowIs(m2.row[0], 0.719350, -0.000148, 0.694647, 0) &&
         RowIs(m2.row[1], 0.124546, 0.983823, -0.128765, 0) && RowIs(m2.row[2], -0.683391, 0.179143, 0.707732, 0) &&
         RowIs(m2.row[3], 0, 0, 0, 1);
    // A right-handed rotation: row 0 . (row 1 x row 2) = +1, rows of length 1.
    for (const Mth::Matrix* m : {&m1, &m2}) {
        const Mth::Vector &a = m->row[0], &b = m->row[1], &c = m->row[2];
        double det = a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) + a.z * (b.x * c.y - b.y * c.x);
        ok = ok && std::fabs(det - 1.0) < 1e-5;
    }
    if (!ok)
        Log("  view 1 row 0 %.6f %.6f %.6f, view 2 row 0 %.6f %.6f %.6f", m1.row[0].x, m1.row[0].y, m1.row[0].z,
            m2.row[0].x, m2.row[0].y, m2.row[0].z);
    // An unnormalized view is normalized.
    MB::View scaled = {{1, 2, 3}, {0, 0, 2}, {0, 3, 0}};
    Mth::Matrix m3 = MB::ViewMatrix(scaled);
    ok = ok && RowIs(m3.row[0], 1, 0, 0, 0) && RowIs(m3.row[1], 0, 1, 0, 0) && RowIs(m3.row[2], 0, 0, 1, 0);
    MB::View tilted = {{0, 0, 0}, {0, 0, -4}, {3, 0, 0}};
    Mth::Matrix m4 = MB::ViewMatrix(tilted);
    ok = ok && RowIs(m4.row[0], 0, 1, 0, 0) && RowIs(m4.row[1], 1, 0, 0, 0) && RowIs(m4.row[2], 0, 0, -1, 0);
    Check(ok, "menu_backdrop: the views' camera matrices");
}

// --- the decisions -----------------------------------------------------------------------------------------------

void TestMenuBackdropDecisions()
{
    bool ok = MB::kBackdropSceneName == Crc::GenerateCRCFromString(MB::kBackdropScene) &&
              MB::kBackdropSkyName == Crc::GenerateCRCFromString(MB::kBackdropSky) &&
              MB::kFrontEndSceneName == Crc::GenerateCRCFromString("mainmenu");
    Check(ok, "menu_backdrop: scene name checksums");
    ok = true;
    for (const char* name : {"mainmenu_camera", "mainmenu_camera02", "mainmenu_camera03", "SS_MenuCam", "level_select_cam"})
        ok = ok && MB::IsMenuCamera(Crc::GenerateCRCFromString(name));
    for (const char* name : {"skater_select_cam", "skater_select_cam01", "skater_select_cam02", "stationary_skater_cam",
                             "CAGR_cam1", "SS_SkaterChoosing", "skateshop_startcam", "test_cam", ""})
        ok = ok && !MB::IsMenuCamera(Crc::GenerateCRCFromString(name));
    ok = ok && !MB::IsMenuCamera(0);
    Check(ok, "menu_backdrop: the menu cameras");

    ok = true;
    for (int setting : {-1, 0, 1, 2, 3}) {
        bool on = setting == 1 || setting == 2;
        for (int bits = 0; bits < 8; ++bits) {
            bool frontEnd = bits & 1, loaded = (bits & 2) != 0, menu = (bits & 4) != 0;
            bool hold = MB::ShouldHold(setting, frontEnd, loaded, menu);
            ok = ok && hold == (on && frontEnd && loaded && menu);
            for (uint32_t scene : {MB::kFrontEndSceneName, MB::kBackdropSceneName, MB::kBackdropSkyName, 0x12345678u}) {
                for (bool holding : {false, true}) {
                    bool hide = MB::ShouldHideScene(scene, setting, frontEnd, loaded, holding);
                    bool expected = false;
                    if (on && frontEnd && loaded)
                        expected = holding ? scene == MB::kFrontEndSceneName
                                           : scene == MB::kBackdropSceneName || scene == MB::kBackdropSkyName;
                    ok = ok && hide == expected;
                }
            }
        }
    }
    Check(ok, "menu_backdrop: when the camera is held and which scenes are hidden");
}

// --- the feature on stand-in game state ----------------------------------------------------------------------------

alignas(16) uint8_t g_ms_skate[0x400];
alignas(16) uint8_t g_ms_career[0x700];
alignas(16) uint8_t g_ms_scenes[4][0x90];
uint32_t g_ms_nodes[5][4];
uint32_t g_ms_anim;
Camera* g_ms_viewport;
int g_ms_recomputes;

// The scene list: the given scenes' names, then the end (the head, scene -1).
void SetScenes(std::initializer_list<uint32_t> names)
{
    int n = 0;
    for (uint32_t name : names) {
        std::memset(g_ms_scenes[n], 0xCD, sizeof(g_ms_scenes[n]));
        std::memcpy(g_ms_scenes[n] + 0x84, &name, 4);
        g_ms_nodes[n][1] = reinterpret_cast<uint32_t>(g_ms_scenes[n]);
        ++n;
    }
    g_ms_nodes[n][1] = 0xFFFFFFFF;
    for (int i = 0; i < n; ++i)
        g_ms_nodes[i][3] = reinterpret_cast<uint32_t>(g_ms_nodes[i + 1]);
    GameVariable<uint32_t>(0x006FA7EC) = reinterpret_cast<uint32_t>(g_ms_nodes[0]);
}

void TestMenuBackdropState()
{
    g_menulvl_saved = {{0x007CE478, 4, {}}, {0x006FA7EC, 4, {}}, {0x00701348, 4, {}}};
    SaveAll(g_menulvl_saved);
    auto* s = new StandIns;
    s->At({0x0057B030}, 0, [](Regs&) { return g_ms_anim; });
    s->At({0x004A09C0}, 0, [](Regs& r) { return r.args[0] == 0 ? reinterpret_cast<uint32_t>(g_ms_viewport) : 0u; });
    s->At({0x00485AA0, AddressOf(&Camera::UpdateAdjustedHFOV)}, 0, [](Regs& r) {
        ++g_ms_recomputes;
        reinterpret_cast<Camera*>(r.ecx)->m_adjustedHFov = 88.5f;
        return 0u;
    });
    GameVariable<float>(0x00701348) = 72.0f; // the default field of view (camera_fov)
    std::memset(g_ms_skate, 0, sizeof(g_ms_skate));
    std::memset(g_ms_career, 0, sizeof(g_ms_career));
    *reinterpret_cast<uint8_t**>(g_ms_skate + 0x20) = g_ms_career;
    *reinterpret_cast<uint32_t*>(g_ms_skate + 0x368) = 0x0BADF00D; // the movie manager (its lookup stood in)
    GameVariable<uint8_t*>(0x007CE478) = g_ms_skate;
    alignas(16) uint8_t cameraMemory[2][kCameraSize];
    Camera* viewport = reinterpret_cast<Camera*>(cameraMemory[0]);
    Camera* other = reinterpret_cast<Camera*>(cameraMemory[1]);
    g_ms_viewport = viewport;
    const uint32_t mainmenu02 = Crc::GenerateCRCFromString("mainmenu_camera02");
    const uint32_t cas = Crc::GenerateCRCFromString("skater_select_cam");
    const uint32_t kOther = 0x0F0F0F0F;

    bool ok = true;
    int cases = 0;
    for (int setting : {0, 1, 2}) {
        SettingScope scope(setting);
        for (int level : {0, 15}) {
            for (int loaded = 0; loaded < 2; ++loaded) {
                for (uint32_t anim : {mainmenu02, cas, 0u}) {
                    for (Camera* camera : {viewport, other}) {
                        g_ms_career[0x630] = static_cast<uint8_t>(level);
                        if (loaded)
                            SetScenes({MB::kFrontEndSceneName, MB::kBackdropSkyName, MB::kBackdropSceneName, kOther});
                        else
                            SetScenes({MB::kFrontEndSceneName, kOther});
                        g_ms_anim = anim;
                        bool holding = setting != 0 && level == 0 && loaded && anim == mainmenu02;
                        bool backdropShown = setting != 0 && level == 0 && loaded;
                        ok = ok && MB::Holding() == holding;
                        ok = ok && MB::HideScene(MB::kFrontEndSceneName) == holding;
                        ok = ok && MB::HideScene(MB::kBackdropSceneName) == (backdropShown && !holding);
                        ok = ok && MB::HideScene(MB::kBackdropSkyName) == (backdropShown && !holding);
                        ok = ok && !MB::HideScene(kOther);

                        // The setters: held -> the view, else what was set.
                        for (int setter = 0; setter < 3; ++setter) {
                            FillCamera(cameraMemory[0], 7 + setter);
                            FillCamera(cameraMemory[1], 7 + setter);
                            g_ms_recomputes = 0;
                            Mth::Vector pos = {1.0f, 2.0f, 3.0f, 1.0f};
                            Mth::Matrix m;
                            for (int r = 0; r < 4; ++r)
                                m.row[r] = {0.5f + r, 0.25f, -0.125f * r, 7.0f};
                            float row3[4];
                            std::memcpy(row3, &camera->m_matrix.row[3], sizeof(row3));
                            if (setter == 0)
                                camera->SetPos(pos);
                            else if (setter == 1)
                                camera->SetMatrix(m);
                            else
                                camera->SetHFOV(33.0f);
                            bool held = holding && camera == viewport;
                            const MB::View* view = MB::ViewFor(setting);
                            Mth::Matrix expected = view ? MB::ViewMatrix(*view) : Mth::Matrix{};
                            if (held) {
                                ok = ok && camera->m_pos.x == view->pos[0] && camera->m_pos.y == view->pos[1] &&
                                     camera->m_pos.z == view->pos[2] && camera->m_pos.w == 1.0f;
                                for (int r = 0; r < 3; ++r)
                                    ok = ok && std::memcmp(&camera->m_matrix.row[r], &expected.row[r], 16) == 0;
                                // (row 3: SetMatrix's, else as it was)
                                ok = ok && (setter == 1 ? camera->m_matrix.row[3].w == 7.0f
                                                        : std::memcmp(&camera->m_matrix.row[3], row3, 16) == 0);
                                ok = ok && camera->m_hFov == 72.0f && camera->m_adjustedHFov == 88.5f &&
                                     g_ms_recomputes == (setter == 2 ? 2 : 1);
                            } else {
                                if (setter == 0)
                                    ok = ok && std::memcmp(&camera->m_pos, &pos, 16) == 0;
                                else if (setter == 1)
                                    ok = ok && std::memcmp(&camera->m_matrix, &m, 64) == 0;
                                else
                                    ok = ok && camera->m_hFov == 33.0f && g_ms_recomputes == 1;
                            }
                        }
                        // Already at the default field of view: not updated again.
                        FillCamera(cameraMemory[0], 3);
                        FillCamera(cameraMemory[1], 3);
                        camera->m_hFov = 72.0f;
                        camera->m_adjustedHFov = -1.0f;
                        g_ms_recomputes = 0;
                        MB::HoldCamera(camera);
                        ok = ok && g_ms_recomputes == 0 && camera->m_adjustedHFov == -1.0f;
                        if (holding && camera == viewport)
                            ok = ok && camera->m_pos.x == MB::ViewFor(setting)->pos[0];
                        ++cases;
                        if (!ok) {
                            Log("  menu_backdrop state: failed at setting %d level %d loaded %d anim %08X %s camera",
                                setting, level, loaded, anim, camera == viewport ? "viewport" : "other");
                            goto done;
                        }
                    }
                }
            }
        }
    }
done:
    // No skate module: never held, nothing hidden.
    GameVariable<uint8_t*>(0x007CE478) = nullptr;
    {
        SettingScope scope(1);
        SetScenes({MB::kFrontEndSceneName, MB::kBackdropSceneName});
        g_ms_anim = mainmenu02;
        ok = ok && !MB::Holding() && MB::HideScene(MB::kBackdropSceneName) && !MB::HideScene(MB::kFrontEndSceneName);
    }
    Check(ok, "menu_backdrop: held camera and hidden scenes on stand-in game state");
    Cleanup();
}

} // namespace

// --- the menu_level feature (src/game/front/menu_level.*) ---------------------------------------------------------

void TestMenuLevel()
{
    namespace ML = Front::MenuLevel;
    auto name = [](const char* in, const char* want) {
        char out[260];
        ML::OverlayName(in, out);
        if (std::strcmp(out, want) != 0)
            Log("  menu_level: '%s' -> '%s' (expected '%s')", in, out, want);
        return std::strcmp(out, want) == 0;
    };
    bool ok = name("levels\\mainmenu\\mainmenu.qb", "levels\\mainmenu\\mainmenu.qb") &&
              name("Data/Scripts/Game/Levels.qb", "scripts\\game\\levels.qb") &&
              name("c:\\ports\\THUG2\\THUG2XB\\Skate6\\data\\scripts\\game\\Levels.qb", "scripts\\game\\levels.qb") &&
              name("models\\metadata\\x.qb", "models\\metadata\\x.qb") && name("", "");
    Check(ok, "menu_level: overlay file names");

    // The move tools/menu_level/make_menu_level.py makes for view 1 takes the shop's main menu camera (180, 40, 300) to
    // the view's place; a turn about up adds to the heading atan2(x, z).
    ML::Move move = {1.896807f, {-2034.805217f, -382.621f, 6070.783948f}};
    float camera[3] = {180.0f, 40.0f, 300.0f}, moved[3];
    ML::MovePoint(move, camera, moved);
    ok = std::fabs(moved[0] - -1808.255f) < 0.01f && std::fabs(moved[1] - -342.621f) < 0.001f &&
         std::fabs(moved[2] - 5804.185f) < 0.01f;
    ML::Move quarter = {1.5707963f, {0, 0, 0}};
    float forward[3] = {0, 0, 1}, side[3] = {0, 2, 0}, turned[3], up[3];
    ML::TurnDirection(quarter, forward, turned);
    ML::TurnDirection(quarter, side, up);
    ok = ok && std::fabs(turned[0] - 1) < 1e-6f && std::fabs(turned[1]) < 1e-6f && std::fabs(turned[2]) < 1e-6f &&
         up[0] == 0 && up[1] == 2 && std::fabs(up[2]) < 1e-6f;
    if (!ok)
        Log("  menu_level: camera moved to %.3f %.3f %.3f, (0 0 1) turned to %f %f %f", moved[0], moved[1], moved[2],
            turned[0], turned[1], turned[2]);
    Check(ok, "menu_level: the move");

    float view[3] = {-1808.255f, -342.621f, 5804.185f}, wheel[3] = {0.0f, -12.75f, -7.07f};
    Check(ML::InShop(camera) && ML::InShop(wheel) && !ML::InShop(view) && !ML::InShop(moved),
          "menu_level: the shop's coordinates");

    // Off unless menu_backdrop is 3: nothing hidden, no overlay files, the camera left alone.
    int saved = MB::Setting();
    char path[260];
    ok = true;
    for (int setting : {0, 1, 2}) {
        MB::Setting() = setting;
        ok = ok && !ML::Active() && !ML::HideScene(MB::kFrontEndSceneName) &&
             !ML::OverlayPath("scripts\\game\\levels.qb", path);
    }
    Camera dummy{};
    dummy.m_pos = {1, 2, 3, 1};
    ML::PlaceCamera(&dummy);
    ok = ok && dummy.m_pos.x == 1 && dummy.m_pos.y == 2 && dummy.m_pos.z == 3;
    MB::Setting() = saved;
    Check(ok, "menu_level: off at settings 0-2");
}

SELFTEST(Early, TestCameraMethods, Cleanup);
SELFTEST(Early, TestLoadScene, Cleanup);
SELFTEST(Early, TestMenuBackdropViews);
SELFTEST(Early, TestMenuBackdropDecisions);
SELFTEST(Early, TestMenuBackdropState, Cleanup);
SELFTEST(Early, TestMenuLevel);
