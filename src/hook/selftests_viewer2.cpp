// Self-tests for cars made in code and the King of the Hill crown (src/game/obj/game_objects.cpp) and the skater trick
// code's list entries (src/game/skater/trick_lists.cpp): the original and ours run the same calls on identical
// stand-in objects in an arena; what they call outside (the composite object's construction and components, the
// object manager, scripts, structures, arrays, the skeleton, the model, memory) is stood in for by recording code
// (viewer_testkit.h), and the calls and the whole arena are compared.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/string.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/buddy_status.h"
#include "game/obj/car_physics.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/model_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/nodes.h"
#include "game/skater/trick_lists.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/skeleton.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;

// Arena layout.
constexpr uint32_t kObject = 0x0000;     // the object under test (0x114)
constexpr uint32_t kSkater = 0x0200;     // the crown's skater (0x108)
constexpr uint32_t kComponents = 0x0400; // components, 0x100 apart by type
constexpr uint32_t kManager = 0x0C00;    // the composite object manager (its vtable slot 1 recorded)
constexpr uint32_t kParams = 0x0D00;
constexpr uint32_t kEntries = 0x0E00;    // trick list entries
constexpr uint32_t kVtables = 0x1000;    // stand-in vtables

void* g_objectVtable[12];
void* g_managerVtable[4];
void* g_entryVtable[2];

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0x0A015E17: return At(kComponents);         // motion
    case 0x286A8D26: return At(kComponents + 0x100); // model
    case 0x222756D5: return At(kComponents + 0x300); // skeleton
    case 0xCE0CA665: return At(kComponents + 0x400); // suspend
    default: return At(kComponents + 0x500);
    }
}

