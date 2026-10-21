#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

// The skater's local network logic component (SkaterLocalNetLogic, type 0x7CD3E6D5, its factory 0x00513630): a local
// skater's updates to the server (message 0x23) - its position, orientation, state and controls, each sent when it
// changed (and everything every 15th frame of its client app, unless it's the server's own skater). Only the update
// is rewritten (0x00512EA0 / 0x005131A0, for the big-level positions, TP-R2 - game/net/thugpro_compat.h); the rest of
// the component (0x00512C60-0x005136E8) isn't yet.
namespace Obj {

class CSkaterLocalNetLogicComponent : public CBaseComponent {
public:
    // 0x00512EA0: which fields differ from what was last sent (bits: 0-2 the position x / y / z, 3-5 the angles,
    // 0x40 the state, 0x80 the controls, 0x100 the board component's value) - all but 0x100 when the update is
    // forced. The angles that do are recorded as sent here.
    uint32_t GetUpdateFlags();
    // 0x005131A0: the skater's update sent to the server when its client app's frame calls for it (every 15th, or
    // when the connection's send interval has gone by since the last; always on the server): a 32-bit time stamp,
    // the 9 field bits, the changed fields; recorded as sent.
    void SendUpdate();

    uint32_t m_lastSendTime;  // +0x18 the client app's time when the last update went
    uint8_t m_sentValue7C;    // +0x1C (the core component's +0xE1)
    uint8_t m_pad1D;
    int16_t m_sentPos[3];     // +0x1E (x, z in half units - y in quarters; whole units in a big level)
    int16_t m_sentAngles[3];  // +0x24 (in 1/4096ths)
    uint8_t m_pad2A[2];
    uint32_t m_sentControls;  // +0x2C (a bit per control the core component holds)
    uint32_t m_sentValue6C;   // +0x30 (the 0xB1D5A3E1 component's +0x18)
    uint8_t m_sentState;      // +0x34 (the core component's +0x18)
    uint8_t m_sentFlag74;     // +0x35 (+0xE0)
    uint8_t m_sentFlag75;     // +0x36 (+0x1C is 1)
    uint8_t m_sentFlag76;     // +0x37 (+0x118)
    uint32_t m_sentValue80;   // +0x38 (the board's: +0x40's +0x22C's +0x4C)
    uint8_t* m_core;          // +0x3C the skater's core component (its state and controls)
    uint8_t* m_board;         // +0x40
};
static_assert(offsetof(CSkaterLocalNetLogicComponent, m_lastSendTime) == 0x18 &&
                  offsetof(CSkaterLocalNetLogicComponent, m_sentValue7C) == 0x1C &&
                  offsetof(CSkaterLocalNetLogicComponent, m_sentPos) == 0x1E &&
                  offsetof(CSkaterLocalNetLogicComponent, m_sentAngles) == 0x24 &&
                  offsetof(CSkaterLocalNetLogicComponent, m_sentControls) == 0x2C &&
                  offsetof(CSkaterLocalNetLogicComponent, m_sentState) == 0x34 &&
                  offsetof(CSkaterLocalNetLogicComponent, m_sentValue80) == 0x38 &&
                  offsetof(CSkaterLocalNetLogicComponent, m_core) == 0x3C &&
                  offsetof(CSkaterLocalNetLogicComponent, m_board) == 0x40,
              "layout must match the original");

} // namespace Obj
