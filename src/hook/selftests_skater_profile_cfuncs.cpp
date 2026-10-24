// Self-tests for the profile, option and appearance script commands of 0x005AAEF0-0x005B0A40
// (skater_profile_cfuncs.cpp, appearance_cfuncs.cpp): each original against ours on stand-in profiles (with faces), skaters
// and a profile manager; the skate module's, the profiles' and the faces' calls without source stood in for by
// recording code (over the game's function and ours alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/crc.h"
#include "game/script/struct.h"
#include "game/skate/appearance_cfuncs.h"
#include "game/skate/skater_profile_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;

constexpr uint32_t kPlayer = 0x67E6859A;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kCurrentFacePoints = 0x12E87395;
constexpr uint32_t kTexture = 0x7D99F28D;
constexpr uint32_t kFullPathIncluded = 0x54C2E57C;
constexpr int kProfiles = 3;

alignas(16) uint8_t g_profiles[kProfiles][0x64];
alignas(16) uint8_t g_faces[kProfiles][0x40];
alignas(16) uint8_t g_skaters[2][0x330];
alignas(16) uint8_t g_profileManager[0x40];
int g_currentProfile;
std::map<uint32_t, uint32_t> g_answers;
uint32_t g_profileNames[kProfiles] = {0x1A0E0000, 0x1A0E0001, 0x1A0E0002};

uint32_t Answer(uint32_t address)
{
    auto it = g_answers.find(address);
    return it == g_answers.end() ? 0u : it->second;
}
int ProfileIndex(uint32_t address)
{
    for (int i = 0; i < kProfiles; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_profiles[i]))
            return i;
    }
    return -1;
}
int FaceIndex(uint32_t address)
{
    for (int i = 0; i < kProfiles; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_faces[i]))
            return i;
    }
    return -1;
}
void RecordPoints(const uint32_t* words)
{
    g_trace.insert(g_trace.end(), words, words + 14);
}

void StandInProfileCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    for (uint32_t address : {0x00567C30u, 0x00567C00u, 0x00567BD0u}) {
        s.At({address}, 8, [address](Regs& r) {
            Record({address, r.ecx, r.args[0], r.args[1] & 0xFF});
            return 0u;
        });
    }
    s.At({0x00564F80}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[g_currentProfile]);
    });
    s.At({0x00564FC0}, 4, [](Regs& r) {
        Record({0x4FC0, r.ecx, r.args[0]});
        return r.args[0] < kProfiles ? reinterpret_cast<uint32_t>(g_profiles[r.args[0]]) : 0u;
    });
    s.At({0x00586210, AddressOf(&Obj::CSkaterProfile::GetDisplayName)}, 0, [](Regs& r) {
        int i = ProfileIndex(r.ecx);
        Record({0x6210, static_cast<uint32_t>(i)});
        static const char* names[] = {"Tony Hawk", "Bob Burnquist", "Custom"};
        return reinterpret_cast<uint32_t>(i >= 0 ? names[i] : "?");
    });
    s.At({0x00586150, AddressOf(&Obj::CSkaterProfile::GetName)}, 0, [](Regs& r) {
        int i = ProfileIndex(r.ecx);
        Record({0x6150, static_cast<uint32_t>(i)});
        return i >= 0 ? g_profileNames[i] : 0u;
    });
    s.At({0x00478390, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t, const char*)>(&CStruct::AddString))},
         12, [](Regs& r) {
             Record({0x8390, r.ecx, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
             return 0u;
         });
    s.At({0x0057BA20, AddressOf(&Obj::CPlayerProfileManager::SetCurrentSkater)}, 4, [](Regs& r) {
        Record({0xBA20, r.ecx, r.args[0]});
        for (int i = 0; i < kProfiles; ++i) {
            if (g_profileNames[i] == r.args[0])
                g_currentProfile = i;
        }
        *reinterpret_cast<int*>(g_profileManager + 0x1C) = g_currentProfile;
        return 0u;
    });
    s.At({0x005665C0}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] < 2 ? reinterpret_cast<uint32_t>(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x00580EB0, AddressOf(&Obj::CSkater::UpdateFromProfile)}, 4, [](Regs& r) {
        Record({0x0EB0, r.ecx, static_cast<uint32_t>(ProfileIndex(r.args[0]))});
        return 0u;
    });
    s.At({0x00488F40}, 8, [](Regs& r) {
        Record({0x8F40, static_cast<uint32_t>(ProfileIndex(r.ecx)), r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    // The faces
    s.At({0x00487550}, 4, [](Regs& r) {
        int i = FaceIndex(r.ecx);
        Record({0x7550, static_cast<uint32_t>(i)});
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        for (int k = 0; k < 14; ++k)
            out[k] = 0xFA000000u + static_cast<uint32_t>(i) * 0x100 + k;
        return r.args[0];
    });
    s.At({0x00487570}, 4, [](Regs& r) {
        Record({0x7570, static_cast<uint32_t>(FaceIndex(r.ecx))});
        RecordPoints(reinterpret_cast<uint32_t*>(r.args[0]));
        return 0u;
    });
    s.At({0x00487000}, 0, [](Regs& r) {
        Record({0x7000, r.args[1]});
        RecordPoints(reinterpret_cast<uint32_t*>(r.args[0]));
        return Answer(0x00487000);
    });
    s.At({0x004869D0}, 0, [](Regs& r) {
        Record({0x69D0, r.args[1]});
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        for (int k = 0; k < 14; ++k)
            out[k] = 0xFB000000u + r.args[1] % 0x10000 + k;
        return Answer(0x004869D0);
    });
    s.At({0x00487380}, 8, [](Regs& r) {
        Record({0x7380, static_cast<uint32_t>(FaceIndex(r.ecx)), Hash(reinterpret_cast<const char*>(r.args[0])),
                r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x00487590}, 4, [](Regs& r) {
        Record({0x7590, static_cast<uint32_t>(FaceIndex(r.ecx)), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x00487520}, 4, [](Regs& r) {
        Record({0x7520, static_cast<uint32_t>(FaceIndex(r.ecx)), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00487510}, 0, [](Regs& r) {
        Record({0x7510, static_cast<uint32_t>(FaceIndex(r.ecx))});
        return Answer(0x00487510);
    });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8,
         [](Regs& r) {
             Record({0x8670, r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
}

void TestProfileCFuncs()
{
    Setup();
    g_saved.push_back({0x007CE4B4, 4, {}});
    g_saved.back().bytes.resize(4);
    std::memcpy(g_saved.back().bytes.data(), reinterpret_cast<void*>(0x007CE4B4), 4);
    GameVariable<uint8_t*>(0x007CE4B4) = g_profileManager;
    auto* standIns = new StandIns;
    StandInProfileCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        for (int i = 0; i < 0x40; ++i)
            g_skate[0x394 + i] = static_cast<uint8_t>((i * 37 + 5) % 3 == 0); // (the option table, uneven)
        std::memset(g_profiles, 0, sizeof(g_profiles));
        std::memset(g_skaters, 0, sizeof(g_skaters));
        std::memset(g_profileManager, 0, sizeof(g_profileManager));
        for (int i = 0; i < kProfiles; ++i)
            *reinterpret_cast<uint8_t**>(g_profiles[i] + 0x54) = i == 2 ? nullptr : g_faces[i]; // (2: no face)
        g_currentProfile = 0;
        *reinterpret_cast<int*>(g_profileManager + 0x1C) = 0;
        g_answers.clear();
        auto call = [&](uint32_t address, CFunc ours, Params& params) { CallCommand(original, address, ours, params); };
        using namespace CFuncs;
        Params none;

        // The options, for players 0-3
        for (int player : {0, 1, 3}) {
            Params p; p.Integer(0, player);
            call(0x005AE370, CFunc_VibrationIsOn, p);
            call(0x005AE400, CFunc_AutoKickIsOn, p);
            call(0x005AE490, CFunc_SpinTapsAreOn, p);
            call(0x005AE3A0, CFunc_VibrationOn, p);
            call(0x005AE3D0, CFunc_VibrationOff, p);
            call(0x005AE430, CFunc_AutoKickOn, p);
            call(0x005AE460, CFunc_AutoKickOff, p);
            call(0x005AE4C0, CFunc_SpinTapsOn, p);
            call(0x005AE4F0, CFunc_SpinTapsOff, p);
        }
        call(0x005AE370, CFunc_VibrationIsOn, none);
        call(0x005AE4F0, CFunc_SpinTapsOff, none);
        // The pro's display name; selecting the current skater (the same, another, by the unnamed checksum, the
        // index without a skater)
        call(0x005AE520, CFunc_GetCurrentProDisplayInfo, none);
        Params same; same.Checksum(kName, g_profileNames[0]);
        call(0x005AE900, CFunc_SelectCurrentSkater, same);
        Params other; other.Checksum(kName, g_profileNames[1]);
        call(0x005AE900, CFunc_SelectCurrentSkater, other);
        call(0x005AE520, CFunc_GetCurrentProDisplayInfo, none);
        Params unnamed; unnamed.Checksum(0, g_profileNames[2]);
        call(0x005AE900, CFunc_SelectCurrentSkater, unnamed);
        Params both; both.Checksum(0, g_profileNames[0]).Checksum(kName, g_profileNames[1]);
        call(0x005AE900, CFunc_SelectCurrentSkater, both);
        // The appearance and the face
        static Params appearance;
        if (!appearance.count)
            appearance.Integer(0x5555, 1);
        Params a; a.Integer(kPlayer, 1).Structure(Crc::GenerateCRCFromString("appearance_structure"), appearance.Struct());
        call(0x005AE550, CFunc_SetPlayerAppearance, a);
        for (uint32_t answer : {0u, 1u}) {
            g_answers[0x00487000] = answer;
            g_answers[0x004869D0] = answer;
            g_answers[0x00487510] = answer;
            for (int player : {0, 1}) {
                Params p; p.Integer(kPlayer, player);
                call(0x005AE5B0, CFunc_GetPlayerFacePoints, p);
                Params existing;
                existing.Integer(0x1234, 5).Structure(kCurrentFacePoints, appearance.Struct());
                g_scriptParamsOverride = &existing;
                call(0x005AE5B0, CFunc_GetPlayerFacePoints, p);
                g_scriptParamsOverride = nullptr;
                call(0x005AE710, CFunc_SetPlayerFacePoints, p);
                Params q; q.Integer(kPlayer, player).Structure(Crc::GenerateCRCFromString("face_points"), appearance.Struct());
                call(0x005AE710, CFunc_SetPlayerFacePoints, q);
                Params t; t.Integer(kPlayer, player).String(kTexture, "faces\\face01.img");
                call(0x005AE7A0, CFunc_SetPlayerFaceTexture, t);
                t.Flag(kFullPathIncluded);
                call(0x005AE7A0, CFunc_SetPlayerFaceTexture, t);
                call(0x005AE820, CFunc_SetPlayerFaceOverlayTexture, t);
                call(0x005AE880, CFunc_ClearPlayerFaceTexture, p);
                call(0x005AE8C0, CFunc_PlayerFaceIsValid, p);
            }
            Params p2; p2.Integer(kPlayer, 2);
            call(0x005AE8C0, CFunc_PlayerFaceIsValid, p2);
        }
        RecordState();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 900, "profile commands: the original reached its calls");
    Compare(traces[0], traces[1], "option, profile and appearance commands (0x005AE370-0x005AE900)");
}

} // namespace

SELFTEST(Early, TestProfileCFuncs, Restore);
