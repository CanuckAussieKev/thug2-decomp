// The skater's non-local network logic component (GameObjects::CRiderRemoteSync, nonlocal_net_logic.h): the
// playback of the states and messages its client sent - interpolation between states, extrapolation when they run
// late, the messages' events. The movement code follows the original's x87 maths (double until stored as floats).

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/nx/feeler.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/model_component.h"
#include "game/obj/rail_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/end_run.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/nonlocal_net_logic.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/special_item.h"
#include "game/skater/skater_x87.h"
#include "game/skater/state_history.h"
#include "game/sys/game_clock.h"
#include "game/gfx/anim_blend.h"
#include "game/obj/control_pad.h"
#include "game/skate/skate_module.h"
#include "game/sys/game_config.h"

namespace GameObjects {

int g_nonlocalLag;

namespace {

template <typename T>
T& Field(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The model component's three spin axes (its m_twistRate, about X, Y, Z) the messages 0x76 / 0x8C start and stop.
constexpr uint32_t kModelParts[3] = {0x38, 0xCC, 0x160};
// Message 0x77: the made item's structure and component
constexpr uint32_t kItemName = 0xCAB94088, kItemOwner = 0x40C698AF, kItemComponent = 0x5CB5CB7E;
constexpr uint32_t kSkaterAppeared = 0x1011A5D6; // the script run when a skater's playback starts (name unknown)

constexpr float kTurnSnap = 0.75f;          // 0x0064A4EC: the at rows this far apart: no slerp, the earlier one's
constexpr float kTeleportDistance = 600.0f; // 0x0064B81C: moved further than this between states: put there
constexpr float kOne = 1.0f;                // 0x006453A8
constexpr float kMsPerSecond = 1000.0f;     // 0x006453C0 (also: playback starts this over the frame rate behind)
constexpr float kHalf = 0.5f;               // 0x006453BC
constexpr float kSpeedScale = 16.0f;        // 0x00645D68: a move's length over the frame's ms, times this
constexpr float kWallCheckReach = 120.0f;   // 0x0064BD2C: extrapolated further than this: no wall check
constexpr uint32_t kFirstForwardCollisionHeight = 0xD4205C9B; // Skater_First_Forward_Collision_Height
constexpr uint32_t kRailGravity = 0xD1F46992;         // (a physics float: the rail's pull downhill; name unknown)
constexpr uint32_t kRailCornerLeaveAngle = 0x76C1DA15; // (the angle a rail may turn and still be followed)
constexpr uint32_t kRailActive = 0x4;                 // CGrindPoint::m_flags
constexpr float kRailEndSlack = 0.1f;                 // 0x00645D74: past a segment's end by more than this
constexpr float kDegreesToRadians = 0.01745329238474369f; // 0x00645464

// A state the history keeps (0x84 bytes, 20 at the history's +0x1C): +0x08 the matrix (+0x28 its at row), +0x48 the
// position, +0x68 the timed flags as bits, +0x6C the end run flags, +0x70 the state, +0x74 doing a trick, +0x75 the
// physics mode, +0x76 driving, +0x78 the time, +0x7C the terrain.
constexpr uint32_t kFrameSize = 0x84;
uint8_t* HistoryFrame(uint8_t* history, int index) { return history + 0x1C + index * kFrameSize; }
uint32_t FrameTime(uint8_t* frame) { return reinterpret_cast<SkaterStateHistoryFrame*>(frame)->GetTime(); }
Maths::Matrix& FrameMatrix(uint8_t* frame) { return Field<Maths::Matrix>(frame, 0x08); }
Maths::Vector& FramePosition(uint8_t* frame) { return Field<Maths::Vector>(frame, 0x48); }

OnlinePlay::Manager* NetManager() { return OnlinePlay::g_gameNetManager; }
// The network manager's client app: +8 its frame count.
uint8_t* ClientApp() { return reinterpret_cast<uint8_t*>(NetManager()->GetClient(0)); }
int FrameRate() { return GameConfig::g_frameRate; }
int Now() { return FrameClock::GameClockElapsed() + g_clockOffset; }
// The level's rails (the skate module's +0x24).
CGrindRailHub* LevelRails() { return Field<CGrindRailHub*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule), 0x24); }

// How far behind the connection is, in frames of `frameMs`: the latency - the server's connection to the skater's
// player, a client's first (to the server) - less a frame, over a frame, as unsigned numbers (0 without a frame).
// (Quirk kept: a latency under a frame wraps round to a huge lag. The original's check for a negative result is
// left out: it never is.)
float ConnectionLag(uint8_t* app, CGameEntity* skater, int frameMs)
{
    OnlinePlay::Manager* net = NetManager();
    NetLink::Conn* conn;
    if (net->OnServer()) {
        uint8_t* player = net->PlayerByObjectId(static_cast<uint16_t>(skater->m_id));
        conn = Field<NetLink::Conn*>(player, 0x18);
    } else {
        NetLink::ConnSearch search;
        search.m_vtable = NetLink::kConnSearchVtable;
        search.m_listLink = nullptr;
        conn = reinterpret_cast<NetLink::App*>(app)->BeginConnWalk(&search);
    }
    int latency = conn->IsLocal() ? 0 : conn->m_latencyTest.m_averageLatency;
    if (frameMs == 0)
        return 0.0f;
    return static_cast<float>(static_cast<double>(static_cast<uint32_t>(latency - frameMs)) /
                              static_cast<double>(static_cast<uint32_t>(frameMs)));
}

} // namespace

