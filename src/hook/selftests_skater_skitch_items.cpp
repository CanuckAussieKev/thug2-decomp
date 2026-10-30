// Self-tests for the skitch and special item components (game/skater/skitch.cpp, special_item.cpp): the originals
// against ours, the calls they make recorded by stand-ins (hook/standins.h).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skitch.h"
#include "game/skater/special_item.h"
#include "game/gfx/model.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    float Range(float low, float high) { return low + (high - low) * static_cast<float>(Below(10001)) / 10000.0f; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

alignas(16) uint8_t g_component[0x40];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_hookManager[0x40];
alignas(16) uint8_t g_nodeList[sizeof(Obj::CClassNodeList)];
alignas(16) Obj::CClassNodeList::Entry g_entries[6];
alignas(16) uint8_t g_items[3][0x200];      // item objects (made by CreateGameObjectFromNode)
alignas(16) uint8_t g_models[4][0x20];      // model components (+0x208 points at model data)
alignas(16) uint8_t g_modelData[4][0x300];
alignas(16) uint8_t g_locks[3][0x20];       // the items' lock components
alignas(16) uint8_t g_structs[16][0x10];
alignas(16) uint8_t g_block[2][0x40];
alignas(16) uint8_t g_netManager[0x40];
alignas(16) uint8_t g_client[0x40];
void* g_fakeVtable[16];
int g_nextStruct, g_blockNext;
bool g_containsOff, g_haveNodes, g_inNetGame, g_wholeTarget;
bool g_found[8];
uint32_t g_next;

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, size_t each, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        if (p >= b && p < b + size)
            return tag + static_cast<uint32_t>((p - b) / each) * 0x1000 + static_cast<uint32_t>((p - b) % each);
        return 0;
    };
    if (!p)
        return 0;
    for (auto r : {in(g_component, sizeof(g_component), sizeof(g_component), 0x61000000),
                   in(g_object, sizeof(g_object), sizeof(g_object), 0x62000000),
                   in(g_items, sizeof(g_items), sizeof(g_items[0]), 0x63000000),
                   in(g_structs, sizeof(g_structs), sizeof(g_structs[0]), 0x64000000),
                   in(g_modelData, sizeof(g_modelData), sizeof(g_modelData[0]), 0x65000000),
                   in(g_locks, sizeof(g_locks), sizeof(g_locks[0]), 0x66000000),
                   in(g_block, sizeof(g_block), sizeof(g_block[0]), 0x67000000),
                   in(g_client, sizeof(g_client), sizeof(g_client), 0x68000000),
                   in(g_nodeList, sizeof(g_nodeList), sizeof(g_nodeList), 0x69000000)})
        if (r)
            return r;
    return 0xBAD00000;
}

