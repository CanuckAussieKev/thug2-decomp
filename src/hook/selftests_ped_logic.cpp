// Self-tests for the PedLogic component and the projectile components (src/game/obj/ped_logic*.cpp,
// projectile_components.cpp): the original and ours in a stand-in world (appmain_components.h, plus the structures,
// scripts, tags, sounds, network players and scores these call), every call recorded and the memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/model_component.h"
#include "game/obj/motion_path.h"
#include "game/obj/object.h"
#include "game/obj/ped_logic.h"
#include "game/obj/projectile_components.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/sys/timer.h"
#include "hook/appmain_components.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Script::CStruct;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// The world of these tests, besides the component world's object: the component itself, its fellow components,
// structures (fake: only their address is used), scripts, network players and scores.
struct PedWorld {
    alignas(16) uint8_t ped[0x200];
    alignas(16) uint8_t projectile[0x80];
    alignas(16) uint8_t motion[0x260];
    alignas(16) uint8_t looping[0x110];
    alignas(16) uint8_t sound[0x40];
    alignas(16) uint8_t model[0x40];
    alignas(16) uint8_t suspend[0x40];
    alignas(16) uint8_t script[0x100];
    alignas(16) uint8_t aux[0x100];
    alignas(16) uint8_t structs[4][0x40];
    alignas(16) uint8_t skater[0x400];
    alignas(16) uint8_t skaters[3][0x110];
    alignas(16) uint8_t scoreComponents[3][0x20];
    alignas(16) uint8_t scores[3][0x20];
    alignas(16) uint8_t players[3][0x40];
    alignas(16) uint8_t manager[0x40];
    alignas(16) uint8_t core[0x300];
    alignas(16) uint8_t profile[0x80];
    alignas(16) uint8_t tags[0x20];
};
PedWorld g_ped;
void* g_objectVtable[12]; // the object's (MarkAsDead, slot 6, and the rest: recorded)

uint32_t PedRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_ped);
    if (word >= base && word < base + sizeof(g_ped))
        return 0xD0000000 + (word - base);
    return WorldRelocate(word);
}

// What the extra stand-ins answer (each run sets them).
struct PedAnswers {
    std::function<bool(uint32_t self, uint32_t name, CStruct** out)> getStructure = [](uint32_t, uint32_t, CStruct**) {
        return false;
    };
    std::function<bool(uint32_t self, uint32_t name, int* out)> getInteger = [](uint32_t, uint32_t, int*) { return false; };
    std::function<bool(uint32_t self, uint32_t name, const char** out)> getString = [](uint32_t, uint32_t, const char**) {
        return false;
    };
    std::function<bool(uint32_t self, uint32_t flag)> containsFlag = [](uint32_t, uint32_t) { return false; };
    std::function<bool(uint32_t name, int* out)> integerTag = [](uint32_t, int*) { return false; };
    std::function<bool(uint32_t name, uint32_t* out)> checksumTag = [](uint32_t, uint32_t*) { return false; };
    std::function<bool(uint32_t name)> scriptExists = [](uint32_t) { return false; };
    std::function<bool()> gotScript = [] { return true; };
    std::function<bool()> rotating = [] { return false; };
    std::function<uint32_t(uint32_t index)> arrayElement = [](uint32_t index) { return 0x5E000000u + index; };
    uint32_t arraySize = 3;
    bool newFails = false;
    // the network
    bool inNetGame = true, collision = true, teamGame = false, friendlyFire = false, local = false;
    std::function<bool(uint32_t player)> canBeHit = [](uint32_t) { return true; };
    std::function<int(uint32_t score)> totalScore = [](uint32_t) { return 50; };
    int skaterCount = 3;
    int scoreNext = 0;
};
PedAnswers g_pa;

// The structures the code gets and makes: their bytes aren't used, only who they are.
CStruct* FakeStruct(int i) { return reinterpret_cast<CStruct*>(g_ped.structs[i]); }

