// Self-tests for the component set-up helpers and peds (src/game/obj/game_objects.cpp, 0x0057B250-0x0057B4E0): the
// original and ours add components to stand-in objects and make peds in an arena; the component constructors, the
// object's methods, scripts and memory are recording stand-ins, and the calls and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/obj/bbox_component.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/motion.h"
#include "game/obj/motion_path.h"
#include "game/obj/object.h"
#include "game/obj/sound_component.h"
#include "game/obj/stream_component.h"
#include "game/obj/suspend_component.h"
#include "game/script/nodes.h"
#include "game/obj/object_proximity.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;

// Arena layout.
constexpr uint32_t kObject = 0x0000;     // an object (0x108)
constexpr uint32_t kComponents = 0x0400; // components, 0x100 apart by type
constexpr uint32_t kManager = 0x0C00;    // the composite object manager
constexpr uint32_t kNodes = 0x0D00;      // node structures, 0x20 apart

void* g_objectVtable[12];
void* g_managerVtable[4];
void* g_componentVtable[4];
bool g_noMotion;

uint32_t ComponentFor(uint32_t type)
{
    switch (type) {
    case 0x0A015E17: return g_noMotion ? 0u : At(kComponents);
    case 0x286A8D26: return At(kComponents + 0x100);
    case 0xCE0CA665: return At(kComponents + 0x200);
    default: return At(kComponents + 0x300);
    }
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x0045E510, AddressOf(&Obj::CCompositeObject::Construct)}, 0, [](Regs& r) {
        Rec(0xE510, {r.ecx});
        reinterpret_cast<Obj::CCompositeObject*>(r.ecx)->m_vtable = g_objectVtable;
        return r.ecx;
    });
    // The component constructors (each records which and returns the memory).
    const std::pair<uint32_t, uint32_t> constructors[] = {
        {0x00433F50u, AddressOf(&Obj::CSuspendComponent::Construct)}, {0x0041AAD0u, AddressOf(&Obj::CBBoxComponent::Construct)},
        {0x00424AA0u, AddressOf(&Obj::CMotionComponent::Construct)},  {0x004EC370u, AddressOf(&Obj::CObjectProximityComponent::Construct)},
        {0x00432D90u, AddressOf(&Obj::CSoundComponent::Construct)},   {0x00433C10u, AddressOf(&Obj::CStreamComponent::Construct)},
        {0x0041F500u, AddressOf(&Obj::CLockObjComponent::Construct)}};
    for (auto [address, ours] : constructors) {
        Handler handler = [address](Regs& r) {
            Rec(address & 0xFFFFF, {r.ecx});
            *reinterpret_cast<uint32_t*>(r.ecx) = address;
            return r.ecx;
        };
        if (ours)
            s.At({address, ours}, 0, handler);
        else
            s.At({address}, 0, handler);
    }
    s.At({0x0045DA30, AddressOf(&Obj::CCompositeObject::AddComponent)}, 4, [](Regs& r) {
        Rec(0xDA30, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return ComponentFor(r.args[0]);
    });
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00461930, AddressOf(&Obj::CObject::SetID)}, 4, [](Regs& r) {
        Rec(0x1930, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00461350, AddressOf(&Obj::CObject::SwitchScript)}, 8, [](Regs& r) {
        Rec(0x1350, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00425970, AddressOf(&Obj::CMotionComponent::OrientToNode)}, 4, [](Regs& r) {
        Rec(0x5970, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Rec(0xBE40, {r.args[0]});
        return *reinterpret_cast<uint32_t*>(r.args[0]);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Rec(0xC050, {r.args[0], r.args[1]});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 1.5f, v[1] = -2.25f, v[2] = 3.0f, v[3] = 1.0f;
             return 1u;
         });
    auto getter = [](Regs& r, uint32_t tag) {
        Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
        auto it = g_params.find({r.ecx, r.args[0]});
        if (it == g_params.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [getter](Regs& r) { return getter(r, 0x6950); });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [getter](Regs& r) { return getter(r, 0x67C0); });
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
    // The object's Finalize (+0x20), the components' InitFromStructure (+0x08), the manager's RegisterObject (+0x04).
    g_objectVtable[8] = s.Thunk(0, [](Regs& r) {
        Rec(0xF1A1, {r.ecx});
        return 0u;
    });
    g_componentVtable[2] = s.Thunk(4, [](Regs& r) {
        Rec(0xC0C2, {r.ecx, r.args[0]});
        return 0u;
    });
    g_managerVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x6E61, {r.ecx, r.args[0]});
        return 0u;
    });
}

// Every helper on its own (the node's components: with a motion - without one the original calls the motion's
// method on null, not run - with and without a name, a trigger
// script with and without params; peds from two nodes.
void TestObjectSetup()
{
    SaveGlobals({{0x006F0FB8, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        g_noMotion = false;
        *Ptr<void**>(kManager) = g_managerVtable;
        GameVariable<uint32_t>(0x006F0FB8) = At(kManager);
        for (uint32_t c = 0; c < 4; ++c)
            *Ptr<void**>(kComponents + 0x100 * c) = g_componentVtable;
        auto* object = Ptr<Obj::CCompositeObject>(kObject);
        object->m_vtable = g_objectVtable;
        auto cdecl1 = [&](uint32_t address, void (*ours)(Obj::CCompositeObject*)) {
            if (original)
                Original<void(__cdecl*)(Obj::CCompositeObject*)>(address)(object);
            else
                ours(object);
        };
        cdecl1(0x0057B250, &Obj::AddBaseComponents);
        cdecl1(0x0057B2F0, &Obj::AddMotionComponents);
        // Nodes: [0] the name.
        uint32_t nodes[4] = {At(kNodes), At(kNodes + 0x20), At(kNodes + 0x40), At(kNodes + 0x60)};
        *reinterpret_cast<uint32_t*>(nodes[0]) = 0x11110001;
        *reinterpret_cast<uint32_t*>(nodes[1]) = 0;
        *reinterpret_cast<uint32_t*>(nodes[2]) = 0x11110003;
        *reinterpret_cast<uint32_t*>(nodes[3]) = 0x11110004;
        g_params[{nodes[0], 0x2CA8A299}] = 0x5C0000A1; // a trigger script with params
        g_params[{nodes[0], 0x7031F10C}] = At(kNodes + 0x100);
        g_params[{nodes[2], 0x2CA8A299}] = 0x5C0000A3; // without
        for (uint32_t node : nodes) {
            if (original)
                Original<void(__cdecl*)(Obj::CCompositeObject*, CStruct*)>(0x0057B420)(object, reinterpret_cast<CStruct*>(node));
            else
                Obj::AddNodeComponents(object, reinterpret_cast<CStruct*>(node));
        }
        *Ptr<uint32_t>(kComponents + 0x1C) = 0x81; // (the motion's flags: one more set)
        for (uint32_t node : {nodes[0], nodes[1]}) {
            if (original)
                Original<void(__cdecl*)(CStruct*)>(0x0057B4E0)(reinterpret_cast<CStruct*>(node));
            else
                Obj::CreatePed(reinterpret_cast<CStruct*>(node));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "object setup");
}

} // namespace

SELFTEST(Early, TestObjectSetup, ViewerTestKit::Cleanup);
