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
#include "hook/game.h"
#include "hook/game_code.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kTaggingVtable = reinterpret_cast<void**>(0x0064C330);
void** const kControlVtable = reinterpret_cast<void**>(0x0064C394);

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
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// --- textures (the texture dictionary 0x00700F20) ---
void* TextureDictionary() { return reinterpret_cast<void*>(Nx::g_particleTextureDict); }
void* LoadTexture(const char* name, bool a, bool b, bool c)
{
    return static_cast<Nx::CTexDict*>(TextureDictionary())->LoadTexture(name, a, b, c);
}
void* CopyTexture(uint32_t key, void* texture)
{
    return static_cast<Nx::CTexDict*>(TextureDictionary())->CopyTexture(key, static_cast<Nx::CTexture*>(texture));
}
void* FindTexture(uint32_t checksum) { return static_cast<Nx::CTexDict*>(TextureDictionary())->GetTexture(checksum); }
void UnloadTexture(void* texture) { static_cast<Nx::CTexDict*>(TextureDictionary())->UnloadTexture(static_cast<Nx::CTexture*>(texture)); }
void ReleaseTexture(void* texture) { GameFunction<void(__thiscall*)(void*)>(0x0049F3B0)(texture); }
// A texture's image put on `texture` (its vtable +0x10).
void PutOn(void* texture, void* image) { GameFunction<void(__thiscall*)(void*, void*)>(0x00487D50)(texture, image); }

// One of the component's own textures given back.
void FreeTexture(void*& texture)
{
    if (!texture)
        return;
    Nx::Engine::KillTextureSplatsOfTexture(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Field<void*>(texture, 4))));
    static_cast<Nx::CTexture*>(texture)->SetPermanent(false);
    ReleaseTexture(texture);
    UnloadTexture(texture);
    texture = nullptr;
}

// A texture loaded on the memory manager's +0x110 heap.
void* LoadOnTextureHeap(const char* name, bool c)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(Field<Mem::Allocator*>(memory, 0x110));
    void* texture = LoadTexture(name, true, true, c);
    Mem::Manager::Get()->PopContext();
    return texture;
}

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }

Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
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
float TagSetting(uint32_t name) { return CWalkComponent::Param(name); }

} // namespace

// --- CTaggingComponent ---

// 0x0051DA80
CBaseComponent* __cdecl CTaggingComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CTaggingComponent));
    if (!memory)
        return nullptr;
    auto* tagging = static_cast<CTaggingComponent*>(memory);
    tagging->CBaseComponent::Construct();
    tagging->m_customTag = false;
    tagging->m_graffiti = nullptr;
    tagging->m_sticker = nullptr;
    tagging->m_vtable = kTaggingVtable;
    tagging->m_type = kType;
    return tagging;
}

// 0x0051D760
void CTaggingComponent::DestroyBody()
{
    m_vtable = kTaggingVtable;
    FreeTexture(m_graffiti);
    FreeTexture(m_sticker);
    CBaseComponent::DestroyBody();
}