void StandInPedWorld(StandIns& s)
{
    using Obj::CObject;
    using Script::CScript;
    // Structures.
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [](Regs& r) {
             auto** out = reinterpret_cast<CStruct**>(r.args[1]);
             bool found = g_pa.getStructure(r.ecx, r.args[0], out);
             Record({0x67C0, PedRelocate(r.ecx), r.args[0], r.args[2], found, found ? PedRelocate(reinterpret_cast<uint32_t>(*out)) : 0});
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             auto* out = reinterpret_cast<int*>(r.args[1]);
             bool found = g_pa.getInteger(r.ecx, r.args[0], out);
             Record({0x6630, PedRelocate(r.ecx), r.args[0], r.args[2], found, found ? PedRelocate(static_cast<uint32_t>(*out)) : 0});
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             auto** out = reinterpret_cast<const char**>(r.args[1]);
             bool found = g_pa.getString(r.ecx, r.args[0], out);
             Record({0x6590, PedRelocate(r.ecx), r.args[0], r.args[2], found, found ? PedRelocate(reinterpret_cast<uint32_t>(*out)) : 0});
             return static_cast<uint32_t>(found);
         });
    // (replace the component world's: the value recorded only when found - the code's locals may start as anything)
    auto checksum = [](Regs& r) {
        bool found = g_answers.getChecksum(r.args[0], reinterpret_cast<uint32_t*>(r.args[1]));
        Record({0x6EC0, PedRelocate(r.ecx), r.args[0], r.args[2] & 0xFF, found,
                found ? *reinterpret_cast<uint32_t*>(r.args[1]) : 0});
        return static_cast<uint32_t>(found);
    };
    s.At({0x00476EC0}, 12, checksum);
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         checksum);
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             bool found = g_answers.getFloat(r.args[0], reinterpret_cast<float*>(r.args[1]));
             Record({0x66D0, PedRelocate(r.ecx), r.args[0], r.args[2], found,
                     found ? *reinterpret_cast<uint32_t*>(r.args[1]) : 0});
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        bool found = g_pa.containsFlag(r.ecx, r.args[0]);
        Record({0x6B40, PedRelocate(r.ecx), r.args[0], found});
        return static_cast<uint32_t>(found);
    });
    auto add = [&s](uint32_t game, uint32_t ours, uint16_t pop, uint32_t tag) {
        s.At({game, ours}, pop, [tag, pop](Regs& r) {
            Record({tag, PedRelocate(r.ecx), r.args[0], r.args[1]});
            if (pop >= 12)
                Record({r.args[2]});
            if (pop >= 20)
                Record({r.args[3], r.args[4]});
            return 0u;
        });
    };
    add(0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum)), 8, 0x7D40);
    add(0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat)), 8, 0x7C60);
    add(0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger)), 8, 0x7B80);
    add(0x00477F70, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::VectorValue)>(&CStruct::AddVector)), 20,
        0x7F70);
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             const char* text = reinterpret_cast<const char*>(r.args[1]);
             uint32_t where = reinterpret_cast<uint32_t>(text);
             // (a string of ours or a stand-in's: by its text; anything else - a quirk's stray pointer - by where)
             bool known = where >= reinterpret_cast<uint32_t>(&g_ped) && where < reinterpret_cast<uint32_t>(&g_ped) + sizeof(g_ped);
             Record({0x79D0, PedRelocate(r.ecx), r.args[0], known ? PedRelocate(where) : Hash(text)});
             return 0u;
         });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, PedRelocate(r.ecx), PedRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7130, PedRelocate(r.ecx)});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, PedRelocate(r.ecx)});
        return r.ecx;
    });
    // A new structure: the stand-in's, from the pools (the original's inlined operator new) or NewStruct.
    auto newStruct = [](Regs&) {
        Record({0x9001, 8});
        return g_pa.newFails ? 0u : reinterpret_cast<uint32_t>(FakeStruct(3));
    };
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, newStruct);
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, PedRelocate(r.args[0])});
        return 0u;
    });
    // The pools (the original's inlined operator news: structures 8 bytes, scripts 0xF4).
    auto pool = [](uint32_t size) {
        Record({0x9001, size});
        if (g_pa.newFails)
            return 0u;
        return reinterpret_cast<uint32_t>(size == 8 ? static_cast<void*>(FakeStruct(3)) : static_cast<void*>(g_ped.aux));
    };
    s.At({0x005BA3D0}, 0, [pool](Regs& r) {
        // (the pool's own block size isn't known here: structures' pool is the one the original asks first)
        return pool(r.ecx == GameVariable<uint32_t>(0x006F361C + 4 * GameVariable<uint32_t>(0x006F3614)) ? 8u : 0xF4u);
    });
    s.At({0x005BAFC0}, 12, [pool](Regs& r) { return pool(r.args[0]); });
    s.At({AddressOf(static_cast<void* (*)(size_t)>(&CScript::operator new))}, 0, [pool](Regs& r) { return pool(r.args[0]); });
    s.At({0x004499B0, AddressOf(static_cast<void (*)(void*)>(&CScript::operator delete))}, 0, [](Regs& r) {
        Record({0x99B0, PedRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x004738E0, AddressOf(&CScript::Construct)}, 0, [](Regs& r) {
        Record({0x38E0, PedRelocate(r.ecx)});
        return r.ecx;
    });
    s.At({0x00474420, AddressOf(&CScript::Destroy)}, 0, [](Regs& r) {
        Record({0x4420, PedRelocate(r.ecx)});
        return 0u;
    });
    s.At({0x004747A0,
          AddressOf(static_cast<void (CScript::*)(uint32_t, CStruct*, Obj::CObject*)>(&CScript::SetScript))},
         12, [](Regs& r) {
             Record({0x47A0, PedRelocate(r.ecx), r.args[0], PedRelocate(r.args[1]), PedRelocate(r.args[2])});
             return 0u;
         });
    s.At({0x00472940, AddressOf(&CScript::GotScript)}, 0, [](Regs& r) {
        bool got = g_pa.gotScript();
        Record({0x2940, PedRelocate(r.ecx), got});
        return static_cast<uint32_t>(got);
    });
    s.At({0x004750B0, AddressOf(&CScript::Update)}, 0, [](Regs& r) {
        Record({0x50B0, PedRelocate(r.ecx)});
        return 0u;
    });
    s.At({0x00472DE0, AddressOf(&Script::ScriptExists)}, 0, [](Regs& r) {
        bool exists = g_pa.scriptExists(r.args[0]);
        Record({0x2DE0, r.args[0], exists});
        return static_cast<uint32_t>(exists);
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070 + 1, r.args[0]});
        auto* array = reinterpret_cast<uint32_t*>(g_ped.structs[2]);
        array[1] = g_pa.arraySize; // (m_size, +4)
        return reinterpret_cast<uint32_t>(array);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, PedRelocate(r.ecx), r.args[0]});
        return g_pa.arrayElement(r.args[0]);
    });
    // Nodes.
    s.At({0x0045C560, AddressOf(static_cast<bool (*)(uint32_t, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             auto* out = reinterpret_cast<float*>(r.args[1]);
             for (int i = 0; i < 4; ++i)
                 out[i] = static_cast<float>(r.args[0] % 1000) * 0.5f + static_cast<float>(i);
             Record({0xC560, r.args[0], PedRelocate(r.args[1])});
             return 1u;
         });
    // The object.
    s.At({0x004607C0, AddressOf(&CObject::GetIntegerTag)}, 8, [](Regs& r) {
        bool found = g_pa.integerTag(r.args[0], reinterpret_cast<int*>(r.args[1]));
        Record({0x07C0, PedRelocate(r.ecx), r.args[0], found});
        return static_cast<uint32_t>(found);
    });
    s.At({0x004607E0, AddressOf(&CObject::GetChecksumTag)}, 8, [](Regs& r) {
        bool found = g_pa.checksumTag(r.args[0], reinterpret_cast<uint32_t*>(r.args[1]));
        Record({0x07E0, PedRelocate(r.ecx), r.args[0], found});
        return static_cast<uint32_t>(found);
    });
    s.At({0x004616D0, AddressOf(&CObject::SetChecksumTag)}, 8, [](Regs& r) {
        Record({0x16D0, PedRelocate(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00461350, AddressOf(&CObject::SwitchScript)}, 8, [](Regs& r) {
        Record({0x1350, PedRelocate(r.ecx), r.args[0], PedRelocate(r.args[1])});
        return 0u;
    });
    s.At({0x004609F0, AddressOf(&CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x09F0, PedRelocate(r.ecx), r.args[0], PedRelocate(r.args[1])});
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return 0x0B1E0000u + (r.args[0] & 0xFF);
    });
    // Components.
    s.At({0x004202A0, AddressOf(&Obj::CModelComponent::HideGeom)}, 12, [](Regs& r) {
        Record({0x02A0, PedRelocate(r.ecx), r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFF});
        return 1u;
    });
    s.At({0x00423150, AddressOf(&Obj::CMotionPathMembers::IsRotating)}, 0, [](Regs& r) {
        bool rotating = g_pa.rotating();
        Record({0x3150, PedRelocate(r.ecx), rotating});
        return static_cast<uint32_t>(rotating);
    });
    s.At({0x00518FA0, AddressOf(&Obj::CSkaterSoundComponent::PlayJumpHere)}, 4, [](Regs& r) {
        Record({0x8FA0, PedRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00518EA0, AddressOf(&Obj::CSkaterSoundComponent::PlayLandHere)}, 4, [](Regs& r) {
        Record({0x8EA0, PedRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x0057B630, AddressOf(&Obj::CPlayerProfileManager::GetCurrentProfile)}, 0, [](Regs& r) {
        Record({0xB630, r.ecx});
        return reinterpret_cast<uint32_t>(g_ped.profile);
    });
    // The network game.
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, PedRelocate(r.ecx), g_pa.inNetGame});
        return static_cast<uint32_t>(g_pa.inNetGame);
    });
    s.At({0x00534340, AddressOf(&GameNet::Manager::PlayerCollisionEnabled)}, 0, [](Regs& r) {
        Record({0x4340, PedRelocate(r.ecx), g_pa.collision});
        return static_cast<uint32_t>(g_pa.collision);
    });
    s.At({0x0041AD90, AddressOf(&Mdl::Skate::GetNumSkaters)}, 0, [](Regs& r) {
        Record({0xAD90, PedRelocate(r.ecx)});
        return static_cast<uint32_t>(g_pa.skaterCount);
    });
    s.At({0x005535F0, AddressOf(&Game::CGameMode::IsTeamGame)}, 0, [](Regs& r) {
        Record({0x35F0, r.ecx});
        return static_cast<uint32_t>(g_pa.teamGame);
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, PedRelocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_ped.skaters[r.args[0] % 3]);
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xAEA0, PedRelocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_ped.players[(r.args[0] & 0xFFFF) % 3]);
    });
    s.At({0x005430E0, AddressOf(&GameNet::PlayerInfo::CanBeHitAgain)}, 0, [](Regs& r) {
        bool can = g_pa.canBeHit(PedRelocate(r.ecx));
        Record({0x30E0, PedRelocate(r.ecx), can});
        return static_cast<uint32_t>(can);
    });
    s.At({0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, [](Regs& r) {
        int total = g_pa.totalScore(PedRelocate(r.ecx));
        Record({0x1B40, PedRelocate(r.ecx), static_cast<uint32_t>(total)});
        return static_cast<uint32_t>(total);
    });
    s.At({0x00561A80, AddressOf(&Mdl::Score::SetTotalScore)}, 4, [](Regs& r) {
        Record({0x1A80, PedRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00532F70, AddressOf(&GameNet::Manager::GetNetworkPreferences)}, 0, [](Regs& r) {
        Record({0x2F70, PedRelocate(r.ecx)});
        return 0x9EF50000u;
    });
    s.At({0x0046C590, AddressOf(&Prefs::Preferences::GetPreferenceChecksum)}, 8, [](Regs& r) {
        Record({0xC590, r.ecx, r.args[0], r.args[1]});
        return g_pa.friendlyFire ? 0xF81BC89Bu : 0x12345678u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return 0x00ABCDEFu;
    });
    s.At({0x00543110, AddressOf(&GameNet::PlayerInfo::SetLastHitTime)}, 4, [](Regs& r) {
        Record({0x3110, PedRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        Record({0x34A0, PedRelocate(r.ecx)});
        return 0xC0110000u + (PedRelocate(r.ecx) & 0xFFFF);
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* msg = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        Record({0x7EB0, PedRelocate(r.ecx), r.args[0], msg->m_id, msg->m_length, static_cast<uint32_t>(msg->m_priority),
                static_cast<uint32_t>(msg->m_queue), msg->m_groupId, msg->m_singular,
                HashBytes(static_cast<const uint8_t*>(msg->m_data), msg->m_length)});
        return 0u;
    });
    s.At({0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, [](Regs& r) {
        Record({0x3300, PedRelocate(r.ecx), g_pa.local});
        return static_cast<uint32_t>(g_pa.local);
    });
    s.At({0x00581580, AddressOf(&Obj::CSkater::GetDisplayName)}, 0, [](Regs& r) {
        Record({0x1580, PedRelocate(r.ecx)});
        return reinterpret_cast<uint32_t>("Skater Name");
    });
    s.At({0x00564C60, AddressOf(&Mdl::Skate::HideSkater)}, 8, [](Regs& r) {
        Record({0x4C60, PedRelocate(r.ecx), PedRelocate(r.args[0]), r.args[1] & 0xFF});
        return 0u;
    });
}

// The components an object of these tests has (by type; null where the seed leaves one out).
Obj::CBaseComponent* PedComponent(uint32_t type, uint32_t missing)
{
    switch (type) {
    case 0x0A015E17: return missing & 1 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_ped.motion);
    case 0xB5F1E9D2: return missing & 2 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_ped.looping);
    case 0x187574FA: return missing & 4 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_ped.sound);
    case 0x286A8D26: return missing & 8 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_ped.model);
    case 0x6EFDDFB8: return missing & 16 ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_ped.ped);
    case 0x5BD63B29: return reinterpret_cast<Obj::CBaseComponent*>(g_ped.core);
    default: return nullptr;
    }
}

std::vector<uint32_t> PedWords()
{
    std::vector<uint32_t> words;
    for (uint32_t i = 0; i < sizeof(g_ped) / 4; ++i)
        words.push_back(PedRelocate(reinterpret_cast<const uint32_t*>(&g_ped)[i]));
    for (uint32_t i = 0; i < offsetof(ComponentWorld, shadowVtable) / 4; ++i)
        words.push_back(PedRelocate(reinterpret_cast<const uint32_t*>(&g_world)[i]));
    return words;
}

GameMemory* PedMemory()
{
    return new GameMemory({{0x007CE478, 4}, {0x007D1800, 4}, {0x007CC979, 1}, {0x007CCE14, 4}, {0x007CE470, 4},
                           {0x007CE4B4, 4}});
}

void FillRandom(uint8_t* p, size_t size, Lcg& random)
{
    for (size_t i = 0; i < size; ++i)
        p[i] = static_cast<uint8_t>(random.Next());
}

// The projectile components: made (and failing to be), destroyed, set up; the score a hit takes (every type, odd
// scales); the collision's update outside / in a network game, with player collision off, far, already hit, no
// skaters, the owner skipped, players that can't be hit, a fireball at a skater with no score, team mates with and
// without friendly fire, out of reach and in, with and without a death script, a fireball's score taken (some left,
// none left: the local player or not, a score already at 0); the proximity's update with no skater yet, no object id,
// no PedLogic yet, a shot ped, projectiles in flight or not, in reach and out.
void TestProjectileComponents()
{
    std::unique_ptr<GameMemory> memory(PedMemory());
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    StandInPedWorld(s);
    g_relocate = PedRelocate;
    for (void*& slot : g_objectVtable)
        slot = s.Thunk(0, [](Regs& r) { Record({0x0B1E, PedRelocate(r.ecx)}); return 0u; });
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x9C000000 + seed);
            Lcg random(0x9C010000 + seed);
            ResetComponentWorld(random);
            std::memset(&g_ped, 0, sizeof(g_ped));
            WorldObject()->m_vtable = g_objectVtable;
            GameVariable<uint8_t*>(0x007CCE14) = g_ped.manager;
            GameVariable<uint32_t>(0x007CE470) = 0x6A3E0000;
            *reinterpret_cast<uint8_t**>(g_ped.manager + 4) = g_ped.manager + 0x20; // (the server app)
            g_answers = Answers();
            g_pa = PedAnswers();
            uint32_t missing = random.Below(2) ? 0 : 16;
            g_answers.component = [missing](uint32_t type) -> Obj::CBaseComponent* {
                if (type == 0xD262802F) // a skater's score component (+0x18: its score)
                    return reinterpret_cast<Obj::CBaseComponent*>(g_ped.scoreComponents[g_pa.scoreNext++ % 3]);
                return PedComponent(type, missing);
            };
            for (int i = 0; i < 3; ++i) {
                *reinterpret_cast<uint8_t**>(g_ped.scoreComponents[i] + 0x18) = g_ped.scores[i];
                auto* skater = reinterpret_cast<Obj::CCompositeObject*>(g_ped.skaters[i]);
                skater->m_id = static_cast<uint32_t>(i + 1);
                float spread = static_cast<float>(seed % 5) * 20.0f;
                skater->m_pos = {WorldObject()->m_pos.x + spread * static_cast<float>(i) - 30.0f,
                                 WorldObject()->m_pos.y + static_cast<float>(random.Below(30)),
                                 WorldObject()->m_pos.z + static_cast<float>(random.Below(60)) - 30.0f, 1.0f};
                *reinterpret_cast<uint32_t*>(g_ped.players[i] + 0x30) = random.Below(2);
            }
            g_pa.inNetGame = seed % 7 != 3;
            g_pa.collision = seed % 5 != 2;
            g_pa.teamGame = seed % 3 == 0;
            g_pa.friendlyFire = seed % 6 == 0;
            g_pa.local = seed % 4 == 1;
            g_pa.skaterCount = seed == 9 ? 0 : 3;
            uint32_t hitMask = random.Below(8);
            g_pa.canBeHit = [hitMask](uint32_t player) {
                return ((hitMask >> ((player - 0xD0000000 - offsetof(PedWorld, players)) / 0x40)) & 1) == 0;
            };
            int scoreValue = static_cast<int>(random.Below(4)) * 15 - 15;
            g_pa.totalScore = [scoreValue](uint32_t) { return scoreValue; };
            Originals guard(original, {0x004F3010, 0x004F2F10, 0x004F2F50, 0x004F2F80, 0x004F2F40, 0x004F2F20,
                                       0x004F3090, 0x004F3550, 0x004F34E0, 0x004F34F0, 0x004F3520, 0x004F35B0});
            std::vector<uint32_t> trace;
            // The collision component.
            g_answers.callocFails = seed == 5;
            trace.push_back(PedRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004F3010)()));
            g_answers.callocFails = false;
            auto* c = reinterpret_cast<Obj::CProjectileCollisionComponent*>(g_ped.projectile);
            FillRandom(g_ped.projectile, sizeof(g_ped.projectile), random);
            c->m_object = WorldObject();
            c->mp_suspend = g_ped.suspend;
            g_ped.suspend[0x30] = seed % 8 == 4;
            static const uint32_t kTypes[] = {0xD039432C, 0x12345678, 0};
            uint32_t type = kTypes[seed % 3];
            g_answers.getFloat = [seed](uint32_t name, float* out) {
                *out = name == 0xC48391A5 ? 20.0f + static_cast<float>(seed) * 3.0f : 1.7f + static_cast<float>(seed);
                return true;
            };
            g_answers.getChecksum = [type](uint32_t name, uint32_t* out) {
                *out = name == 0xB4D91E49 ? type : name == 0x81C39E06 ? 2u : 0x0DEA0000u + (name & 0xFFFF);
                return true;
            };
            Fn<void(__thiscall*)(void*, void*)>(original, 0x004F2F80)(c, FakeStruct(0));
            if (seed % 4 == 0)
                c->m_deathScript = 0;
            static const float kScales[] = {0.0f, 1.0f, 2.9f, -3.5f, 1000.25f};
            for (float scale : kScales) {
                for (uint32_t t : kTypes) {
                    float savedScale = c->m_scale;
                    uint32_t savedType = c->m_projectileType;
                    c->m_scale = scale;
                    c->m_projectileType = t;
                    trace.push_back(static_cast<uint32_t>(Fn<int(__thiscall*)(void*)>(original, 0x004F2F20)(c)));
                    c->m_scale = savedScale;
                    c->m_projectileType = savedType;
                }
            }
            // Each team / friendly-fire case with the hit skater at the edge of the reach (in, on, out).
            Mth::Vector savedPos = WorldObject()->m_pos;
            WorldObject()->m_pos = {100.0f, 20.0f, -50.0f, 1.0f};
            bool savedNet = g_pa.inNetGame, savedCollision = g_pa.collision;
            g_pa.inNetGame = true;
            g_pa.collision = true;
            auto savedCanBeHit = g_pa.canBeHit;
            g_pa.canBeHit = [](uint32_t) { return true; };
            for (int k = 0; k < 8; ++k) {
                g_pa.teamGame = (k & 1) != 0;
                g_pa.friendlyFire = (k & 2) != 0;
                for (int i = 0; i < 3; ++i)
                    *reinterpret_cast<uint32_t*>(g_ped.players[i] + 0x30) = (k & 4) ? 1u : static_cast<uint32_t>(i);
                static const float kEdges[] = {-0.5f, 0.0f, 0.5f, -1.5f, 0.25f};
                float reach = c->m_radius + 48.0f;
                for (int i = 0; i < 3; ++i) {
                    auto* skater = reinterpret_cast<Obj::CCompositeObject*>(g_ped.skaters[i]);
                    skater->m_pos = {i == 0 ? 100.0f + reach + kEdges[(k + seed) % 5] : 5000.0f, 20.0f, -50.0f, 1.0f};
                }
                c->m_hit = false;
                int taken = c->m_projectileType == 0xD039432C ? static_cast<int>(c->m_scale) * 10 : 0;
                int total = (k & 3) == 0 ? taken : (k & 3) == 1 ? taken + 5 : (k & 3) == 2 ? 0 : taken - 5;
                g_pa.totalScore = [total](uint32_t) { return total; };
                Fn<void(__thiscall*)(void*)>(original, 0x004F3090)(c);
                trace.push_back(0xED6E0000 + static_cast<uint32_t>(k));
            }
            g_pa.canBeHit = savedCanBeHit;
            g_pa.totalScore = [scoreValue](uint32_t) { return scoreValue; };
            g_pa.inNetGame = savedNet;
            g_pa.collision = savedCollision;
            WorldObject()->m_pos = savedPos;
            c->m_hit = seed % 9 == 7;
            Fn<void(__thiscall*)(void*)>(original, 0x004F3090)(c);
            Fn<void(__thiscall*)(void*)>(original, 0x004F3090)(c);
            Fn<void(__thiscall*)(void*)>(original, 0x004F2F40)(c);
            Fn<void(__thiscall*)(void*)>(original, 0x004F3090)(c);
            Fn<void(__thiscall*)(void*)>(original, 0x004F2F10)(c);
            c->m_vtable = reinterpret_cast<void**>(0x0064B830);
            trace.push_back(PedRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned)>(original, 0x004F2F50)(c, seed & 1)));
            // The proximity component.
            g_answers.callocFails = seed == 6;
            trace.push_back(PedRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004F3550)()));
            g_answers.callocFails = false;
            auto* p = reinterpret_cast<Obj::CProjectileProximityComponent*>(g_ped.projectile);
            FillRandom(g_ped.projectile, sizeof(g_ped.projectile), random);
            p->m_object = WorldObject();
            g_answers.component = [missing](uint32_t type) { return PedComponent(type, missing); };
            Fn<void(__thiscall*)(void*, void*)>(original, 0x004F3520)(p, FakeStruct(0));
            auto* skater = reinterpret_cast<Obj::CSkater*>(g_ped.skater);
            skater->m_id = 0x51;
            *reinterpret_cast<uint32_t*>(g_ped.core + 0x2FC) = 0xA770 + seed;
            for (int i = 0; i < 5; ++i) {
                uint8_t* projectile = reinterpret_cast<uint8_t*>(&skater->m_projectiles[i]);
                *reinterpret_cast<uint32_t*>(projectile) = 0x9A0 + static_cast<uint32_t>(i);
                projectile[4] = static_cast<uint8_t>(random.Below(2));
                auto* at = reinterpret_cast<float*>(projectile + 8);
                at[0] = WorldObject()->m_pos.x + static_cast<float>(random.Below(120)) - 60.0f;
                at[1] = WorldObject()->m_pos.y + 30.0f + static_cast<float>(random.Below(40)) - 20.0f;
                at[2] = WorldObject()->m_pos.z + static_cast<float>(random.Below(80)) - 40.0f;
            }
            if (seed % 5 == 1) { // (exactly on the edge: 48 away in x)
                WorldObject()->m_pos = {100.0f, 20.0f, -50.0f, 1.0f};
                auto* at = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&skater->m_projectiles[0]) + 8);
                at[0] = 148.0f;
                at[1] = 50.0f;
                at[2] = -50.0f;
                reinterpret_cast<uint8_t*>(&skater->m_projectiles[0])[4] = 1;
            }
            for (int step = 0; step < 8; ++step) {
                if (step == 0)
                    p->mp_skater = nullptr;
                if (step == 1)
                    p->mp_skater = skater;
                if (step == 2)
                    p->mp_pedLogic = nullptr;
                WorldObject()->m_id = step == 3 ? 0 : 0x0B1E0003;
                *reinterpret_cast<uint32_t*>(g_ped.ped + 0x24) = (step == 4 || seed % 6 == 5) ? 0x54F87744 : 0x1234;
                Fn<void(__thiscall*)(void*)>(original, 0x004F35B0)(p);
                trace.push_back(PedRelocate(reinterpret_cast<uint32_t>(p->mp_skater)));
                trace.push_back(PedRelocate(reinterpret_cast<uint32_t>(p->mp_pedLogic)));
            }
            Fn<void(__thiscall*)(void*)>(original, 0x004F34E0)(p);
            p->m_vtable = reinterpret_cast<void**>(0x0064B864);
            trace.push_back(PedRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned)>(original, 0x004F34F0)(p, seed & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            std::vector<uint32_t> world = PedWords();
            trace.insert(trace.end(), world.begin(), world.end());
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "ped logic: the projectile components");
    }
    g_relocate = nullptr;
    delete &s;
}

