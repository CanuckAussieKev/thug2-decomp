// The skater's local network logic component: its update to the server (0x00512EA0, 0x005131A0). See
// local_net_logic.h.

#include "game/skater/local_net_logic.h"

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/thugpro_compat.h"
#include "game/util/bitstream.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

namespace Thugpro = GameNet::Thugpro;

constexpr uint32_t kComponent6C = 0xB1D5A3E1; // the component whose +0x18 goes out as the 0x80 field's 3 bits

void* NetManager() { return GameVariable<void*>(0x007CCE14); }

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// The network manager's client app for a skater (0x00532EA0: its +8 + 4 * index) and whether this machine is the
// server (0x00533F90: its flags' bit 0).
Net::App* ClientApp(int index) { return GameFunction<Net::App*(__thiscall*)(void*, int)>(0x00532EA0)(NetManager(), index); }
bool OnServer() { return GameFunction<uint32_t(__thiscall*)(void*)>(0x00533F90)(NetManager()) & 0xFF; }

// What the update reads of the skater: its orientation as angles, the core component's state and controls, the two
// other components' values.
struct UpdateState {
    Mth::Vector angles;
    uint8_t state, flag74, value7C, flag75, flag76;
    uint32_t controls, value6C, value80;
};

int16_t PackAngle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * 4096.0)); }

} // namespace

// (shared by both: the original repeats it in each)
static void ReadUpdateState(CSkaterLocalNetLogicComponent* self, CBaseComponent* component6C, UpdateState& out)
{
    reinterpret_cast<Mth::Matrix*>(reinterpret_cast<uint8_t*>(self->m_object) + 0xC0)->GetEulerAngles(out.angles);
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
    out.value6C = *GameFunction<uint32_t*(__thiscall*)(CBaseComponent*, uint32_t*)>(0x00510D30)(component6C, &value);
}

// 0x00512EA0
uint32_t CSkaterLocalNetLogicComponent::GetUpdateFlags()
{
    uint32_t flags = 0;
    Net::App* app = ClientApp(Field<int>(m_object, 0x2BC));
    CBaseComponent* component6C = m_object->GetComponent(kComponent6C);
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
void CSkaterLocalNetLogicComponent::SendUpdate()
{
    Net::MsgDesc desc;
    desc.Construct();
    Util::BitStream stream;
    auto* app = static_cast<Net::Client*>(ClientApp(Field<int>(m_object, 0x2BC)));
    CBaseComponent* component6C = m_object->GetComponent(kComponent6C);
    bool onServer = OnServer();
    if (!onServer && static_cast<uint32_t>(app->m_frameCount) % 15 != 0) {
        // Between the 15th frames: only once the connection's send interval has gone by.
        Net::ConnSearch search;
        search.m_vtable = reinterpret_cast<void**>(0x0064759C);
        search.m_node = nullptr;
        Net::Conn* conn = app->FirstConnection(&search);
        if (app->m_time - m_lastSendTime < conn->GetSendInterval())
            return;
    }
    m_lastSendTime = app->m_time;
    uint8_t buffer[0x400];
    stream.SetOutputData(buffer, 0x400);
    UpdateState now;
    ReadUpdateState(this, component6C, now);
    stream.WriteBits(app->m_time, 32);
    uint32_t flags = GetUpdateFlags();
    stream.WriteBits(flags, 9);
    if (onServer) {
        // (a copy of the server's own skater's position, written and never read: the original's static)
        uint32_t& guard = GameVariable<uint32_t>(0x007CCDE0);
        if (!(guard & 1))
            guard |= 1;
        GameVariable<Mth::Vector>(0x007CCDD0) = m_object->m_pos;
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
    desc.m_length = static_cast<uint16_t>(size);
    desc.m_singular = true;
    desc.m_priority = 0x81;
    app->EnqueueMessageToServer(&desc);
}

} // namespace Obj

REPLACE(0x00512EA0, &Obj::CSkaterLocalNetLogicComponent::GetUpdateFlags);
REPLACE(0x005131A0, &Obj::CSkaterLocalNetLogicComponent::SendUpdate);