void StandInScript(StandIns& s)
{
    using Script::CStruct;
    auto newStruct = [](Regs&) {
        Record({0x4E5});
        return reinterpret_cast<uint32_t>(g_structs[g_nextStruct++ & 15]);
    };
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, newStruct);
    s.At({0x005BA3D0}, 0, newStruct); // (the original's inlined NewStruct: the pool's Allocate)
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) { Record({0x1417, Id(r.ecx)}); return r.ecx; });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0xC1EA, Id(r.ecx)}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0xDE1, Id(r.args[0])}); return 0u; });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             bool found = g_found[(r.args[0] ^ (r.args[0] >> 9)) & 7];
             Record({0x6FC5, Id(r.ecx), r.args[0], r.args[2], found});
             if (found)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = r.args[0] * 7 + 3;
             return static_cast<uint32_t>(found);
         });
    // (the original calls 0x00476CB0, the bool-assert form)
    s.At({0x00476CB0, 0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             bool found = g_found[(r.args[0] >> 3) & 7];
             Record({0x6F17, Id(r.ecx), r.args[0], r.args[2] & 0xFF, found});
             if (found)
                 *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(g_next % 2);
             return static_cast<uint32_t>(found);
         });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0xADDC, Id(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0xA9E, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x6E57, r.args[0]});
        return reinterpret_cast<uint32_t>(g_structs[14]);
    });
    s.At({0x00476BA0, AddressOf(static_cast<bool (CStruct::*)(const char*)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0xF1A6, Id(r.ecx), r.args[0]});
        return static_cast<uint32_t>(g_containsOff);
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5C41, r.args[0], Id(r.args[1]), Id(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
}

void StandInObjects(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA11, r.args[0]});
        return g_blockNext < 0 ? 0u : reinterpret_cast<uint32_t>(g_block[g_blockNext]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) { Record({0xF5EE, Id(r.args[0])}); return 0u; });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD7, Id(r.ecx), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0)});
        return 0u;
    });
    s.At({0x00460820, AddressOf(&Obj::CObject::CopyTagsTo)}, 4, [](Regs& r) {
        Record({0x7A65, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x004616F0, AddressOf(&Obj::CObject::SetTagsFromStruct)}, 4, [](Regs& r) {
        Record({0x7A66, Id(r.ecx), Id(r.args[0])});
        return 0u;
    });
    s.At({0x00468EB0, AddressOf(&Obj::CreateGameObjectFromNode)}, 0, [](Regs& r) {
        Record({0x68EB, Id(r.args[0])});
        return reinterpret_cast<uint32_t>(g_items[g_next++ % 3]);
    });
    s.At({0x00492EE0, AddressOf(&Nx::CModel::SetScale)}, 4, [](Regs& r) {
        const uint32_t* colour = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x492E, Id(r.ecx), colour[0], colour[1], colour[2], colour[3]});
        return 1u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0x44E4, r.ecx == reinterpret_cast<uint32_t>(g_netManager)});
        return static_cast<uint32_t>(g_inNetGame);
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* msg = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
        const uint8_t* data = static_cast<const uint8_t*>(msg->m_data);
        Record({0x459E, Id(r.ecx), msg->m_id, msg->m_length, static_cast<uint32_t>(msg->m_priority),
                static_cast<uint32_t>(msg->m_queue), msg->m_groupId});
        if (msg->m_id == 0x77) // (the last two bytes are left over on the original's stack)
            Record({At<uint32_t>(const_cast<uint8_t*>(data), 0), At<uint32_t>(const_cast<uint8_t*>(data), 4), data[8], data[9]});
        else // (the first eight too)
            Record({data[8], data[9]});
        return 0u;
    });
    // The fake vtables' slots: MarkAsDead (6) of the items, InitFromStructure (2) of the locks, Suspend (7) of the
    // component, the node list (13) of the hook manager.
    g_fakeVtable[6] = s.Thunk(0, [](Regs& r) { Record({0xDEAD, Id(r.ecx)}); return 0u; });
    g_fakeVtable[2] = s.Thunk(4, [](Regs& r) { Record({0x10C, Id(r.ecx), Id(r.args[0])}); return 0u; });
    g_fakeVtable[7] = s.Thunk(4, [](Regs& r) { Record({0x5B5, Id(r.ecx), r.args[0] & 0xFF}); return 0u; });
    g_fakeVtable[13] = s.Thunk(0, [](Regs& r) {
        Record({0x4D, r.ecx == reinterpret_cast<uint32_t>(g_hookManager)});
        return g_haveNodes ? reinterpret_cast<uint32_t>(g_nodeList) : 0u;
    });
}

// An object with components (`types` / `parts`, last first).
void Link(uint8_t* object, std::initializer_list<std::pair<uint8_t*, uint32_t>> parts)
{
    uint8_t* next = nullptr;
    for (auto part : parts) {
        At<uint32_t>(part.first, 4) = part.second;
        At<uint8_t*>(part.first, 0x10) = next;
        next = part.first;
    }
    At<uint8_t*>(object, 0x100) = next;
}