// A run's PedLogic world: the component (random bytes), its object (with tags and a script, or not), a script with
// its parameters, the fellow components' random bytes.
Obj::CPedLogicComponent* ResetPedWorld(Lcg& random, uint32_t seed)
{
    ResetComponentWorld(random);
    std::memset(&g_ped, 0, sizeof(g_ped));
    FillRandom(g_ped.ped, sizeof(g_ped.ped), random);
    FillRandom(g_ped.motion, sizeof(g_ped.motion), random);
    FillRandom(g_ped.looping, sizeof(g_ped.looping), random);
    FillRandom(g_ped.sound, sizeof(g_ped.sound), random);
    WorldObject()->m_vtable = g_objectVtable;
    WorldObject()->mp_tags = seed % 3 ? FakeStruct(2) : nullptr;
    WorldObject()->m_script = seed % 4 ? reinterpret_cast<Script::CScript*>(g_ped.script) : nullptr;
    *reinterpret_cast<CStruct**>(g_ped.script + 0x14) = FakeStruct(1);
    GameVariable<uint8_t*>(0x007CE4B4) = g_ped.manager;
    g_answers = Answers();
    g_pa = PedAnswers();
    auto* c = reinterpret_cast<Obj::CPedLogicComponent*>(g_ped.ped);
    c->m_object = WorldObject();
    c->mp_auxScript = nullptr;
    return c;
}