// 0x0051D860
CTaggingComponent* CTaggingComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0051D890
void CTaggingComponent::InitFromStructure(Script::CStruct* params)
{
    int skater = static_cast<int>(reinterpret_cast<uint32_t>(params));
    params->GetInteger(0xA2B033FDu, &skater, 0);
    char key[32];
    std::snprintf(key, sizeof(key), reinterpret_cast<const char*>(0x0064C380), skater); // "_%d"
    void* texture = LoadOnTextureHeap(reinterpret_cast<const char*>(0x0064C374), true); // "tags\cas_01"
    m_graffiti = CopyTexture(Crc::ExtendCRCWithString(0xF4FD2D63, key), texture);
    ReleaseTexture(texture);
    UnloadTexture(texture);
    texture = LoadOnTextureHeap(reinterpret_cast<const char*>(0x0064C364), true); // "tags\sticker"
    m_sticker = CopyTexture(Crc::ExtendCRCWithString(0x130618A0, key), texture);
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
int CTaggingComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    switch (name) {
    case 0x19504222: { // SetGraffitiTexture
        const char* texture = reinterpret_cast<const char*>(name);
        if (params->GetString(0u, &texture, 0))
            SetGraffitiTexture(texture);
        return 1;
    }
    case 0x90042D4A: { // SetStickerTexture
        const char* texture = reinterpret_cast<const char*>(name);
        if (params->GetString(0u, &texture, 0))
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
void CTaggingComponent::PlaceTag(bool sticker, Mth::Vector start, Mth::Vector end, int size, bool send)
{
    float turn = static_cast<float>(Script::GetFloat(0x2D7DF904u) * static_cast<double>(kDegreesToRadians));
    float spin = 0.0f;
    if (sticker)
        spin = static_cast<float>((Mth::Rnd(90) - 45) * static_cast<double>(kDegreesToRadians));
    float width = static_cast<float>(size);
    Nx::Engine::CreateTextureSplatAt(&start, &end, width, width, spin, turn, 5000.0f,
                                     reinterpret_cast<const char*>(0x006469B4), 0x54628ED7, 0, 0,
                                     static_cast<uint8_t*>(sticker ? m_sticker : m_graffiti));
    if (!send)
        return;
    Net::MsgDesc msg;
    msg.Construct();
    GameNet::Manager* manager = NetManager();
    if (!manager->InNetGame())
        return;
    struct {
        uint8_t objectId;
        uint8_t sticker;
        uint8_t pad[2];
        Mth::Vector start;
        Mth::Vector end;
        int size;
    } tag = {static_cast<uint8_t>(m_object->m_id), sticker, {}, start, end, size};
    msg.m_data = &tag;
    msg.m_length = 0x28;
    msg.m_id = 0x99;
    msg.m_groupId = 0xA;
    msg.m_queue = Net::MsgDesc::kQueueSequenced;
    static_cast<Net::Client*>(manager->GetClient(0))->EnqueueMessageToServer(&msg);
}

// --- CTaggingControlComponent ---

// 0x0051E280
CBaseComponent* __cdecl CTaggingControlComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CTaggingControlComponent));
    if (!memory)
        return nullptr;
    auto* control = static_cast<CTaggingControlComponent*>(memory);
    control->CBaseComponent::Construct();
    control->m_vtable = kControlVtable;
    control->m_type = kType;
    control->m_pending = false;
    control->m_blocked = false;
    return control;
}

// 0x0051DD50
void CTaggingControlComponent::DestroyBody()
{
    m_vtable = kControlVtable;
    CBaseComponent::DestroyBody();
}

// 0x0051DD60
CTaggingControlComponent* CTaggingControlComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0051DD90
void CTaggingControlComponent::Finalize()
{
    m_walk = m_object->GetComponent(kWalk);
    m_input = m_object->GetComponent(kInput);
    m_corePhysics = m_object->GetComponent(kSkaterCorePhysics);
    m_trigger = m_object->GetComponent(kTrigger);
    m_tagging = static_cast<CTaggingComponent*>(m_object->GetComponent(CTaggingComponent::kType));
}

