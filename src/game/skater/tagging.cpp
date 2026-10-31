// Graffiti tags and stickers: the tagging and tagging control components. See tagging.h.

#include "game/skater/tagging.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/obj/skeleton_component.h"
#include "game/obj/trigger_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/walk_physics.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/texture_dict.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/trigger_events.h"
#include "game/gfx/model_utils.h"

namespace GameObjects {

namespace {

extern void** const kTaggingVtable;
extern void** const kControlVtable;

constexpr uint32_t kCustomTag = 0xAA286543;
constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTrigger = 0xE594F0A2;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;
constexpr uint32_t kSkaterScore = 0xD262802F;

constexpr float kDegreesToRadians = 0.017453292f; // 0x00645464
constexpr float kInch = 12.0f;                   // 0x0064546C (feet to inches)

template <typename T>
T& Prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// --- textures (the texture dictionary 0x00700F20) ---
void* TextureDictionary() { return reinterpret_cast<void*>(Engine3D::g_particleTextureDict); }
void* IngestSkin(const char* name, bool a, bool b, bool c)
{
    return static_cast<Engine3D::CTextureSet*>(TextureDictionary())->IngestSkin(name, a, b, c);
}
void* CopyTexture(uint32_t key, void* texture)
{
    return static_cast<Engine3D::CTextureSet*>(TextureDictionary())->CopyTexture(key, static_cast<Engine3D::CBitmapTexture*>(texture));
}
void* FindTexture(uint32_t checksum) { return static_cast<Engine3D::CTextureSet*>(TextureDictionary())->FetchSkin(checksum); }
void UnloadTexture(void* texture) { static_cast<Engine3D::CTextureSet*>(TextureDictionary())->UnloadTexture(static_cast<Engine3D::CBitmapTexture*>(texture)); }
void ReleaseTexture(void* texture) { reinterpret_cast<void(__thiscall*)(void*)>(&Engine3D::VirtualSlot32Jump)(texture); }
// A texture's image put on `texture` (its vtable +0x10).
void PutOn(void* texture, void* image) { reinterpret_cast<void(__thiscall*)(void*, void*)>(&Gfx2D::VirtualSlot4Jump)(texture, image); }

// One of the component's own textures given back.
void FreeTexture(void*& texture)
{
    if (!texture)
        return;
    Engine3D::Engine::KillTextureSplatsOfTexture(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Prop<void*>(texture, 4))));
    static_cast<Engine3D::CBitmapTexture*>(texture)->PinResident(false);
    ReleaseTexture(texture);
    UnloadTexture(texture);
    texture = nullptr;
}

// A texture loaded on the memory manager's +0x110 heap.
void* LoadOnTextureHeap(const char* name, bool c)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(Prop<MemSys::MemSource*>(memory, 0x110));
    void* texture = IngestSkin(name, true, true, c);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return texture;
}

OnlinePlay::Hub* NetManager() { return OnlinePlay::g_gameNetManager; }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// x87 FSIN / FCOS, as the original takes them.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A float of the tag settings (in the global structure 0x6775E538, walk_parameters).
float TagSetting(uint32_t name) { return CFootMovement::Param(name); }

} // namespace

// --- CTaggingComponent ---

// 0x0051DA80
CObjectModule* __cdecl CTaggingComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CTaggingComponent));
    if (!memory)
        return nullptr;
    auto* tagging = static_cast<CTaggingComponent*>(memory);
    tagging->CObjectModule::MakeNew();
    tagging->m_customTag = false;
    tagging->m_graffiti = nullptr;
    tagging->m_sticker = nullptr;
    tagging->m_vtable = kTaggingVtable;
    tagging->m_kindTag = kType;
    return tagging;
}

// 0x0051D760
void CTaggingComponent::DestroyBody()
{
    m_vtable = kTaggingVtable;
    FreeTexture(m_graffiti);
    FreeTexture(m_sticker);
    CObjectModule::DestroyBody();
}