// 0x00515940
void CRiderRemoteSync::Interpolate()
{
    uint8_t* app = ClientApp();
    if (!app || Field<int>(m_pastFrames, 0x18) < kHistory)
        return;
    auto* skater = static_cast<CRider*>(m_object);
    if (m_syncFrame == 0) {
        // Playback starts at the newest state, a fixed time behind it.
        int newest = (Field<int>(m_pastFrames, 0x18) - 1) % kHistory;
        m_syncFrame = Field<uint32_t>(app, 8);
        int delay = static_cast<int>(static_cast<double>(kMsPerSecond) / FrameRate());
        uint8_t* frame = HistoryFrame(m_pastFrames, newest);
        m_playIdx = newest;
        m_syncTime = static_cast<int>(FrameTime(frame) - delay);
        skater->m_basisFrame = FrameMatrix(frame);
        Field<Maths::Matrix>(skater, 0xC0) = FrameMatrix(frame);
        skater->m_pos = FramePosition(frame);
        skater->MarkJumpedAhead(true);
        skater->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
        if (skater->GetFlag2C4())
            return;
        skater->ShowSkater();
        Scripts::LaunchScript(kSkaterAppeared, nullptr, skater, false, nullptr);
        NetManager()->PlayerByObjectId(static_cast<uint16_t>(skater->m_id)); // (its result unused)
        UpdateGroundLighting();
        return;
    }

    // (the original's slerp interpolator is made here: its constructor 0x005902E0 does nothing)
    auto* player = reinterpret_cast<OnlinePlay::PlayerRecord*>(NetManager()->PlayerByObjectId(static_cast<uint16_t>(skater->m_id)));
    if (!player->HasFinishedJoining())
        player->SetFinishedJoining();
    int count = Field<int>(m_pastFrames, 0x18);
    int oldest = count % kHistory; // (the next to be written)
    uint32_t now = Field<uint32_t>(app, 8) - m_syncFrame + m_syncTime;
    uint8_t* last = HistoryFrame(m_pastFrames, (count - 1) % kHistory);
    if (now > FrameTime(last)) {
        // Past the newest state: the playback falls back (by a second's frames, or to the state when further).
        int adjust;
        if (now - FrameTime(last) > 1000)
            adjust = static_cast<int>(FrameTime(last) - now);
        else
            adjust = -1000 / FrameRate();
        m_syncTime += adjust;
        now = Field<uint32_t>(app, 8) - m_syncFrame + m_syncTime;
    }
    // The first state not yet reached, from the oldest (none: nothing played this frame).
    int entry = oldest;
    while (now > FrameTime(HistoryFrame(m_pastFrames, entry))) {
        entry = (entry + 1) % kHistory;
        if (entry == oldest)
            return;
    }
    int lag = (oldest - entry + kHistory) % kHistory;
    m_playIdx = entry;
    g_nonlocalLag = lag;
    if (lag >= 10)
        m_syncTime += 1000 / FrameRate() * 10; // (far behind: ten frames on)
    else if (lag == 0)
        skater->Reset27C();
    uint8_t* from = HistoryFrame(m_pastFrames, (entry + kHistory - 1) % kHistory);
    uint8_t* to = HistoryFrame(m_pastFrames, entry);
    float elapsed = static_cast<float>(static_cast<int>(now - FrameTime(from)));
    int span = static_cast<int>(FrameTime(to) - FrameTime(from));
    float t = static_cast<float>(static_cast<double>(elapsed) / span);
    Maths::Vector move = FramePosition(to) - FramePosition(from);
    m_lastPlayPos = m_playPos;

    // The orientation: slerped, unless the at rows are far apart (then the earlier state's).
    Maths::Vector turn = Field<Maths::Vector>(to, 0x28) - Field<Maths::Vector>(from, 0x28);
    double turned = std::sqrt((static_cast<double>(turn.x) * turn.x + static_cast<double>(turn.z) * turn.z) +
                              static_cast<double>(turn.y) * turn.y);
    if (turned > kTurnSnap) {
        skater->m_basisFrame = FrameMatrix(from);
        Field<Maths::Matrix>(skater, 0xC0) = FrameMatrix(from);
    } else {
        Maths::Matrix start = FrameMatrix(from), end = FrameMatrix(to), between;
        Maths::ArcRotationBlender slerp(start, end);
        slerp.GetMatrix(between, t);
        skater->m_basisFrame = between;
        Field<Maths::Matrix>(skater, 0xC0) = between;
    }

    // The position: interpolated, unless it moved too far (then put at the later state's, standing).
    double distance = std::sqrt((static_cast<double>(move.x) * move.x + static_cast<double>(move.y) * move.y) +
                                static_cast<double>(move.z) * move.z);
    if (distance > kTeleportDistance) {
        skater->m_pos = FramePosition(to);
        skater->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
        skater->MarkJumpedAhead(true);
    } else {
        move = FramePosition(from) + move * t; // (the move's storage now holds the position)
        skater->m_pos = move;
        skater->m_vel = (skater->m_pos - m_lastPlayPos) / m_frameSeconds;
        m_moves[m_updates % kHistory] = skater->m_vel;
        // The velocity: the average of the moves kept - quirk kept: the sum starts from the position, not zero.
        double sum[4] = {move.x, move.y, move.z, move.w};
        int n = 0;
        for (; n < m_updates && n < kHistory; ++n) {
            sum[0] += m_moves[n].x;
            sum[1] += m_moves[n].y;
            sum[2] += m_moves[n].z;
            sum[3] += m_moves[n].w;
        }
        if (n > 0) {
            double inverse = static_cast<double>(kOne) / n;
            float rounded = static_cast<float>(inverse);
            skater->m_vel.x = static_cast<float>(inverse * sum[0]);
            skater->m_vel.y = static_cast<float>(sum[1] * rounded);
            skater->m_vel.z = static_cast<float>(sum[2] * rounded);
            skater->m_vel.w = static_cast<float>(sum[3] * rounded);
        }
    }

    // The state as the later state has it.
    m_playPos = skater->m_pos;
    auto* state = reinterpret_cast<CRiderPoseState*>(m_state);
    state->SetState(Field<int>(to, 0x70));
    Field<uint8_t>(m_state, 0xE0) = Field<uint8_t>(to, 0x74) != 0;
    state->SetSurfaceKind(Field<uint8_t>(to, 0x7C));
    state->SetPhysicsMode(Field<uint8_t>(to, 0x75) != 0);
    Field<uint8_t>(m_state, 0x118) = Field<uint8_t>(to, 0x76) != 0;
    reinterpret_cast<CRiderSessionEnd*>(m_runOver)->m_flags = Field<uint32_t>(to, 0x6C); // (0x00407A70)
    // The timed flags (+0x20 to +0xD8, each a bool and the time it last changed) from the state's bits.
    uint32_t bits = Field<uint32_t>(to, 0x68);
    for (uint32_t i = 0, offset = 0x20; offset < 0xE0; ++i, offset += 8) {
        uint8_t value = (bits & (1u << i)) != 0;
        if (Field<uint8_t>(m_state, offset) != value) {
            Field<uint8_t>(m_state, offset) = value;
            Field<int>(m_state, offset + 4) = Now();
        }
    }
    if (Field<uint8_t>(m_state, 0x78))
        Field<Maths::Vector>(m_state, 0xE4) = skater->m_vel;
    else
        Field<Maths::Vector>(m_state, 0xE4) = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00515360
void CRiderRemoteSync::Extrapolate()
{
    OnlinePlay::Manager* net = NetManager();
    uint8_t* app = ClientApp();
    int frameMs = static_cast<int>(static_cast<double>(m_frameSeconds) * kMsPerSecond);
    float lag = ConnectionLag(app, m_object, frameMs);
    int slot = m_updates % kHistory;
    float dw = m_playPos.w - m_lastPlayPos.w;
    float dz = m_playPos.z - m_lastPlayPos.z;
    float dy = m_playPos.y - m_lastPlayPos.y;
    float dx = m_playPos.x - m_lastPlayPos.x;
    float dirX = dx, dirZ = dz;
    // How well the move lines up with this machine's skater's (0 to 1; 0 without one).
    double alignment = 0.0;
    auto* local = static_cast<uint8_t*>(net->GetOwnPlayer());
    if (local && Field<CGameEntity*>(local, 0x14)) {
        Maths::Vector theirs = Field<CGameEntity*>(local, 0x14)->m_vel;
        Maths::Vector mine = {dx, dy, dz, dw};
        theirs.Normalize();
        mine.Normalize();
        alignment = ((static_cast<double>(mine.x) * theirs.x + static_cast<double>(mine.z) * theirs.z) +
                     static_cast<double>(mine.y) * theirs.y + kOne) *
                    kHalf;
    }
    double squared = (static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz;
    float squaredRounded = static_cast<float>(squared);
    m_speeds[slot] = static_cast<float>(std::sqrt(squared) / static_cast<double>(static_cast<uint32_t>(frameMs)) *
                                        kSpeedScale);
    m_lags[slot] = lag;
    m_alignments[slot] = static_cast<float>(alignment);
    ++m_updates;

    // The averages over the updates kept.
    double speeds = 0.0, lags = 0.0, alignments = 0.0;
    int n = 0;
    for (; n < m_updates && n < kHistory; ++n) {
        speeds += m_speeds[n];
        lags += m_lags[n];
        alignments += m_alignments[n];
    }
    double inverse = static_cast<double>(kOne) / n;
    float rounded = static_cast<float>(inverse);
    float speed = static_cast<float>(inverse * speeds);
    double scale = (rounded * lags) * (alignments * rounded);
    // On along the last move's direction (flattened: y isn't moved) by the average speed, times the average lag and
    // alignment.
    double length = std::sqrt(static_cast<double>(squaredRounded));
    if (length > 0.0) {
        double toUnit = static_cast<double>(kOne) / length;
        dirX = static_cast<float>(dx * toUnit);
        dirZ = static_cast<float>(toUnit * dz);
    }
    float moveW = static_cast<float>(static_cast<double>(dw) * speed * scale);
    float moveZ = static_cast<float>(static_cast<double>(dirZ) * speed * scale);
    float moveX = static_cast<float>(static_cast<double>(dirX) * speed * scale);
    Maths::Vector& pos = m_object->m_pos;
    pos.w += moveW;
    pos.z += moveZ;
    pos.x += moveX;

    // A wall between the played position and the new one (both the collision height up the up row): bounced off.
    CGameEntity* skater = m_object;
    float height = static_cast<float>(ReadTuningFloat(kFirstForwardCollisionHeight, 1, skater));
    const Maths::Vector& up = skater->m_basisFrame.row[1];
    float raiseW = height * up.w, raiseZ = height * up.z, raiseY = height * up.y, raiseX = height * up.x;
    Maths::Vector from = {raiseX + m_playPos.x, raiseY + m_playPos.y, raiseZ + m_playPos.z, raiseW + m_playPos.w};
    Maths::Vector to = {raiseX + pos.x, raiseY + pos.y, raiseZ + pos.z, raiseW + pos.w};
    Engine3D::CProbeRay feeler;
    feeler.Construct();
    double ex = static_cast<double>(from.x) - to.x, ey = static_cast<double>(from.y) - to.y,
           ez = static_cast<double>(from.z) - to.z;
    if (std::sqrt((ez * ez + ey * ey) + ex * ex) > kWallCheckReach)
        return;
    feeler.SetLine(&from, &to);
    feeler.SetExcluded(0x10, 0);
    if (feeler.GetHitShape(true, false))
        BounceOffWall(&feeler.m_surfaceDir, &feeler.m_contactPos);
}

// 0x00516640
CGrindPoint* CRiderRemoteSync::MoveAlongRail(CGrindPoint* node, float seconds)
{
    CGrindRailHub* rails = LevelRails();
    CGrindPoint* next = node->m_next;
    if (!next)
        return nullptr;
    CGameEntity* skater = m_object;
    Maths::Vector a, b, p1, p2;
    rails->GetAbsolutePos(&a, node);
    rails->GetAbsolutePos(&b, next);
    double sx = static_cast<double>(b.x) - a.x, sy = static_cast<double>(b.y) - a.y,
           sz = static_cast<double>(b.z) - a.z, sw = static_cast<double>(b.w) - a.w;
    float length = static_cast<float>(std::sqrt((sx * sx + sy * sy) + sz * sz));
    double inverse = static_cast<double>(kOne) / length;
    float rounded = static_cast<float>(inverse);
    Maths::Vector along = {static_cast<float>(inverse * sx), static_cast<float>(rounded * sy),
                         static_cast<float>(rounded * sz), static_cast<float>(rounded * sw)};
    // The rail's gravity along it.
    float gravity = ReadTuningFloat(kRailGravity, 1, skater);
    Maths::Vector pull = {0.0f, gravity, 0.0f, 1.0f};
    pull.ProjectOnto(along);
    Maths::Vector& vel = skater->m_vel;
    vel.x = static_cast<float>(static_cast<double>(pull.x) * seconds + vel.x);
    vel.y = static_cast<float>(static_cast<double>(pull.y) * seconds + vel.y);
    vel.z = static_cast<float>(pull.z * seconds) + vel.z;
    vel.w = static_cast<float>(pull.w * seconds) + vel.w;
    float sign = -1.0f;
    if (!((static_cast<double>(along.y) * vel.y + static_cast<double>(along.z) * vel.z) +
              static_cast<double>(along.x) * vel.x <
          0.0))
        sign = 1.0f;

    // Whether the rail goes on round the corner ahead (else the skater stops at its end): the next segment there
    // active and turning less than Rail_Corner_Leave_Angle (both flattened).
    bool stopped = false;
    CGrindPoint* beyond = sign > 0.0f ? next->m_next : node->m_lastValue;
    if (!beyond || !(beyond->m_flags & kRailActive)) {
        stopped = true;
    } else {
        Maths::Vector here, there;
        if (sign > 0.0f) {
            rails->GetAbsolutePos(&p1, next);
            rails->GetAbsolutePos(&p2, node);
            here = p2 - p1;
            rails->GetAbsolutePos(&p1, beyond);
            rails->GetAbsolutePos(&p2, next);
            there = p2 - p1;
        } else {
            rails->GetAbsolutePos(&p1, node);
            rails->GetAbsolutePos(&p2, next);
            here = p2 - p1;
            rails->GetAbsolutePos(&p1, beyond);
            rails->GetAbsolutePos(&p2, node);
            there = p2 - p1;
        }
        here.y = 0.0f;
        there.y = 0.0f;
        here.Normalize();
        there.Normalize();
        float cosine = static_cast<float>((static_cast<double>(there.z) * here.z + static_cast<double>(there.y) * here.y) +
                                          static_cast<double>(there.x) * here.x);
        double limit = Skater::X87Cos(static_cast<double>(ReadTuningFloat(kRailCornerLeaveAngle, 1, skater)) *
                                      kDegreesToRadians);
        if (limit > cosine)
            stopped = true;
    }

    // Past the segment's end (further from its other end than its length): onto the next segment, if it's there,
    // clear and not round too sharp a corner - the overshoot carried on along it.
    float overshoot = 0.0f;
    rails->GetAbsolutePos(&p1, sign > 0.0f ? node : next);
    const Maths::Vector& pos = skater->m_pos;
    double dx = static_cast<double>(pos.x) - p1.x, dy = static_cast<double>(pos.y) - p1.y,
           dz = static_cast<double>(pos.z) - p1.z;
    double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (distance > static_cast<double>(length) + kRailEndSlack) {
        overshoot = static_cast<float>(distance - length);
        // (quirk kept: going forward, the segment's own end is checked for being active, not the node beyond it)
        if (!beyond || !((sign > 0.0f ? next : beyond)->m_flags & kRailActive))
            return nullptr;
        if (sign > 0.0f) {
            Maths::Vector to, from;
            rails->GetAbsolutePos(&to, beyond);
            rails->GetAbsolutePos(&from, next);
            if (!RailClear(from, to) || stopped)
                return nullptr;
            rails->GetAbsolutePos(&p1, next);
            skater->m_pos = p1;
            node = next;
        } else {
            Maths::Vector to, from;
            rails->GetAbsolutePos(&to, beyond);
            rails->GetAbsolutePos(&from, node);
            if (!RailClear(from, to) || stopped)
                return nullptr;
            rails->GetAbsolutePos(&p1, node);
            skater->m_pos = p1;
            node = beyond;
        }
    }

    // Along the segment it's on: the velocity and the matrix turned along it (the way it's going), the move.
    CGrindPoint* end = node->m_next;
    rails->GetAbsolutePos(&p1, node);
    rails->GetAbsolutePos(&p2, end);
    Maths::Vector segment = {p2.x - p1.x, p2.y - p1.y, p2.z - p1.z, p2.w - p1.w};
    segment.Normalize();
    sign = -1.0f;
    if (!((static_cast<double>(segment.z) * vel.z + static_cast<double>(segment.y) * vel.y) +
              static_cast<double>(segment.x) * vel.x <
          0.0))
        sign = 1.0f;
    vel.SetDirection(segment);
    vel.x *= sign;
    vel.y *= sign;
    vel.z *= sign;
    vel.w *= sign;
    Maths::Vector heading = {segment.x * sign, segment.y * sign, segment.z * sign, segment.w * sign};
    Maths::Matrix& m = skater->m_basisFrame;
    m.row[2] = heading;
    m.row[2].Normalize();
    m.row[0] = {m.row[2].z, 0.0f, -m.row[2].x, 0.0f};
    m.row[0].Normalize();
    const Maths::Vector& r = m.row[0];
    const Maths::Vector& at = m.row[2];
    float upX = static_cast<float>(static_cast<double>(r.z) * at.y - static_cast<double>(r.y) * at.z);
    float upY = static_cast<float>(static_cast<double>(r.x) * at.z - static_cast<double>(at.x) * r.z);
    float upZ = static_cast<float>(static_cast<double>(at.x) * r.y - static_cast<double>(r.x) * at.y);
    m.row[1] = {upX, upY, upZ, 0.0f};
    m.row[1].Normalize();
    Maths::Vector& p = skater->m_pos;
    p.x = static_cast<float>(static_cast<double>(seconds) * vel.x + p.x);
    p.y = static_cast<float>(static_cast<double>(seconds) * vel.y + p.y);
    p.z = static_cast<float>(seconds * vel.z) + p.z;
    p.w = static_cast<float>(seconds * vel.w) + p.w;
    p.x = static_cast<float>(static_cast<double>(heading.x) * overshoot + p.x);
    p.y = static_cast<float>(static_cast<double>(heading.y) * overshoot + p.y);
    p.z = static_cast<float>(heading.z * overshoot) + p.z;
    p.w = static_cast<float>(heading.w * overshoot) + p.w;
    return node;
}

// 0x00516E00
void CRiderRemoteSync::ExtrapolateOnRail()
{
    CGrindRailHub* rails = LevelRails();
    OnlinePlay::Manager* net = NetManager();
    uint8_t* app = ClientApp();
    int frameMs = static_cast<int>(static_cast<double>(m_frameSeconds) * kMsPerSecond);
    float lag = ConnectionLag(app, m_object, frameMs);
    int slot = m_updates % kHistory;
    float dx = m_playPos.x - m_lastPlayPos.x;
    float dy = m_playPos.y - m_lastPlayPos.y;
    float dz = m_playPos.z - m_lastPlayPos.z;
    float dw = m_playPos.w - m_lastPlayPos.w;
    double alignment = 0.0;
    auto* local = static_cast<uint8_t*>(net->GetOwnPlayer());
    if (local) { // (its skater isn't checked here)
        Maths::Vector theirs = Field<CGameEntity*>(local, 0x14)->m_vel;
        Maths::Vector mine = {dx, dy, dz, dw};
        theirs.Normalize();
        mine.Normalize();
        alignment = ((static_cast<double>(mine.z) * theirs.z + static_cast<double>(mine.y) * theirs.y) +
                     static_cast<double>(mine.x) * theirs.x + kOne) *
                    kHalf;
    }
    double squared = (static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
    m_speeds[slot] = static_cast<float>(std::sqrt(squared) / static_cast<double>(static_cast<uint32_t>(frameMs)) *
                                        kSpeedScale);
    m_lags[slot] = lag;
    m_alignments[slot] = static_cast<float>(alignment);
    ++m_updates;
    double lags = 0.0, alignments = 0.0;
    int n = 0;
    for (; n < m_updates && n < kHistory; ++n) {
        lags += m_lags[n];
        alignments += m_alignments[n];
    }
    double inverse = static_cast<double>(kOne) / n;
    // How far along the rail: the frame's length times the average alignment and lag (at most one second's worth).
    double ahead = ((inverse * alignments) * (inverse * lags)) * m_frameSeconds;
    float left = static_cast<float>(ahead);
    if (ahead > kOne)
        left = kOne;

    CGrindPoint* node = rails->FindNode(Field<uint32_t>(HistoryFrame(m_pastFrames, m_playIdx), 0x80));
    if (!node || !(left > 0.0f))
        return;
    do {
        node = MoveAlongRail(node, left > m_frameSeconds ? m_frameSeconds : left);
        if (!node) {
            // Off the rail's end: from the played position, as on the ground.
            m_object->m_pos = m_playPos;
            Extrapolate();
            return;
        }
        left = left - m_frameSeconds;
    } while (left > 0.0f);
}

// 0x00516180 (its EAX - left over from the loop - isn't read)
void CRiderRemoteSync::PlayHistoryEvents()
{
    uint8_t* app = ClientApp();
    if (!app || Field<int>(m_pastFrames, 0x18) < kHistory)
        return;
    int first = Field<int>(m_pastFrames, 0xA6C) % kHistory;
    uint32_t now = Field<uint32_t>(app, 8) - m_syncFrame + m_syncTime;
    auto* messages = reinterpret_cast<StateHistoryMessage*>(m_pastFrames + 0xA70);
    auto* blender = static_cast<CAnimBlenderComponent*>(m_animBlender);
    int i = first;
    do {
        StateHistoryMessage* msg = &messages[i];
        uint8_t* m = reinterpret_cast<uint8_t*>(msg);
        if (msg->GetTime() > m_lastEventTime && msg->GetTime() <= now) {
            switch (msg->m_id) {
            case 0x29: // an animation played (the primary channel)
                if (Gfx2D::CBlendChannel* channel = blender->FindChannel(0))
                    channel->PlayAnim(Field<uint32_t>(m, 0x20), false, Field<float>(m, 0x10), Field<float>(m, 0x14),
                                      Field<int8_t>(m, 0x02), Field<float>(m, 0x18), Field<float>(m, 0x1C), nullptr);
                break;
            case 0x2A: // the board rotated (the byte passed as it was sent)
                static_cast<CRiderSpinAndFlip*>(m_flipAndRotate)
                    ->SetBoardRotated(static_cast<uint32_t>(Field<int8_t>(m, 0x01)), Field<bool>(m, 0x09), 0, false);
                break;
            case 0x2B:
                if (Gfx2D::CBlendChannel* channel = blender->FindChannel(0))
                    channel->ChooseWobbleGoal(Field<float>(m, 0x0C), false);
                break;
            case 0x2C:
                if (Gfx2D::CBlendChannel* channel = blender->FindChannel(0))
                    channel->SetFlipped(static_cast<uint32_t>(Field<int8_t>(m, 0x01)), Field<bool>(m, 0x08), 0,
                                        false); // (the byte passed as it was sent)
                break;
            case 0x3C: { // (20 bytes of the message passed by address)
                uint32_t data[5];
                std::memcpy(data, m + 0x28, sizeof(data));
                if (Gfx2D::CBlendChannel* channel = blender->FindChannel(0))
                    channel->SetWobbleDetails(reinterpret_cast<const float*>(data), false);
                break;
            }
            case 0x3D:
                if (Gfx2D::CBlendChannel* channel = blender->FindChannel(0))
                    channel->SetLoopType(Field<int8_t>(m, 0x02), false);
                break;
            case 0x3E:
                reinterpret_cast<CMeshInstanceModule*>(m_meshSet)->ConcealMesh(Field<uint32_t>(m, 0x20),
                                                                      Field<uint8_t>(m, 0x0A) != 0, false);
                break;
            case 0x41:
                if (Gfx2D::CBlendChannel* channel = blender->FindChannel(0))
                    channel->SetSpeed(Field<float>(m, 0x1C), false, false, false);
                break;
            case 0x76: { // the model's three parts set going, by the message's bits
                Field<Maths::Vector>(m_meshSet, 0x28) = {0.0f, 30.0f, 0.0f, 1.0f};
                int time = Now();
                for (uint32_t bit = 0; bit < 3; ++bit) {
                    if (Field<uint8_t>(m, 0x03) & (1u << bit))
                        reinterpret_cast<CModelSpinAxis*>(m_meshSet + kModelParts[bit])
                            ->Add(static_cast<float>(Field<int>(m, 0x3C)), static_cast<uint32_t>(time),
                                  Field<float>(m, 0x48), Field<float>(m, 0x4C), Field<int>(m, 0x40),
                                  Field<uint8_t>(m, 0x50) != 0, false);
                }
                break;
            }
            case 0x77: { // a special item made: its +0x5CB5CB7E component told its name and owner
                auto* items = static_cast<CPickupItemHandler*>(m_object->GetComponent(CPickupItemHandler::kType));
                Scripts::CStruct* structure = Scripts::ReadSubStruct(Field<uint32_t>(m, 0x20)); // (the original also passes 1)
                CGameEntity* item = items->CreateItem(Field<int>(m, 0x44), structure);
                Scripts::CStruct* params = Scripts::NewStruct();
                if (params)
                    params = params->Init();
                params->PutNameKey(kItemName, Field<uint32_t>(m, 0x24));
                params->PutNameKey(kItemOwner, m_object->m_id);
                item->GetComponent(kItemComponent)->ConfigureFromData(params);
                if (params) {
                    params->Clear();
                    Scripts::DeleteStruct(params);
                }
                break;
            }
            case 0x78: // the special item gone
                static_cast<CPickupItemHandler*>(m_object->GetComponent(CPickupItemHandler::kType))
                    ->DestroyItem(Field<int>(m, 0x44));
                break;
            case 0x8C:
                for (uint32_t part : kModelParts)
                    reinterpret_cast<CModelSpinAxis*>(m_meshSet + part)->Clear();
                break;
            default:
                break;
            }
        }
        i = (i + 1) % kHistory;
    } while (i != first);
    m_lastEventTime = now;
}

} // namespace Obj