void BuildWorld(Random& r)
{
    for (size_t i = 0; i < sizeof(g_component); ++i)
        g_component[i] = static_cast<uint8_t>(r.Next());
    std::memset(g_object, 0, sizeof(g_object));
    std::memset(g_items, 0, sizeof(g_items));
    At<void**>(g_component, 0) = g_fakeVtable;
    At<uint8_t*>(g_component, 0x0C) = g_object;
    At<uint32_t>(g_object, 0x14) = 0x30 + r.Below(0x30);
    for (int i = 0; i < 3; ++i) {
        At<void**>(g_items[i], 0) = g_fakeVtable;
        At<uint32_t>(g_items[i], 0x14) = 0x100 + i;
        At<void**>(g_locks[i], 0) = g_fakeVtable;
        Link(g_items[i], {{g_locks[i], 0x5CB5CB7E}, {g_models[1 + i], r.OneIn(4) ? 0x1234u : 0x286A8D26u}});
        At<uint8_t*>(g_models[1 + i], 0x208) = r.OneIn(4) ? nullptr : g_modelData[1 + i];
    }
    At<uint8_t*>(g_models[0], 0x208) = r.OneIn(4) ? nullptr : g_modelData[0];
    for (int k = 0; k < 4; ++k)
        At<uint32_t>(g_modelData[0], 0x1C + 4 * k) = r.Next();
    Link(g_object, {{g_hookManager, r.OneIn(5) ? 0x1u : 0x27443708u}, {g_models[0], r.OneIn(4) ? 0x2u : 0x286A8D26u}});
    At<void**>(g_hookManager, 0) = g_fakeVtable;
    // The object's place and the car's hook nodes.
    float angle = r.Range(0.0f, 6.0f), tilt = r.Range(-0.4f, 0.4f);
    float* m = reinterpret_cast<float*>(g_object + 0x6C);
    float ca = std::cos(angle), sa = std::sin(angle), ct = std::cos(tilt), st = std::sin(tilt);
    const float rows[16] = {ca, 0.0f, -sa, r.Range(-1.0f, 1.0f), sa * st, ct, ca * st, r.Range(-1.0f, 1.0f),
                            sa * ct, -st, ca * ct, r.Range(-1.0f, 1.0f), r.Range(-9.0f, 9.0f), 2.0f, 3.0f, 0.5f};
    std::memcpy(m, rows, sizeof(rows));
    float pos[4] = {r.Range(-300.0f, 300.0f), r.Range(-20.0f, 20.0f), r.Range(-300.0f, 300.0f), r.OneIn(2) ? 1.0f : 0.25f};
    std::memcpy(g_object + 0x4C, pos, 16);
    auto* list = reinterpret_cast<Obj::CClassNodeList*>(g_nodeList);
    std::memset(g_nodeList, 0x5A, sizeof(g_nodeList));
    int count = static_cast<int>(r.Below(7));
    list->m_entries = count ? &g_entries[0] : nullptr;
    for (int i = 0; i < 6; ++i) {
        g_entries[i].pos = {r.Range(-40.0f, 40.0f), r.Range(-5.0f, 15.0f), r.Range(-60.0f, 60.0f), 1.0f};
        if (i > 0 && r.OneIn(5))
            g_entries[i].pos = g_entries[i - 1].pos; // (equally near: the first wins)
        g_entries[i].next = i + 1 < count ? &g_entries[i + 1] : nullptr;
    }
    list->m_transformed = r.OneIn(8);
    if (r.OneIn(3)) {
        // Whole numbers and an upright car: exact distances, so equally near points tie exactly.
        const float upright[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(m, upright, sizeof(upright));
        for (int k = 0; k < 3; ++k)
            reinterpret_cast<float*>(g_object + 0x4C)[k] = static_cast<float>(static_cast<int>(r.Below(21)) - 10);
        for (int i = 0; i < 6; ++i) {
            g_entries[i].pos = {static_cast<float>(static_cast<int>(r.Below(9)) - 4), 0.0f,
                                static_cast<float>(static_cast<int>(r.Below(9)) - 4), 1.0f};
            if (i > 0 && r.OneIn(3))
                g_entries[i].pos = g_entries[i - 1].pos;
        }
        list->m_transformed = false;
        g_wholeTarget = true;
    } else {
        g_wholeTarget = false;
    }
    g_haveNodes = !r.OneIn(8);
    g_containsOff = r.Bit();
    g_inNetGame = r.Bit();
    for (bool& f : g_found)
        f = !r.OneIn(3);
    g_nextStruct = 0;
    g_next = r.Below(3);
    At<uint8_t*>(g_netManager, 8) = g_client;
}

void TraceWorld()
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_component);
    for (size_t i = 0; i < 0x20 / 4; ++i)
        g_trace.push_back(Id(w[i]) == 0xBAD00000 ? w[i] : Id(w[i]));
}