// 0x0051D860
CTaggingComponent* CTaggingComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0051D890
void CTaggingComponent::ConfigureFromData(Scripts::CStruct* params)
{
    int skater = static_cast<int>(reinterpret_cast<uint32_t>(params));
    params->FetchIntValue(0xA2B033FDu, &skater, 0);
    char key[32];
    std::snprintf(key, sizeof(key), "_%d", skater); // "_%d"
    void* texture = LoadOnTextureHeap("tags\\cas_01", true); // "tags\cas_01"
    m_graffiti = CopyTexture(Checksums::FoldTextIntoChecksum(0xF4FD2D63, key), texture);
    ReleaseTexture(texture);
    UnloadTexture(texture);
    texture = LoadOnTextureHeap("tags\\sticker", true); // "tags\sticker"
    m_sticker = CopyTexture(Checksums::FoldTextIntoChecksum(0x130618A0, key), texture);
    ReleaseTexture(texture);
    UnloadTexture(texture);
}

// 0x0051D830
void CTaggingComponent::ApplyCustomTagTexture() { PutOn(m_graffiti, FindTexture(kCustomTag)); }

// 0x0051D850
bool CTaggingComponent::HasCustomTagTexture() { return m_customTag; }

// 0x0051D9C0
void CTaggingComponent::SetGraffitiTexture(const char* name)
{
    void* texture = LoadOnTextureHeap(name, false);
    PutOn(m_graffiti, texture);
    ReleaseTexture(texture);
    UnloadTexture(texture);
}

// 0x0051DA20
void CTaggingComponent::SetStickerTexture(const char* name)
{
    void* texture = LoadOnTextureHeap(name, false);
    PutOn(m_sticker, texture);
    ReleaseTexture(texture);
    UnloadTexture(texture);
}

// 0x0051DB00
int CTaggingComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case 0x19504222: { // SetGraffitiTexture
        const char* texture = reinterpret_cast<const char*>(name);
        if (params->FetchText(0u, &texture, 0))
            SetGraffitiTexture(texture);
        return 1;
    }
    case 0x90042D4A: { // SetStickerTexture
        const char* texture = reinterpret_cast<const char*>(name);
        if (params->FetchText(0u, &texture, 0))
            SetStickerTexture(texture);
        return 1;
    }
    case 0x0164645C: // HasCustomTagTexture
        return m_customTag ? 1 : 0;
    case 0x723DEE0A: // ApplyCustomTagTexture
        ApplyCustomTagTexture();
        return 1;
    default:
        return 2;
    }
}

// 0x0051DBC0
void CTaggingComponent::PlaceTag(bool sticker, Maths::Vec4 start, Maths::Vec4 end, int size, bool send)
{
    float turn = static_cast<float>(Scripts::FetchFloat(0x2D7DF904u) * static_cast<double>(kDegreesToRadians));
    float spin = 0.0f;
    if (sticker)
        spin = static_cast<float>((Maths::Rnd(90) - 45) * static_cast<double>(kDegreesToRadians));
    float width = static_cast<float>(size);
    Engine3D::Engine::CreateTextureSplatAt(&start, &end, width, width, spin, turn, 5000.0f,
                                     "", 0x54628ED7, 0, 0,
                                     static_cast<uint8_t*>(sticker ? m_sticker : m_graffiti));
    if (!send)
        return;
    NetLink::NetMessageInfo msg;
    msg.MakeNew();
    OnlinePlay::Hub* manager = NetManager();
    if (!manager->InNetGame())
        return;
    struct {
        uint8_t objectId;
        uint8_t sticker;
        uint8_t pad[2];
        Maths::Vec4 start;
        Maths::Vec4 end;
        int size;
    } tag = {static_cast<uint8_t>(m_entity->m_ident), sticker, {}, start, end, size};
    msg.m_content = &tag;
    msg.m_byteCount = 0x28;
    msg.m_ident = 0x99;
    msg.m_groupId = 0xA;
    msg.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    static_cast<NetLink::Guest*>(manager->FetchPatron(0))->SendToHost(&msg);
}

// --- CTaggingControlComponent ---

// 0x0051E280
CObjectModule* __cdecl CTaggingControlComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CTaggingControlComponent));
    if (!memory)
        return nullptr;
    auto* control = static_cast<CTaggingControlComponent*>(memory);
    control->CObjectModule::MakeNew();
    control->m_vtable = kControlVtable;
    control->m_kindTag = kType;
    control->m_pending = false;
    control->m_blocked = false;
    return control;
}