Mth::Matrix* __stdcall SameMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x0045E510, AddressOf(&Obj::CCompositeObject::Construct)}, 0, [](Regs& r) {
        Rec(0xE510, {r.ecx});
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(r.ecx);
        object->m_vtable = g_objectVtable;
        object->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
        return r.ecx;
    });
    s.At({0x0045D890, AddressOf(&Obj::CCompositeObject::DestroyBody)}, 0, [](Regs& r) {
        Rec(0xD890, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0u;
    });
    s.At({0x0057B250, AddressOf(&Obj::AddBaseComponents)}, 0, [](Regs& r) {
        Rec(0xB250, {r.args[0]});
        return 0u;
    });
    s.At({0x0057B2F0, AddressOf(&Obj::AddMotionComponents)}, 0, [](Regs& r) {
        Rec(0xB2F0, {r.args[0]});
        return 0u;
    });
    s.At({0x0057B420, AddressOf(&Obj::AddNodeComponents)}, 0, [](Regs& r) {
        Rec(0xB420, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00461930, AddressOf(&Obj::CObject::SetID)}, 4, [](Regs& r) {
        Rec(0x1930, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0041BAD0, AddressOf(&Obj::CCarPhysicsComponent::Construct)}, 0, [](Regs& r) {
        Rec(0xBAD0, {r.ecx});
        return r.ecx;
    });
    s.At({0x0045DA30, AddressOf(&Obj::CCompositeObject::AddComponent)}, 4, [](Regs& r) {
        Rec(0xDA30, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return ComponentFor(r.args[0]);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Rec(0xC050, {r.args[0], r.args[1]});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 10.5f, v[1] = -20.25f, v[2] = 30.0f, v[3] = 1.0f;
             return 1u;
         });
    s.At({0x00423C90, AddressOf(&Obj::CMotionComponent::InitPathFollower)}, 4, [](Regs& r) {
        Rec(0x3C90, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00423510, AddressOf(&Obj::CMotionPathMembers::GetPathFollower)}, 0, [](Regs& r) {
        Rec(0x3510, {r.ecx});
        return At(kComponents + 0x600);
    });
    s.At({0x00420480, AddressOf(&Obj::CModelComponent::Teleport)}, 0, [](Regs& r) {
        Rec(0x0480, {r.ecx});
        return 0u;
    });
    s.At({0x004879D0, AddressOf(&Gfx::MakeModelPath)}, 0, [](Regs& r) {
        Rec(0x79D0, {Text(r.args[1]), Text(r.args[2])});
        new (reinterpret_cast<void*>(r.args[0])) Str::String("models/crown/crown.mdl");
        return r.args[0];
    });
    s.At({0x004935E0, AddressOf(&Nx::CModel::AddFileGeom)}, 32, [](Regs& r) {
        Rec(0x35E0, {r.ecx, Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7]});
        return 0u;
    });
    s.At({0x00493C60, AddressOf(&Nx::CModel::SetActive)}, 4, [](Regs& r) {
        Rec(0x3C60, {r.ecx, r.args[0]});
        return 0u;
    });
    // The crown's update.
    s.At({0x005480F0, AddressOf(&GameNet::FoldedGetters::Word18)}, 0, [](Regs& r) {
        Rec(0x80F0, {r.ecx});
        return At(kComponents + 0x700);
    });
    s.At({0x004A36E0, AddressOf(&Gfx::CSkeleton::GetBoneMatrix)}, 8, [](Regs& r) {
        Rec(0x36E0, {r.ecx, r.args[0], r.args[1]});
        auto* m = reinterpret_cast<float*>(r.args[1]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(std::cos(i * 0.37 + 1.0) * 3.0);
        return 0u;
    });
    s.At({0x00404DD0, AddressOf(&Mth::CreateRotateXMatrix)}, 0, [](Regs& r) {
        Rec(0x4DD0, {r.args[1]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(std::sin(i * 0.61) + (i % 5 == 0));
        return r.args[0];
    });
    s.Bytes(0x005BFA8C, Branch(0xE9, 0x005BFA8C, reinterpret_cast<void*>(&SameMultiply)));
    // Arrays.
    s.At({0x0046CAA0, AddressOf(&Script::CArray::Init)}, 0, [](Regs& r) {
        Rec(0xCAA0, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 12);
        return r.ecx;
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        Rec(0x7140, {r.args[0]});
        return 0u;
    });
    s.At({0x0046CAB0, AddressOf(&Script::CArray::Clear)}, 0, [](Regs& r) {
        Rec(0xCAB0, {r.ecx});
        return 0u;
    });
    s.At({0x00408FD0, AddressOf(&Script::DeleteArray)}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        return 0u;
    });
    // The stand-in vtables: the object's Finalize (+0x20) and CreateComponentFromStructure (+0x24), the manager's
    // RegisterObject (+0x04).
    g_objectVtable[8] = s.Thunk(0, [](Regs& r) {
        Rec(0xF1A1, {r.ecx});
        return 0u;
    });
    g_objectVtable[9] = s.Thunk(8, [](Regs& r) {
        Rec(0xC0C0, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    g_managerVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x6E61, {r.ecx, r.args[0]});
        return 0u;
    });
    g_entryVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0xDE1E, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

void SaveObjectGlobals() { SaveGlobals({{0x006F0FB8, 4, {}}, {0x00690FA0, 4, {}}}); }

// Cars (made, set up), the crown (constructed, set up from a node, put on and taken off, its update worn and not, the
// destructors).
void TestGameObjects()
{
    SaveObjectGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        GameVariable<int>(0x00690FA0) = 40;
        std::vector<uint32_t>& t = traces[run];
        *Ptr<void**>(kManager) = g_managerVtable;
        GameVariable<uint32_t>(0x006F0FB8) = At(kManager);
        auto* params = Ptr<CStruct>(kParams);
        // A car.
        if (original)
            Original<void(__cdecl*)(CStruct*)>(0x00578170)(params);
        else
            Obj::CreateCar(params);
        auto* car = Ptr<Obj::CCompositeObject>(0x1400);
        car->m_vtable = g_objectVtable;
        if (original)
            Original<void(__cdecl*)(Obj::CCompositeObject*, CStruct*)>(0x005780E0)(car, params);
        else
            Obj::InitCar(car, params);
        // The crown.
        auto* crown = Ptr<Obj::CCrownObject>(kObject);
        std::memset(crown, 0xCD, sizeof(Obj::CCrownObject)); // (so the constructor's every field shows)
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00578240)(crown)
                                                            : crown->Construct())));
        crown->m_vtable = g_objectVtable;
        if (original)
            Original<void(__thiscall*)(void*, void*, CStruct*)>(0x00578490)(crown, Ptr<void>(kManager), params);
        else
            crown->InitFromNode(Ptr<Obj::CCompositeObjectManager>(kManager), params);
        auto* skater = Ptr<Obj::CCompositeObject>(kSkater);
        for (int i = 0; i < 16; ++i)
            (&skater->m_matrix.row[0].x)[i] = static_cast<float>(std::sin(i * 1.7) * 2.5);
        skater->m_pos = {100.5f, -3.25f, 77.0f, 1.0f};
        auto update = [&]() {
            if (original)
                Original<void(__thiscall*)(void*)>(0x00578310)(crown);
            else
                crown->OwnUpdate();
            const auto* words = Ptr<uint32_t>(kObject + 0x4C);
            t.insert(t.end(), words, words + (0xAC - 0x4C) / 4);
        };
        auto worn = [&]() {
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x005782E0)(crown) : crown->IsWorn());
        };
        update();
        worn();
        if (original)
            Original<void(__thiscall*)(void*, void*)>(0x005782B0)(crown, skater);
        else
            crown->PutOn(skater);
        worn();
        update();
        skater->m_pos = {-1.0f, 0.5f, 9.75f, 0.0f};
        update();
        if (original)
            Original<void(__thiscall*)(void*)>(0x005782D0)(crown);
        else
            crown->TakeOff();
        worn();
        update();
        crown->m_worn = true;
        if (original)
            Original<void(__thiscall*)(void*)>(0x005782F0)(crown);
        else
            crown->DestroyBody();
        for (unsigned int flags : {0u, 1u}) {
            crown->m_worn = true;
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x00578460)(crown, flags)
                         : crown->Destroy(flags))));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
        t.push_back(GameVariable<uint32_t>(0x00690FA0));
    }
    Cleanup();
    Compare(traces[0], traces[1], "cars, the crown");
}