// 0x0051E300
int CTaggingControlComponent::CallMemberFunction(uint32_t name, Script::CStruct*, Script::CScript*)
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
    Nx::CFeeler feeler;
    feeler.Construct();
    Script::CStruct* settings = Script::GetStructure(0x6775E538u);
    float reach;
    Mth::Vector facing;
    CCompositeObject* skater = m_corePhysics->m_object;
    if (!sticker) {
        settings->GetFloat(0xE7AB5365u, &reach, 1);
        if (!walking) {
            facing.x = skater->m_vel.x;
            facing.z = skater->m_vel.z;
            facing.w = skater->m_vel.w;
        } else {
            Mth::Vector heading;
            const Mth::Vector* got = static_cast<CWalkComponent*>(m_walk)->GetFacing(&heading);
            facing.x = got->x;
            facing.z = got->z;
            facing.w = got->w;
        }
    } else {
        settings->GetFloat(0x7B5E76C4u, &reach, 1);
        const Mth::Vector& up = skater->m_matrix.row[1];
        facing.x = -up.x;
        facing.z = -up.z;
        facing.w = up.w;
    }
    facing.y = 0.0f;
    facing.Normalize();

    Mth::Vector from = m_object->m_pos;
    double height = sticker ? 30.0 : (low ? 20.0 : 50.0); // 0x00646240 / 0x006486AC / 0x006463CC
    from.y = static_cast<float>(from.y + height);
    feeler.SetIgnore(0x3000, 0);
    // From 12 behind to the reach ahead (the w's products kept unrounded).
    float backX = facing.x * kInch, backY = facing.y * kInch, backZ = facing.z * kInch;
    double backW = static_cast<double>(facing.w) * kInch;
    feeler.m_line.start = {from.x - backX, from.y - backY, from.z - backZ, static_cast<float>(from.w - backW)};
    float aheadX = facing.x * reach, aheadY = facing.y * reach, aheadZ = facing.z * reach;
    double aheadW = static_cast<double>(facing.w) * reach;
    feeler.m_line.end = {from.x + aheadX, from.y + aheadY, from.z + aheadZ, static_cast<float>(aheadW + from.w)};
    if (!feeler.GetCollision(true, false)) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064C3C8)); // "No contact found for sticker?"
        return false;
    }

    // The wall: 12 out from it along its normal, and 12 into it (as the original rounds them).
    const Mth::Vector& point = feeler.m_point;
    const Mth::Vector& normal = feeler.m_normal;
    double nw = static_cast<double>(normal.w) * kInch;
    double nz = static_cast<double>(normal.z) * kInch;
    double ny = static_cast<double>(normal.y) * kInch;
    float nxf = static_cast<float>(static_cast<double>(normal.x) * kInch);
    float nwf = static_cast<float>(nw);
    float nyf = static_cast<float>(ny);
    Mth::Vector inside = {static_cast<float>(point.x - static_cast<double>(nxf)), static_cast<float>(point.y - ny),
                          static_cast<float>(point.z - nz), static_cast<float>(point.w - static_cast<double>(nwf))};
    Mth::Vector outside = {static_cast<float>((point.x + static_cast<double>(nxf)) + 1.0),
                           static_cast<float>(point.y + static_cast<double>(nyf)),
                           static_cast<float>(static_cast<float>(point.z + nz) + 1.0),
                           static_cast<float>(point.w + static_cast<double>(nwf))};
    if (!m_pending) {
        if (sticker) {
            m_pending = true;
            m_countdown = 10;
            return true;
        }
        Dbg::Printf(reinterpret_cast<const char*>(0x0064C3E8)); // "sticker......"
        m_tagging->PlaceTag(true, outside, inside, 30, true);
        return true;
    }
    m_tagging->PlaceTag(true, outside, inside, 30, true);
    m_pending = false;
    return true;
}