// 0x0051DD50
void CTaggingControlComponent::DestroyBody()
{
    m_vtable = kControlVtable;
    CObjectModule::DestroyBody();
}

// 0x0051DD60
CTaggingControlComponent* CTaggingControlComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0051DD90
void CTaggingControlComponent::Seal()
{
    m_walk = m_entity->FetchModule(kWalk);
    m_input = m_entity->FetchModule(kInput);
    m_corePhysics = m_entity->FetchModule(kSkaterCorePhysics);
    m_trigger = m_entity->FetchModule(kTrigger);
    m_tagging = static_cast<CTaggingComponent*>(m_entity->FetchModule(CTaggingComponent::kType));
}

// 0x0051E300
int CTaggingControlComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct*, Scripts::CScript*)
{
    switch (name) {
    case 0x89E8C244: // VertStickerSlap
        m_countdown = 0;
        SlapSticker(true, false, false);
        return 1;
    case 0x3A760118: // BlockTaggingOff
        m_blocked = false;
        return 1;
    case 0xB260F511: // TagNow
        m_countdown = 0;
        return 1;
    case 0xFE5E3293: // TagClearFlags
        m_countdown = 0;
        m_cooldown = 0.0f;
        m_pending = false;
        m_blocked = false;
        return 1;
    case 0xCD8B5E35: // BlockTaggingOn
        m_blocked = true;
        return 1;
    default:
        return 2;
    }
}

// 0x0051DDF0
bool CTaggingControlComponent::SlapSticker(bool sticker, bool walking, bool low)
{
    if (m_cooldown > 0.0f) {
        m_cooldown = static_cast<float>(m_cooldown - 1.0);
        return false;
    }
    if (m_countdown > 0)
        return false;
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    Scripts::CStruct* settings = Scripts::ReadSubStruct(0x6775E538u);
    float reach;
    Maths::Vec4 facing;
    CGameEntity* skater = m_corePhysics->m_entity;
    if (!sticker) {
        settings->FetchFloat(0xE7AB5365u, &reach, 1);
        if (!walking) {
            facing.x = skater->m_vel.x;
            facing.z = skater->m_vel.z;
            facing.w = skater->m_vel.w;
        } else {
            Maths::Vec4 heading;
            const Maths::Vec4* got = static_cast<CFootMovement*>(m_walk)->GetFacing(&heading);
            facing.x = got->x;
            facing.z = got->z;
            facing.w = got->w;
        }
    } else {
        settings->FetchFloat(0x7B5E76C4u, &reach, 1);
        const Maths::Vec4& up = skater->m_basisFrame.row[1];
        facing.x = -up.x;
        facing.z = -up.z;
        facing.w = up.w;
    }
    facing.y = 0.0f;
    facing.MakeUnit();

    Maths::Vec4 from = m_entity->m_atPoint;
    double height = sticker ? 30.0 : (low ? 20.0 : 50.0); // 0x00646240 / 0x006486AC / 0x006463CC
    from.y = static_cast<float>(from.y + height);
    feeler.SetExcluded(0x3000, 0);
    // From 12 behind to the reach ahead (the w's products kept unrounded).
    float backX = facing.x * kInch, backY = facing.y * kInch, backZ = facing.z * kInch;
    double backW = static_cast<double>(facing.w) * kInch;
    feeler.m_line.start = {from.x - backX, from.y - backY, from.z - backZ, static_cast<float>(from.w - backW)};
    float aheadX = facing.x * reach, aheadY = facing.y * reach, aheadZ = facing.z * reach;
    double aheadW = static_cast<double>(facing.w) * reach;
    feeler.m_line.end = {from.x + aheadX, from.y + aheadY, from.z + aheadZ, static_cast<float>(aheadW + from.w)};
    if (!feeler.GetHitShape(true, false)) {
        DebugOut::Printf("No contact found for sticker?\n"); // "No contact found for sticker?"
        return false;
    }

    // The wall: 12 out from it along its normal, and 12 into it (as the original rounds them).
    const Maths::Vec4& point = feeler.m_contactPos;
    const Maths::Vec4& normal = feeler.m_surfaceDir;
    double nw = static_cast<double>(normal.w) * kInch;
    double nz = static_cast<double>(normal.z) * kInch;
    double ny = static_cast<double>(normal.y) * kInch;
    float nxf = static_cast<float>(static_cast<double>(normal.x) * kInch);
    float nwf = static_cast<float>(nw);
    float nyf = static_cast<float>(ny);
    Maths::Vec4 inside = {static_cast<float>(point.x - static_cast<double>(nxf)), static_cast<float>(point.y - ny),
                          static_cast<float>(point.z - nz), static_cast<float>(point.w - static_cast<double>(nwf))};
    Maths::Vec4 outside = {static_cast<float>((point.x + static_cast<double>(nxf)) + 1.0),
                           static_cast<float>(point.y + static_cast<double>(nyf)),
                           static_cast<float>(static_cast<float>(point.z + nz) + 1.0),
                           static_cast<float>(point.w + static_cast<double>(nwf))};
    if (!m_pending) {
        if (sticker) {
            m_pending = true;
            m_countdown = 10;
            return true;
        }
        DebugOut::Printf("sticker.............................\n"); // "sticker......"
        m_tagging->PlaceTag(true, outside, inside, 30, true);
        return true;
    }
    m_tagging->PlaceTag(true, outside, inside, 30, true);
    m_pending = false;
    return true;
}