// The PedLogic component's making, settings, states and script member functions: made (and failing to be),
// constructed, destroyed (with and without an aux script); set up from its parameters (female or not, a board with
// and without desc_id, no profile); the movement started per update type; the no-move update rotating or not; the
// position history; the ground triangle copied; the sounds' state and terrain (each component there or not); the
// board hidden / shown from every state; the state parameters (and their structure failing to be made); the state
// set: no ped_init, the unique table or the common one, a state found in neither / only the common one, the old
// state's exit action (a script, a missing one, wait_anim_finished with a high-priority state and not), its
// hide_boardgeom and the new one's, an object script or none, an aux script killed; the aux script run, kept or
// deleted; the bail (blocked by the motion flag, the stored velocity given back or not); the profile's voice; every
// member function, each of their branches (the quirky defaults over `params` / `this` too).
void TestPedLogicCore()
{
    std::unique_ptr<GameMemory> memory(PedMemory());
    StandIns& s = *new StandIns();
    StandInComponentWorld(s);
    StandInPedWorld(s);
    // Global structures by name: the state tables (null for some).
    std::function<CStruct*(uint32_t)> table = [](uint32_t) { return nullptr; };
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [&table](Regs& r) {
        CStruct* found = table(r.args[0]);
        Record({0x9020, r.args[0], PedRelocate(reinterpret_cast<uint32_t>(found))});
        return reinterpret_cast<uint32_t>(found);
    });
    g_relocate = PedRelocate;
    for (void*& slot : g_objectVtable)
        slot = s.Thunk(0, [](Regs& r) { Record({0x0B1E, PedRelocate(r.ecx)}); return 0u; });
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x9D000000 + seed);
            Lcg random(0x9D010000 + seed);
            Obj::CPedLogicComponent* c = ResetPedWorld(random, seed);
            uint32_t missing = random.Below(32);
            g_answers.component = [missing](uint32_t type) { return PedComponent(type, missing & 15); };
            Lcg answers(0x9D020000 + seed);
            Originals guard(original, {0x004F0B10, 0x004EE4F0, 0x004F2120, 0x004F2540, 0x004ED2C0, 0x004ED460,
                                       0x004ED4A0, 0x004ED4F0, 0x004EDC10, 0x004EDC60, 0x004EDCB0, 0x004EDD00,
                                       0x004EFB10, 0x004F0B70, 0x004F2240, 0x004F1E10, 0x004EE2C0, 0x004F2040,
                                       0x004EDD80, 0x004F24B0, 0x004ED150, 0x004ED160, 0x004ED170, 0x004ED1D0,
                                       0x004ED220, 0x004ED2A0, 0x004EDDA0, 0x004EDDD0, 0x004EDE00, 0x004EDE20,
                                       0x004EDEA0, 0x004EE100, 0x004EE180, 0x004EE1B0, 0x004EE1E0, 0x004EE230,
                                       0x004F24E0, 0x004F20F0, 0x004EE280, 0x004F20E0, 0x004EE3A0});
            std::vector<uint32_t> trace;
            auto member = [&](uint32_t address, CStruct* params) {
                trace.push_back(Fn<uint8_t(__thiscall*)(void*, void*, void*)>(original, address)(c, params, g_ped.script));
                trace.push_back(c->m_flags);
            };
            // Made, constructed.
            g_answers.callocFails = seed == 2;
            trace.push_back(PedRelocate(Fn<uint32_t(__cdecl*)()>(original, 0x004F0B10)()));
            g_answers.callocFails = false;
            trace.push_back(PedRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004EE4F0)(c)));
            c->m_object = WorldObject();
            // Set up from its parameters.
            uint32_t skeleton = seed % 3 == 0 ? 0x8642CA2A : 0x1111;
            g_answers.getChecksum = [&answers, skeleton](uint32_t name, uint32_t* out) {
                if (answers.Below(5) == 0)
                    return false;
                *out = name == 0x09794932 ? skeleton : 0x0D000000 + (name & 0xFFFF);
                return true;
            };
            g_pa.getStructure = [&answers](uint32_t, uint32_t name, CStruct** out) {
                if (answers.Below(4) == 0)
                    return false;
                *out = FakeStruct(static_cast<int>(name % 3));
                return true;
            };
            g_answers.getFloat = [&answers](uint32_t name, float* out) {
                if (answers.Below(3) == 0)
                    return false;
                *out = static_cast<float>(name % 1000) * 0.125f;
                return true;
            };
            for (int i = 0; i < 3; ++i) {
                c->m_flags = 0;
                Fn<void(__thiscall*)(void*, void*)>(original, 0x004ED2C0)(c, FakeStruct(0));
                trace.push_back(c->m_female);
                trace.push_back(c->m_flags);
            }
            // The movement, the no-move update, the history, the triangle.
            static const uint32_t kUpdates[] = {0x5B4E8427, 0xA631E964, 0x520A0B40, 0};
            for (uint32_t update : kUpdates) {
                c->m_updateType = update;
                c->m_flags = random.Next();
                Fn<void(__thiscall*)(void*)>(original, 0x004ED460)(c);
                trace.push_back(c->m_flags);
            }
            for (int i = 0; i < 2; ++i) {
                g_pa.rotating = [i] { return i == 0; };
                FillRandom(reinterpret_cast<uint8_t*>(&c->m_savedMatrix), sizeof(Mth::Matrix), random);
                Fn<void(__thiscall*)(void*)>(original, 0x004ED4A0)(c);
                trace.push_back(HashFloats(&c->m_savedMatrix, 16));
                trace.push_back(HashFloats(&WorldObject()->m_displayMatrix, 16));
            }
            for (int i = 0; i < 3; ++i) {
                WorldObject()->m_pos.x += 1.5f;
                Fn<void(__thiscall*)(void*)>(original, 0x004ED4F0)(c);
            }
            alignas(16) uint8_t triangle[0x40];
            FillRandom(triangle, sizeof(triangle), random);
            trace.push_back(PedRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x004EDC10)(
                g_ped.aux, triangle)));
            trace.push_back(HashBytes(g_ped.aux, 0x40));
            // The sounds.
            for (int i = 0; i < 4; ++i) {
                *reinterpret_cast<int*>(g_ped.looping + 0x74) = i & 1;
                g_ped.looping[0x60] = static_cast<uint8_t>(i & 2);
                Fn<void(__thiscall*)(void*, int)>(original, 0x004EDC60)(c, i == 3 ? -1 : 1);
                Fn<void(__thiscall*)(void*, int)>(original, 0x004EDCB0)(c, i == 2 ? 0x102 : 2);
            }
            // The board.
            for (uint32_t flags : {0u, 1u, 0x20001u, 0x20000u}) {
                for (int hide : {0, 1, 2}) {
                    c->m_flags = flags;
                    trace.push_back(Fn<uint8_t(__thiscall*)(void*, int)>(original, 0x004EDD00)(c, hide));
                    trace.push_back(c->m_flags);
                }
            }
            // The state parameters.
            for (int i = 0; i < 2; ++i) {
                g_pa.newFails = i == 1 && seed % 2;
                trace.push_back(PedRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x004EFB10)(c, FakeStruct(1))));
            }
            g_pa.newFails = false;
            // The state.
            uint32_t tablesMask = answers.Below(8);
            table = [tablesMask](uint32_t name) -> CStruct* {
                if (name == 0x51F1BB49)
                    return tablesMask & 1 ? nullptr : FakeStruct(0);
                return tablesMask & 2 ? nullptr : FakeStruct(1);
            };
            g_pa.integerTag = [&answers](uint32_t, int* out) {
                int k = static_cast<int>(answers.Below(6));
                if (k == 0)
                    return false;
                *out = k == 1 ? 0 : k;
                return true;
            };
            g_pa.checksumTag = [&answers](uint32_t, uint32_t* out) {
                if (answers.Below(2) == 0)
                    return false;
                *out = 0x7AB1E000;
                return true;
            };
            g_pa.containsFlag = [&answers](uint32_t, uint32_t) { return answers.Below(3) == 0; };
            g_pa.scriptExists = [&answers](uint32_t) { return answers.Below(2) == 0; };
            g_pa.gotScript = [&answers] { return answers.Below(2) == 0; };
            g_pa.getStructure = [&answers](uint32_t, uint32_t name, CStruct** out) {
                if (answers.Below(4) == 0)
                    return false;
                *out = FakeStruct(static_cast<int>(name % 3));
                return true;
            };
            g_answers.getChecksum = [&answers](uint32_t name, uint32_t* out) {
                uint32_t k = answers.Below(6);
                if (k == 0)
                    return false;
                if (name == 0xC1868052) // exit_state_action
                    *out = k == 1 ? 0x2E64106C : 0xE0E0E000 + k;
                else
                    *out = 0x0D000000 + (name & 0xFFFF) + k;
                return true;
            };
            static const uint32_t kTables[] = {0, 0x51F1BB49, 0x46196541};
            for (int i = 0; i < 12; ++i) {
                c->m_state = i % 4 == 0 ? 0xDA3403B0 : 0x5A7E0000 + static_cast<uint32_t>(i);
                c->m_stateTable = 0x7AB1E000 + static_cast<uint32_t>(i % 3);
                c->mp_auxScript = i % 3 == 1 ? reinterpret_cast<Script::CScript*>(g_ped.aux) : nullptr;
                c->m_flags = random.Next();
                bool set = Fn<uint8_t(__thiscall*)(void*, uint32_t, uint32_t, void*)>(original, 0x004F0B70)(
                    c, 0x57A7E000 + static_cast<uint32_t>(i), kTables[i % 3], FakeStruct(2));
                trace.push_back(set);
                trace.push_back(c->m_state);
                trace.push_back(c->m_stateTable);
                trace.push_back(c->m_updateType);
                trace.push_back(c->m_flags);
                trace.push_back(PedRelocate(reinterpret_cast<uint32_t>(c->mp_auxScript)));
            }
            // The aux script.
            for (int i = 0; i < 6; ++i) {
                c->mp_auxScript = i % 2 ? reinterpret_cast<Script::CScript*>(g_ped.aux) : nullptr;
                g_pa.newFails = i == 4;
                Fn<void(__thiscall*)(void*, uint32_t, void*)>(original, 0x004F2240)(c, i == 5 ? 0 : 0x5C000 + i,
                                                                                     FakeStruct(1));
                trace.push_back(PedRelocate(reinterpret_cast<uint32_t>(c->mp_auxScript)));
            }
            g_pa.newFails = false;
            // The bail.
            for (int i = 0; i < 6; ++i) {
                *reinterpret_cast<uint32_t*>(g_ped.motion + 0x18) = i == 0 ? 0x100 : 0x2FF;
                c->m_storedMaxVelocity = i == 2 ? -1.0f : i == 3 ? std::numeric_limits<float>::quiet_NaN()
                                                         : i == 4 ? 0.0f : 7.5f * static_cast<float>(i);
                c->m_flags = i % 2 ? 0x604 : 0x1F9;
                if (!(missing & 1)) {
                    trace.push_back(Fn<uint8_t(__thiscall*)(void*)>(original, 0x004F1E10)(c));
                    trace.push_back(c->m_flags);
                }
            }
            // The profile's voice.
            uint32_t voiceAt = answers.Below(5);
            g_pa.arrayElement = [](uint32_t index) { return reinterpret_cast<uint32_t>(FakeStruct(static_cast<int>(index % 3))); };
            g_answers.getChecksum = [&answers, voiceAt](uint32_t name, uint32_t* out) {
                if (name == 0x1804A7C4 && answers.Below(3) == 0) // voice
                    return false;
                *out = name == 0x40C698AF ? 0x7000 + voiceAt : 0x7002; // id; the voice / the profile name
                return true;
            };
            g_pa.getInteger = [&answers](uint32_t, uint32_t, int* out) {
                *out = static_cast<int>(answers.Below(3));
                return true;
            };
            g_pa.getString = [&answers](uint32_t, uint32_t, const char** out) {
                if (answers.Below(4) == 0)
                    return false;
                *out = answers.Below(2) ? "voice_a" : nullptr;
                return true;
            };
            for (int i = 0; i < 4; ++i) {
                g_pa.arraySize = static_cast<uint32_t>(i + 1);
                const char* voice = Fn<const char*(__cdecl*)()>(original, 0x004EE2C0)();
                trace.push_back(Hash(voice));
            }
            // The member functions.
            bool noPercent = false;
            g_answers.getFloat = [&answers, &noPercent](uint32_t name, float* out) {
                if (answers.Below(3) == 0 || (noPercent && name == 0x9E497FC6))
                    return false;
                static const float kValues[] = {50.0f, 2000.0f, -10.0f, 0.0f, 1099.5f};
                *out = kValues[answers.Below(5)] + static_cast<float>(name & 3);
                return true;
            };
            g_answers.getChecksum = [&answers](uint32_t name, uint32_t* out) {
                uint32_t k = answers.Below(5);
                if (k == 0)
                    return false;
                *out = name == 0x17140E6D ? (k == 1 ? 0x0A7BE964 : 0x7000 + k) : name == 0xA1DC81F9 ? 0x7000 + k
                                                                                          : 0x5A7E0000 + k;
                return true;
            };
            g_pa.getInteger = [&answers](uint32_t, uint32_t, int* out) {
                uint32_t k = answers.Below(4);
                if (k == 0)
                    return false; // (the quirky defaults stay)
                *out = static_cast<int>(k) - 1;
                return true;
            };
            g_pa.containsFlag = [&answers](uint32_t, uint32_t) { return answers.Below(2) == 0; };
            for (int i = 0; i < 6; ++i) {
                c->m_flags = random.Next();
                c->m_state = i % 2 ? 0x5A7E0001 : 0x1234;
                static const float kMaxSpeeds[] = {-100.0f, 880.0f, 500.0f, 880.0f, 1100.0f, 1400.0f};
                *reinterpret_cast<float*>(g_ped.motion + 0x2C) = kMaxSpeeds[i];
                noPercent = i % 2 == 1;
                c->m_female = static_cast<uint32_t>(i % 3);
                c->mp_auxScript = i % 2 ? reinterpret_cast<Script::CScript*>(g_ped.aux) : nullptr;
                member(0x004F2040, FakeStruct(0));
                member(0x004EDD80, FakeStruct(0));
                member(0x004F24B0, FakeStruct(0));
                member(0x004ED160, FakeStruct(0));
                member(0x004ED150, FakeStruct(0));
                member(0x004ED160, FakeStruct(0));
                member(0x004ED170, FakeStruct(0));
                member(0x004ED1D0, FakeStruct(0));
                member(0x004ED220, FakeStruct(0));
                member(0x004ED2A0, FakeStruct(0));
                if (!(missing & 1)) {
                    member(0x004EDDA0, FakeStruct(0));
                    member(0x004EDDD0, FakeStruct(0));
                    member(0x004F20E0, FakeStruct(0));
                }
                member(0x004EDE00, FakeStruct(0));
                member(0x004EDE20, FakeStruct(0));
                trace.push_back(*reinterpret_cast<uint32_t*>(g_ped.motion + 0x2C));
                member(0x004EDEA0, FakeStruct(0));
                member(0x004EE100, FakeStruct(0));
                trace.push_back(c->m_kind);
                member(0x004EE180, FakeStruct(0));
                member(0x004EE1B0, FakeStruct(0));
                member(0x004EE1E0, FakeStruct(0));
                member(0x004EE230, FakeStruct(0));
                member(0x004F24E0, FakeStruct(0));
                member(0x004F20F0, FakeStruct(0));
                member(0x004EE280, FakeStruct(0));
                g_answers.node = [i](uint32_t) { return i % 3 ? FakeStruct(1) : nullptr; };
                member(0x004EE3A0, FakeStruct(0));
                c->m_flags = random.Next();
            }
            // Destroyed.
            c->mp_auxScript = seed % 2 ? reinterpret_cast<Script::CScript*>(g_ped.aux) : nullptr;
            Fn<void(__thiscall*)(void*)>(original, 0x004F2120)(c);
            c->m_vtable = reinterpret_cast<void**>(0x0064B7A8);
            c->mp_auxScript = nullptr;
            trace.push_back(PedRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned)>(original, 0x004F2540)(c, seed & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            std::vector<uint32_t> world = PedWords();
            trace.insert(trace.end(), world.begin(), world.end());
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "ped logic: the component's making, states and member functions");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestProjectileComponents, RemoveStandIns);
SELFTEST(Early, TestPedLogicCore, RemoveStandIns);
