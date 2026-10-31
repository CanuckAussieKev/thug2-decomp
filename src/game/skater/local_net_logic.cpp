// The skater's local network logic component: its update to the server (0x00512EA0, 0x005131A0), the rest of it
// (0x00512E30-0x00512E94, 0x00513630-0x005136EA) and the graffiti steal message's handler (0x005136F0). See
// local_net_logic.h.

#include "game/skater/local_net_logic.h"

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"
#include "game/net/thugpro_compat.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/end_run.h"
#include "game/skater/skater.h"
#include "game/util/bitstream.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/camera_lookaround.h"
#include "game/skater/vehicle_sound.h"
#include "game/skater/skater_camera_component.h"

namespace GameObjects {

Maths::Vector g_localNetPosition;
uint32_t g_localNetGuard;

namespace {

namespace Thugpro = OnlinePlay::Thugpro;

constexpr uint32_t kComponent6C = 0xB1D5A3E1; // the component whose +0x18 goes out as the 0x80 field's 3 bits

OnlinePlay::Manager* NetManager() { return OnlinePlay::g_gameNetManager; }

extern void** const kVtable;
constexpr uint32_t kSkaterLocalNetLogic = 0x7CD3E6D5;
constexpr uint32_t kSkaterState = 0x43686585, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kString0 = 0xA4B08520, kGraffitiStealYou = 0x0B3FF911, kGraffitiStealOther = 0x4D7F6FFA;

// GameModules::Skate (0x007CE478; not rewritten yet): a multiplayer game (0x005651F0), a skater by its id (0x00566600).
bool IsVersusSession()
{
    return GameModules::Skate::IsMultiplayer() != 0;
}
CRider* SkaterById(uint32_t id)
{
    return GameModules::Skate::Instance()->GetRiderByNumber(id);
}
// The network manager's first local player and its run count (+0x38, which the manager's code calls the game id).
void* LocalPlayer() { return NetManager()->GetOwnPlayer(); }
uint8_t RunCount() { return NetManager()->GetGameId(); }

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// The network manager's client app for a skater (0x00532EA0: its +8 + 4 * index) and whether this machine is the
// server (0x00533F90: its flags' bit 0).
NetLink::App* ClientApp(int index) { return NetManager()->GetClient(index); }
bool OnServer() { return NetManager()->OnServer(); }

// What the update reads of the skater: its orientation as angles, the core component's state and controls, the two
// other components' values.
struct UpdateState {
    Maths::Vector angles;
    uint8_t state, flag74, value7C, flag75, flag76;
    uint32_t controls, value6C, value80;
};

int16_t PackAngle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * 4096.0)); }

} // namespace

// (shared by both: the original repeats it in each)
static void ReadUpdateState(CRiderHostSync* self, CObjectModule* component6C, UpdateState& out)
{
    reinterpret_cast<Maths::Matrix*>(reinterpret_cast<uint8_t*>(self->m_object) + 0xC0)->GetEulerAngles(out.angles);
    uint8_t* core = self->m_core;
    out.state = Field<uint8_t>(core, 0x18);
    out.value7C = Field<uint8_t>(core, 0xE1);
    out.flag74 = Field<uint8_t>(core, 0xE0);
    out.flag75 = Field<int>(core, 0x1C) == 1 ? 1 : 0;
    out.flag76 = Field<uint8_t>(core, 0x118);
    uint8_t* board = Field<uint8_t*>(self->m_board, 0x22C);
    out.value80 = board ? Field<uint32_t>(board, 0x4C) : 0;
    // A bit per control (the core component's 24, 8 bytes apart from +0x20): set while it's held.
    uint32_t controls = 0;
    for (int k = 0; k < 24; ++k) {
        if (Field<uint8_t>(core, 0x20 + 8 * k))
            controls |= 1u << k;
        else
            controls &= ~(1u << k);
    }
    out.controls = controls;
    uint32_t value;
    out.value6C = *static_cast<CRiderSessionEnd*>(component6C)->GetFlags(&value); // (SkaterEndRun)
}

// 0x00512EA0
uint32_t CRiderHostSync::GetUpdateFlags()
{
    uint32_t flags = 0;
    NetLink::App* app = ClientApp(Field<int>(m_object, 0x2BC));
    CObjectModule* component6C = m_object->GetComponent(kComponent6C);
    bool force = OnServer() || static_cast<uint32_t>(app->m_frameCount) % 15 == 0;
    UpdateState now;
    ReadUpdateState(this, component6C, now);
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value = k == 1 ? Thugpro::PackHeight(m_object->m_pos.y) : Thugpro::PackHorizontal((&m_object->m_pos.x)[k]);
        if (value != m_sentPos[k] || force)
            flags |= 1u << k;
    }
    for (int k = 0; k < 3; ++k) {
        int16_t value = PackAngle((&now.angles.x)[k]);
        if (value != m_sentAngles[k] || force) {
            m_sentAngles[k] = value;
            flags |= 8u << k;
        }
    }
    if (now.state != m_sentState || now.flag74 != m_sentFlag74 || now.value7C != m_sentValue7C ||
        now.flag75 != m_sentFlag75 || now.flag76 != m_sentFlag76 || force)
        flags |= 0x40;
    if (now.controls != m_sentControls || now.value6C != m_sentValue6C || force)
        flags |= 0x80;
    if (now.value80 != m_sentValue80) // (not forced)
        flags |= 0x100;
    return flags;
}