// 0x0051E390
bool CTaggingControlComponent::TickFrame()
{
    uint8_t* state = Prop<uint8_t*>(m_entity->FetchModule(kSkaterPhysicsControl), 0x2C);
    if (Prop<bool>(state, 0x118) || Prop<int>(state, 0x1C) != 1 || m_blocked)
        return false;
    if (m_cooldown > 0.0f) {
        m_cooldown = static_cast<float>(m_cooldown - 1.0);
        return false;
    }
    if (m_countdown > 0)
        return false;
    int walkState = Prop<int>(m_walk, 0x18);
    if (walkState != 2 && walkState != 0)
        return false;
    CObjectModule* core = m_entity->FetchModule(kSkaterCorePhysics);
    CObjectModule* skeleton = m_entity->FetchModule(kSkeleton);
    uint32_t bone = Prop<bool>(Prop<uint8_t*>(core, 0x1528), 0x28) ? 0x82B37CBCu : 0x78BC41DFu;
    if (Prop<uint32_t>(m_entity, 0x158) == 0xFC3A8D7F)
        bone = 0xDDEC28AF;
    auto* button = reinterpret_cast<CControlButton*>(reinterpret_cast<uint8_t*>(m_input) + 0x1A0);
    bool tapped = Prop<bool>(button, 0) && static_cast<uint32_t>(button->GetHeldMillis()) < 100u;
    if (!tapped && (!m_pending || m_countdown != 0))
        return true;

    float reach = TagSetting(0xDC98A1BE);
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    Maths::Vec4 jointWorld;
    static_cast<CBoneRigModule*>(skeleton)->GetBonePosition(bone, &jointWorld);
    if (Prop<int>(m_walk, 0x18) == 2)
        jointWorld.y = static_cast<float>(jointWorld.y - 10.0); // 0x00646710
    Maths::Vec4 hits[6];
    float distances[6];
    float rise = static_cast<float>(reach * 0.0);
    bool found = false;
    const Maths::Vec4& heading = Prop<Maths::Vec4>(m_walk, 0x48);
    // A direction across the heading: `index` of six steps of 27 degrees from 67.5 degrees one side.
    auto direction = [&](int index, float& dx, float& dz) {
        double angle = index * static_cast<double>(std::bit_cast<float>(0x3EF1463Au)) -
                       std::bit_cast<float>(0x3F96CBE4u); // 0x0064C438 / 0x0064C434
        double c = X87Cos(angle), s = X87Sin(angle);
        dx = static_cast<float>(c * heading.x - s * heading.z);
        dz = static_cast<float>(s * heading.x + c * heading.z);
    };
    for (int i = 0; i < 6; ++i) {
        float dx, dz;
        direction(i, dx, dz);
        feeler.SetExcluded(0x3000, 0);
        float backZ = dz * kInch, backX = dx * kInch;
        feeler.m_line.start = {jointWorld.x - backX, jointWorld.y, jointWorld.z - backZ, static_cast<float>(jointWorld.w - 12.0)};
        float aheadZ = dz * reach, aheadX = dx * reach;
        feeler.m_line.end = {jointWorld.x + aheadX, jointWorld.y + rise, jointWorld.z + aheadZ,
                             static_cast<float>(static_cast<double>(reach) + jointWorld.w)};
        if (feeler.GetHitShape(true, false)) {
            hits[i] = feeler.m_contactPos;
            distances[i] = std::fabs(feeler.m_reach);
            found = true;
            static_cast<CActivationZoneModule*>(m_trigger)->CheckFeeler(g_taggingEvents, &feeler);
        } else {
            distances[i] = 1000.0f;
        }
    }
    if (!found) {
        DebugOut::Printf("No contact found for splat?\n");
        return false;
    }

    // The nearest wall, then the line along it stretched by 10 until it hits.
    int nearest = 0;
    double nearestDistance = 999.0f; // 0x0064C410
    for (int i = 0; i < 6; ++i) {
        if (nearestDistance > distances[i]) {
            nearestDistance = distances[i];
            nearest = i;
        }
    }
    float dx, dz;
    direction(nearest, dx, dz);
    float distance = static_cast<float>(nearestDistance);
    float backZ = dz * kInch, backX = dx * kInch;
    for (int j = 0; j < 5; ++j) {
        feeler.SetExcluded(0x3000, 0);
        feeler.m_line.start = {jointWorld.x - backX, jointWorld.y, jointWorld.z - backZ, static_cast<float>(jointWorld.w - 12.0)};
        float aheadZ = dz * distance, aheadY = static_cast<float>(distance * 0.0), aheadX = dx * distance;
        feeler.m_line.end = {jointWorld.x + aheadX, jointWorld.y + aheadY, jointWorld.z + aheadZ,
                             static_cast<float>(static_cast<double>(distance) + jointWorld.w)};
        if (feeler.GetHitShape(true, false))
            break;
        distance = static_cast<float>(distance + 10.0);
    }
    Maths::Vec4 start = feeler.m_line.start, end = feeler.m_line.end;

    if (!m_pending) {
        m_pending = true;
        Scripts::LaunchScript(Prop<int>(m_walk, 0x18) == 2 ? 0x8C92DEA3u : 0x5213AFD6u, nullptr, m_entity, false, nullptr);
        m_countdown = 30;
        return true;
    }
    void* score = Prop<void*>(m_entity->FetchModule(kSkaterScore), 0x18);
    int comboScore = Truncated(Prop<int>(score, 0x14) * static_cast<double>(Prop<float>(score, 0x60)));
    int size = comboScore > 5000 ? 100 : (comboScore > 1000 ? 75 : 50);
    m_tagging->PlaceTag(false, start, end, size, true);
    m_cooldown = Scripts::FetchFloat(0xD29DE3D7u);
    if (comboScore > 0) {
        Scripts::CStruct* params = NewParams();
        params->PushFloat(0xE6306423u, static_cast<float>(size));
        Scripts::LaunchScript(0x2B8E0B2B, params, m_entity, false, nullptr);
        DeleteParams(params);
    }
    m_pending = false;
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kTaggingVtable_slots[] = {&GameObjects::CTaggingComponent::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CTaggingComponent::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CTaggingComponent::InvokeMethodByName};
void** const kTaggingVtable = reinterpret_cast<void**>(kTaggingVtable_slots);
static VtableSlot kControlVtable_slots[] = {&GameObjects::CTaggingControlComponent::Teardown,
         &GameObjects::CTaggingControlComponent::TickFrame,
         Stubs::IgnoreArgument,
         &GameObjects::CTaggingControlComponent::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CTaggingControlComponent::InvokeMethodByName};
void** const kControlVtable = reinterpret_cast<void**>(kControlVtable_slots);

} // namespace
} // namespace Obj