// The trick code's list entries: made (in a list of two, unlinked), the flag, destroyed (with and without an array,
// both scalar destructors with and without freeing).
void TestTrickLists()
{
    SaveObjectGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        GameVariable<int>(0x00690FA0) = 7;
        std::vector<uint32_t>& t = traces[run];
        std::memset(Ptr<void>(kEntries), 0xCD, 0x1C0); // (so the constructors' every field shows)
        // (but the names' capacity: the original String constructor leaves it, ours clears it - see string.cpp)
        *Ptr<uint32_t>(kEntries + 0x100 + 0x1C) = 0;
        *Ptr<uint32_t>(kEntries + 0x140 + 0x1C) = 0;
        auto made = [&](uint32_t offset, bool named) {
            void* entry = Ptr<void>(kEntries + offset);
            void* result;
            if (named)
                result = original ? Original<void*(__thiscall*)(void*)>(0x005787F0)(entry)
                                  : static_cast<Obj::TrickNameEntry*>(entry)->Construct();
            else
                result = original ? Original<void*(__thiscall*)(void*)>(0x00578740)(entry)
                                  : static_cast<Obj::TrickListEntry*>(entry)->Construct();
            t.push_back(Rel(reinterpret_cast<uint32_t>(result)));
            const auto* words = Ptr<uint32_t>(kEntries + offset); // (before the destructors overwrite it)
            for (int i = 0; i < 0x64 / 4; ++i)
                t.push_back(Rel(words[i]));
        };
        made(0x000, false);
        made(0x080, false);
        made(0x100, true);
        made(0x140, true);
        auto* a = Ptr<Obj::TrickListEntry>(kEntries);
        auto* b = Ptr<Obj::TrickListEntry>(kEntries + 0x80);
        // b linked after a.
        a->m_node.next = &b->m_node;
        a->m_node.prev = &b->m_node;
        b->m_node.next = &a->m_node;
        b->m_node.prev = &a->m_node;
        if (original)
            Original<void(__thiscall*)(void*)>(0x00578670)(a);
        else
            a->SetFlag();
        if (original)
            Original<void(__thiscall*)(void*)>(0x00579570)(a);
        else
            a->DestroyBody();
        b->m_array = nullptr;
        for (unsigned int flags : {1u, 0u}) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x00579820)(b, flags)
                                                                : b->Destroy(flags))));
            b->m_array = Ptr<Script::CArray>(kEntries + 0x1C0);
        }
        auto* c = Ptr<Obj::TrickNameEntry>(kEntries + 0x100);
        auto* d = Ptr<Obj::TrickNameEntry>(kEntries + 0x140);
        c->m_name = "kickflip";
        d->m_node.next = d->m_node.prev = &c->m_node;
        c->m_node.next = c->m_node.prev = &d->m_node;
        if (original)
            Original<void(__thiscall*)(void*)>(0x00579630)(c);
        else
            c->DestroyBody();
        for (unsigned int flags : {0u, 1u})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x00579600)(d, flags)
                                                                : d->Destroy(flags))));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
        t.push_back(GameVariable<uint32_t>(0x00690FA0));
    }
    Cleanup();
    Compare(traces[0], traces[1], "trick list entries");
}

} // namespace

SELFTEST(Early, TestGameObjects, ViewerTestKit::Cleanup);
SELFTEST(Early, TestTrickLists, ViewerTestKit::Cleanup);