// 0x005131A0
void CRiderHostSync::SendUpdate()
{
    NetLink::NetMessageInfo desc;
    desc.Construct();
    Util::PackedBitBuffer stream;
    auto* app = static_cast<NetLink::Client*>(ClientApp(Field<int>(m_object, 0x2BC)));
    CObjectModule* component6C = m_object->GetComponent(kComponent6C);
    bool onServer = OnServer();
    if (!onServer && static_cast<uint32_t>(app->m_frameCount) % 15 != 0) {
        // Between the 15th frames: only once the connection's send interval has gone by.
        NetLink::ConnSearch search;
        search.m_vtable = NetLink::kConnSearchVtable;
        search.m_listLink = nullptr;
        NetLink::Conn* conn = app->BeginConnWalk(&search);
        if (app->m_clock - m_lastSendTime < conn->GetTransmitPeriod())
            return;
    }
    m_lastSendTime = app->m_clock;
    uint8_t buffer[0x400];
    stream.BindOutputBuffer(buffer, 0x400);
    UpdateState now;
    ReadUpdateState(this, component6C, now);
    stream.WriteBits(app->m_clock, 32);
    uint32_t flags = GetUpdateFlags();
    stream.WriteBits(flags, 9);
    if (onServer) {
        // (a copy of the server's own skater's position, written and never read: the original's static)
        uint32_t& guard = g_localNetGuard;
        if (!(guard & 1))
            guard |= 1;
        g_localNetPosition = m_object->m_pos;
    }
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value = k == 1 ? Thugpro::PackHeight(m_object->m_pos.y) : Thugpro::PackHorizontal((&m_object->m_pos.x)[k]);
        if (flags & (1u << k))
            stream.WriteBits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        m_sentPos[k] = value;
    }
    for (int k = 0; k < 3; ++k) {
        int16_t value = PackAngle((&now.angles.x)[k]);
        if (flags & (8u << k))
            stream.WriteBits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        m_sentAngles[k] = value;
    }
    // (the bytes go out sign-extended: WriteBits doesn't mask them, as the original)
    auto signedByte = [](uint8_t value) { return static_cast<uint32_t>(static_cast<int>(static_cast<int8_t>(value))); };
    if (flags & 0x40) {
        m_sentValue7C = now.value7C;
        m_sentFlag75 = now.flag75;
        m_sentState = now.state;
        m_sentFlag74 = now.flag74;
        m_sentFlag76 = now.flag76;
        stream.WriteBits(signedByte(now.flag74 ? static_cast<uint8_t>(now.state | 0x80) : now.state), 4);
        stream.WriteBits(signedByte(now.value7C), 6);
        stream.WriteBits(signedByte(now.flag75), 1);
        stream.WriteBits(signedByte(now.flag76), 1);
    }
    if (flags & 0x80) {
        m_sentControls = now.controls;
        m_sentValue6C = now.value6C;
        stream.WriteBits(now.controls, 5);
        stream.WriteBits(now.value6C, 3);
    }
    if (flags & 0x100) {
        m_sentValue80 = now.value80;
        stream.WriteBits(now.value80, 32);
    }
    stream.Flush();
    int size = stream.GetByteCount();
    desc.m_data = buffer;
    desc.m_id = 0x23;
    desc.m_byteCount = static_cast<uint16_t>(size);
    desc.m_singular = true;
    desc.m_priority = 0x81;
    app->SendToHost(&desc);
}

// 0x00513630
CObjectModule* __cdecl CRiderHostSync::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderHostSync));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderHostSync*>(memory);
    component->CObjectModule::Construct();
    component->m_vtable = kVtable;
    component->m_sentControls = 0;
    component->m_sentValue6C = 0;
    component->m_core = nullptr;
    component->m_board = nullptr;
    component->m_lastSendTime = 0;
    component->m_sentValue7C = 0xFF;
    component->m_type = kSkaterLocalNetLogic;
    component->m_sentControls = 0;
    component->m_sentFlag74 = 0xFF;
    component->m_sentState = 0;
    component->m_sentValue80 = 0;
    return component;
}

// 0x00512E30
void CRiderHostSync::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00512E40
CRiderHostSync* CRiderHostSync::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00512E70
void CRiderHostSync::Finalize()
{
    m_core = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterState));
    m_board = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
}

// 0x005136C0
void CRiderHostSync::Update()
{
    if (IsVersusSession() && LocalPlayer())
        SendUpdate();
}

// 0x005136F0
int __cdecl CRiderHostSync::HandleGraffitiSteal(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    if (static_cast<uint8_t>(message[0]) != RunCount())
        return NetLink::kHandlerMsgDone;
    uint32_t victimId = *reinterpret_cast<const uint32_t*>(message + 4);
    uint32_t thiefId = *reinterpret_cast<const uint32_t*>(message + 8);
    auto* skater = static_cast<CRider*>(context->m_data);
    CRider* victim = SkaterById(victimId);
    if (!victim)
        return NetLink::kHandlerContinue;
    CRider* thief = SkaterById(thiefId);
    if (!thief)
        return NetLink::kHandlerContinue;
    uint32_t script;
    CRider* other;
    if (skater->m_id == victimId) {
        script = kGraffitiStealYou;
        other = thief;
    } else if (skater->m_id == thiefId) {
        script = kGraffitiStealOther;
        other = victim;
    } else {
        return NetLink::kHandlerContinue;
    }
    Scripts::CStruct* params = Scripts::NewStruct();
    if (params)
        params = params->Init();
    params->Clear(); // (on the new structure, as the original)
    params->AddString(kString0, Scripts::QBVAL_STRING, other->GetDisplayName());
    Scripts::LaunchScript(script, params, skater, false, nullptr);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    return NetLink::kHandlerContinue;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderHostSync::Destroy,
         &GameObjects::CRiderHostSync::Update,
         &CRiderStateReset::ConfigureFromData,
         &GameObjects::CRiderHostSync::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