// 0x0051E390
bool CTaggingControlComponent::Update()
{
    uint8_t* state = Field<uint8_t*>(m_object->GetComponent(kSkaterPhysicsControl), 0x2C);
    if (Field<bool>(state, 0x118) || Field<int>(state, 0x1C) != 1 || m_blocked)
        return false;
    if (m_cooldown > 0.0f) {
        m_cooldown = static_cast<float>(m_cooldown - 1.0);
        return false;
    }
    if (m_countdown > 0)
        return false;
    int walkState = Field<int>(m_walk, 0x18);
    if (walkState != 2 && walkState != 0)
        return false;
    CBaseComponent* core = m_object->GetComponent(kSkaterCorePhysics);
    CBaseComponent* skeleton = m_object->GetComponent(kSkeleton);
    uint32_t bone = Field<bool>(Field<uint8_t*>(core, 0x1528), 0x28) ? 0x82B37CBCu : 0x78BC41DFu;
    if (Field<uint32_t>(m_object, 0x158) == 0xFC3A8D7F)
        bone = 0xDDEC28AF;
    auto* button = reinterpret_cast<CControlButton*>(reinterpret_cast<uint8_t*>(m_input) + 0x1A0);
    bool tapped = Field<bool>(button, 0) && static_cast<uint32_t>(button->GetPressedTime()) < 100u;
    if (!tapped && (!m_pending || m_countdown != 0))
        return true;

    float reach = TagSetting(0xDC98A1BE);
    Nx::CFeeler feeler;
    feeler.Construct();
    Mth::Vector bonePos;
    static_cast<CSkeletonComponent*>(skeleton)->GetBonePosition(bone, &bonePos);
    if (Field<int>(m_walk, 0x18) == 2)
        bonePos.y = static_cast<float>(bonePos.y - 10.0); // 0x00646710
    Mth::Vector hits[6];
    float distances[6];
    float rise = static_cast<float>(reach * 0.0);
    bool found = false;
    const Mth::Vector& heading = Field<Mth::Vector>(m_walk, 0x48);
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
        feeler.SetIgnore(0x3000, 0);
        float backZ = dz * kInch, backX = dx * kInch;
        feeler.m_line.start = {bonePos.x - backX, bonePos.y, bonePos.z - backZ, static_cast<float>(bonePos.w - 12.0)};
        float aheadZ = dz * reach, aheadX = dx * reach;
        feeler.m_line.end = {bonePos.x + aheadX, bonePos.y + rise, bonePos.z + aheadZ,
                             static_cast<float>(static_cast<double>(reach) + bonePos.w)};
        if (feeler.GetCollision(true, false)) {
            hits[i] = feeler.m_point;
            distances[i] = std::fabs(feeler.m_distance);
            found = true;
            static_cast<CTriggerComponent*>(m_trigger)->CheckFeeler(reinterpret_cast<const int*>(0x0064C388), &feeler);
        } else {
            distances[i] = 1000.0f;
        }
    }
    if (!found) {
        Dbg::Printf(GAME_TEXT(0x0064C414, "No contact found for splat?\n"));
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
        feeler.SetIgnore(0x3000, 0);
        feeler.m_line.start = {bonePos.x - backX, bonePos.y, bonePos.z - backZ, static_cast<float>(bonePos.w - 12.0)};
        float aheadZ = dz * distance, aheadY = static_cast<float>(distance * 0.0), aheadX = dx * distance;
        feeler.m_line.end = {bonePos.x + aheadX, bonePos.y + aheadY, bonePos.z + aheadZ,
                             static_cast<float>(static_cast<double>(distance) + bonePos.w)};
        if (feeler.GetCollision(true, false))
            break;
        distance = static_cast<float>(distance + 10.0);
    }
    Mth::Vector start = feeler.m_line.start, end = feeler.m_line.end;

    if (!m_pending) {
        m_pending = true;
        Script::RunScript(Field<int>(m_walk, 0x18) == 2 ? 0x8C92DEA3u : 0x5213AFD6u, nullptr, m_object, false, nullptr);
        m_countdown = 30;
        return true;
    }
    void* score = Field<void*>(m_object->GetComponent(kSkaterScore), 0x18);
    int comboScore = Truncated(Field<int>(score, 0x14) * static_cast<double>(Field<float>(score, 0x60)));
    int size = comboScore > 5000 ? 100 : (comboScore > 1000 ? 75 : 50);
    m_tagging->PlaceTag(false, start, end, size, true);
    m_cooldown = Script::GetFloat(0xD29DE3D7u);
    if (comboScore > 0) {
        Script::CStruct* params = NewParams();
        params->AddFloat(0xE6306423u, static_cast<float>(size));
        Script::RunScript(0x2B8E0B2B, params, m_object, false, nullptr);
        DeleteParams(params);
    }
    m_pending = false;
    return true;
}

} // namespace Obj

REPLACE(0x0051DA80, Obj::CTaggingComponent::Create);
REPLACE(0x0051D760, &Obj::CTaggingComponent::DestroyBody);
REPLACE(0x0051D860, &Obj::CTaggingComponent::Destroy);
REPLACE(0x0051D890, &Obj::CTaggingComponent::InitFromStructure);
REPLACE(0x0051DB00, &Obj::CTaggingComponent::CallMemberFunction);
REPLACE(0x0051D830, &Obj::CTaggingComponent::ApplyCustomTagTexture);
REPLACE_SOURCE_ONLY(0x0051D850, &Obj::CTaggingComponent::HasCustomTagTexture); // 4 bytes
REPLACE(0x0051D9C0, &Obj::CTaggingComponent::SetGraffitiTexture);
REPLACE(0x0051DA20, &Obj::CTaggingComponent::SetStickerTexture);
REPLACE(0x0051DBC0, &Obj::CTaggingComponent::PlaceTag);
REPLACE(0x0051E280, Obj::CTaggingControlComponent::Create);
REPLACE(0x0051DD50, &Obj::CTaggingControlComponent::DestroyBody);
REPLACE(0x0051DD60, &Obj::CTaggingControlComponent::Destroy);
REPLACE(0x0051DD90, &Obj::CTaggingControlComponent::Finalize);
REPLACE(0x0051E300, &Obj::CTaggingControlComponent::CallMemberFunction);
REPLACE(0x0051DDF0, &Obj::CTaggingControlComponent::SlapSticker);
REPLACE(0x0051E390, &Obj::CTaggingControlComponent::Update);
REPLACE_SOURCE_ONLY(0x0051EA30, &Obj::CTaggingControlComponent::Update); // a jump to 0x0051E390 (the vtable's)