void TestSkitchAndSpecialItems()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x007CCE14, 4, {}}, {0x006F3614, 4, {}}, {0x006F361C, 16, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    int calls = 0;
    {
        auto* s = new StandIns;
        StandInScript(*s);
        StandInObjects(*s);
        GameVariable<void*>(0x007D1800) = g_block[1];
        GameVariable<void*>(0x007CCE14) = g_netManager;
        GameVariable<int>(0x006F3614) = 1; // (the original's inlined NewStruct takes the script pool)
        GameVariable<uint32_t>(0x006F361C + 4) = 0x12345678;
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{3506};
            for (int n = 0; n < 5000; ++n) {
                BuildWorld(r);
                int which = static_cast<int>(r.Below(12));
                Record({0xCA5E, static_cast<uint32_t>(which)});
                size_t before = g_trace.size();
                auto* skitch = reinterpret_cast<Obj::CSkitchComponent*>(g_component);
                auto* items = reinterpret_cast<Obj::CSpecialItemComponent*>(g_component);
                // (the items held: some of the item objects, or none)
                for (int i = 0; i < 2; ++i)
                    items->m_items[i] = r.OneIn(3) ? nullptr : reinterpret_cast<Obj::CCompositeObject*>(g_items[r.Below(3)]);
                if (which <= 1 && r.OneIn(10))
                    skitch->m_object = nullptr; // (the hook points' functions check for one)
                switch (which) {
                case 0: {
                    Mth::Vector out = {7.0f, 7.0f, 7.0f, 7.0f};
                    Mth::Vector target = {r.Range(-300.0f, 300.0f), r.Range(-20.0f, 20.0f), r.Range(-300.0f, 300.0f), 1.0f};
                    if (g_wholeTarget)
                        target = {static_cast<float>(static_cast<int>(r.Below(21)) - 10), 0.0f,
                                  static_cast<float>(static_cast<int>(r.Below(21)) - 10), 1.0f};
                    skitch->m_hooks = r.OneIn(6) ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_hookManager);
                    int index = original ? Original<int(__thiscall*)(void*, Mth::Vector*, const Mth::Vector*)>(0x0051BA70)(skitch, &out, &target)
                                         : skitch->GetNearestHookPoint(&out, &target);
                    const uint32_t* o = reinterpret_cast<const uint32_t*>(&out);
                    Record({static_cast<uint32_t>(index), o[0], o[1], o[2], o[3]});
                    break;
                }
                case 1: {
                    Mth::Vector out = {7.0f, 7.0f, 7.0f, 7.0f};
                    skitch->m_hooks = r.OneIn(6) ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_hookManager);
                    int index = static_cast<int>(r.Below(8)) - 1;
                    bool found = original ? Original<bool(__thiscall*)(void*, Mth::Vector*, int)>(0x0051BC70)(skitch, &out, index)
                                          : skitch->GetHookPoint(&out, index);
                    const uint32_t* o = reinterpret_cast<const uint32_t*>(&out);
                    Record({found, o[0], o[1], o[2], o[3]});
                    break;
                }
                case 2: {
                    uint32_t name = r.OneIn(3) ? 0x12345678u : 0xC38CCE3Bu;
                    int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x0051B9D0)(skitch, name, g_structs[8], nullptr)
                                          : skitch->CallMemberFunction(name, reinterpret_cast<Script::CStruct*>(g_structs[8]), nullptr);
                    Record({static_cast<uint32_t>(result)});
                    if (original) {
                        Original<void(__thiscall*)(void*, void*)>(0x0051B9B0)(skitch, nullptr);
                        Original<void(__thiscall*)(void*)>(0x0051B970)(skitch);
                    } else {
                        skitch->InitFromStructure(nullptr);
                        reinterpret_cast<Obj::ComponentWithoutUpdate*>(skitch)->Update();
                    }
                    break;
                }
                case 3: {
                    // Create / destroy both components.
                    for (int kind = 0; kind < 2; ++kind) {
                        std::memset(g_block[0], 0xA5, sizeof(g_block[0]));
                        g_blockNext = r.OneIn(4) ? -1 : 0;
                        void* made;
                        if (original)
                            made = Original<void*(__cdecl*)()>(kind ? 0x0051BD80 : 0x0051BA10)();
                        else
                            made = kind ? Obj::CSpecialItemComponent::Create() : Obj::CSkitchComponent::Create();
                        Record({Id(reinterpret_cast<uint32_t>(made))});
                        g_trace.insert(g_trace.end(), reinterpret_cast<uint32_t*>(g_block[0]), reinterpret_cast<uint32_t*>(g_block[0] + 0x20));
                        if (!made)
                            continue;
                        unsigned flags = r.Below(2);
                        void* result;
                        if (original)
                            result = Original<void*(__thiscall*)(void*, unsigned)>(kind ? 0x0051C130 : 0x0051B980)(made, flags);
                        else if (kind)
                            result = static_cast<Obj::CSpecialItemComponent*>(made)->Destroy(flags);
                        else
                            result = static_cast<Obj::CSkitchComponent*>(made)->Destroy(flags);
                        Record({Id(reinterpret_cast<uint32_t>(result)), At<uint32_t>(g_block[0], 0)});
                    }
                    break;
                }
                case 4: {
                    int slot = static_cast<int>(r.Below(2));
                    if (original)
                        Original<void(__thiscall*)(void*, int)>(0x0051BE00)(items, slot);
                    else
                        items->DestroyItem(slot);
                    break;
                }
                case 5:
                    if (original)
                        Original<void(__thiscall*)(void*)>(r.Bit() ? 0x0051BF00 : 0x0051BF20)(items);
                    else if (r.Bit())
                        items->DestroyItems();
                    else
                        items->DestroyBody();
                    break;
                case 6: {
                    int slot = static_cast<int>(r.Below(2));
                    void* item = original ? Original<void*(__thiscall*)(void*, int, void*)>(0x0051BF80)(items, slot, g_structs[9])
                                          : items->CreateItem(slot, reinterpret_cast<Script::CStruct*>(g_structs[9]));
                    Record({Id(reinterpret_cast<uint32_t>(item))});
                    break;
                }
                default: {
                    static const uint32_t names[] = {0x827C6BD1, 0x742188FA, 0xA1BAA25E, 0xCE887492, 0x827C6BD1, 0xCE887492};
                    uint32_t name = which == 11 && r.Bit() ? 0x11112222u : names[(which - 7) + r.Below(2) * (which < 9 ? 4 : 0)];
                    if (name == 0x827C6BD1 || name == 0x742188FA)
                        g_found[7] = true; // (the index: without it the original writes past the two items)
                    int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x0051C160)(items, name, g_structs[10], nullptr)
                                          : items->CallMemberFunction(name, reinterpret_cast<Script::CStruct*>(g_structs[10]), nullptr);
                    Record({static_cast<uint32_t>(result)});
                    break;
                }
                }
                if (original)
                    calls += static_cast<int>(g_trace.size() - before);
                TraceWorld();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Log("  skitch / special items: %d trace words from the original", calls);
    Compare(traces[0], traces[1], "skitch (hook points, allow) and special items (create, destroy, exists, messages)");
}

} // namespace

SELFTEST(Early, TestSkitchAndSpecialItems, RemoveStandIns);
