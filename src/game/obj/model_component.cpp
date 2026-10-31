// The model component (Obj::CModelComponent): its display turns (CModelSpin / CModelSpinAxis), the display matrix and
// the smaller members - see model_component.h. From the disassembly of 0x00420070-0x00420A46 and 0x004213E0-0x004219EC.
// The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the FPU stack are
// worked in double.

#include "game/obj/model_component.h"
#include "game/gfx/d3dx.h"

#include <cstdint>
#include <cstring>

#include "game/gfx/engine_model_lights.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/model.h"
#include "game/mth/matrix.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/nx/feeler.h"
#include "game/script/names.h"
#include "game/core/dbg.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/obj/control_pad.h"

namespace Obj {

namespace {

constexpr float kHalfPi = 1.57079637f;         // 0x00645484
constexpr float kFramesToMs = 16.666666f;      // 0x006460DC (1000 / 60)
constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464
constexpr float kLerpRateScale = 0.02f;        // 0x00646124
constexpr double kTwoTo32 = 4294967296.0;      // 0x006453B4

constexpr uint8_t kMsgHideGeom = 0x3E;
constexpr uint8_t kMsgRotateDisplay = 0x76;
constexpr uint8_t kMsgCancelRotateDisplay = 0x8C;

// The x87 FSIN, as the originals take it.
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

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// Nx::CModel (not rewritten yet): hide it (0x00494B70), a geom hidden or shown (0x00494800), its texture `src`
// replaced (0x00493E80), geom `index` (0x00494640), the bone volumes (0x00492FC0) and a line tested against them
// (0x004939E0), and its lights (+0x2C, the folded getter 0x00532EB0).
void ModelHide(Nx::CModel* model, bool hide) { model->Hide(hide); }
void ModelHideGeom(Nx::CModel* model, uint32_t name, bool hide)
{
    model->HideGeom(name, hide);
}
bool ModelReplaceTexture(Nx::CModel* model, uint32_t mesh, const char* src, const char* dest)
{
    return model->ReplaceTextureWith(mesh, src, reinterpret_cast<uint32_t>(dest));
}
void** ModelGeom(Nx::CModel* model, int index)
{
    return static_cast<void**>(model->GetGeom(index));
}
void* ModelBoneVolumes(Nx::CModel* model) { return model->GetBoneVolumes(); }
int ModelTestCollision(Nx::CModel* model, Mth::Vector* line, void* skeleton, Mth::Matrix* matrix, uint32_t a,
                       uint32_t b, uint32_t c, uint32_t d)
{
    // (the original's seven stack arguments: the line, skeleton and matrix pointers, then a, b, c and d - the extra
    // result's pointer)
    return model->TestBoneVolumes(reinterpret_cast<uint32_t>(line), reinterpret_cast<uint32_t>(skeleton),
                                  reinterpret_cast<uint32_t>(matrix), a, b, c, reinterpret_cast<uint32_t*>(d));
}
void* ModelLights(Nx::CModel* model) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x2C); }
// The model's lights (not rewritten yet): their brightness from the level's (0x0048E660), set at once (0x0048E8A0)
// or lerped towards (0x0048E830); without model lights, the global brightness (0x0048EDB0).
void LightsUpdateBrightness(void* lights) { static_cast<Nx::CModelLights*>(lights)->UpdateBrightness(); }
void LightsSetBrightness(void* lights, float brightness)
{
    static_cast<Nx::CModelLights*>(lights)->SetBrightness(brightness);
}
void LightsLerpBrightness(void* lights, float brightness, float rate)
{
    static_cast<Nx::CModelLights*>(lights)->MoveBrightness(brightness, rate);
}
void SetGlobalBrightness(float brightness) { Nx::SetWorldBrightness(brightness); }

// A skeleton component's skeleton (0x005480F0: its +0x18).
void* SkeletonOf(CBaseComponent* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18);
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The network manager (0x007CCE14) and its players (not rewritten yet): the player with an object id (0x0053AEA0),
// whether it's this machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
void* NetManager() { return reinterpret_cast<void*>(GameNet::g_gameNetManager); }
void* PlayerByObjectId(uint32_t id)
{
    return static_cast<GameNet::Manager*>(NetManager())->PlayerByObjectId(static_cast<uint16_t>(id));
}
bool IsLocalPlayer(void* player) { return static_cast<GameNet::PlayerInfo*>(player)->IsLocalPlayer() != 0; }
int PlayerSkaterIndex(void* player) { return static_cast<int>(static_cast<GameNet::PlayerInfo*>(player)->GetSkaterValue2BC()); }
Net::Client* ClientApp(void* manager, int index)
{
    return static_cast<Net::Client*>(static_cast<GameNet::Manager*>(manager)->GetClient(index));
}

// A message about `object` to the server, when its player is this machine's.
void SendToServer(CCompositeObject* object, uint8_t id, uint16_t length, void* data)
{
    void* manager = NetManager();
    void* player = PlayerByObjectId(object->m_id);
    if (!player || !IsLocalPlayer(player))
        return;
    Net::MsgDesc message;
    message.Construct();
    Net::Client* app = ClientApp(manager, PlayerSkaterIndex(player));
    message.m_data = data;
    message.m_length = length;
    message.m_id = id;
    app->EnqueueMessageToServer(&message);
}

} // namespace

// 0x00420590
double CModelSpin::Angle()
{
    if (!(m_flags & MODEL_SPIN_ON))
        return 0.0;
    int elapsed = m_flags & MODEL_SPIN_REAL_TIME ? static_cast<int>(Tmr::GetTime() - m_start)
                                                  : Tmr::GameClockTimeSince(static_cast<int>(m_start));
    double t = static_cast<double>(elapsed);
    if (elapsed < 0)
        t += kTwoTo32; // (as unsigned)
    if (t > m_period) { // (a NaN period never ends)
        if (m_flags & MODEL_SPIN_HOLD)
            return static_cast<double>(m_range) + m_base;
        m_flags &= ~MODEL_SPIN_ON;
        return 0.0;
    }
    if (m_power == 0)
        return static_cast<double>(m_range) / m_period * t + m_base;
    double phase = t / m_period * kHalfPi;
    if (m_power < 0)
        return (1.0 - X87Sin(kHalfPi - phase)) * m_range + m_base;
    double s = X87Sin(phase);
    for (int i = m_power - 1; i > 0; --i)
        s *= s;
    return s * m_range + m_base;
}

// 0x004204F0
void CModelSpinAxis::Clear()
{
    for (CModelSpin& spin : m_spins)
        spin.m_flags &= ~(MODEL_SPIN_ON | MODEL_SPIN_HOLD);
    m_active = false;
}

// 0x00420520
void CModelSpinAxis::Add(float period, uint32_t start, float base, float range, int power, bool hold, bool realTime)
{
    for (CModelSpin& spin : m_spins) {
        if (spin.m_flags & MODEL_SPIN_ON)
            continue;
        spin.m_period = period;
        spin.m_start = start;
        spin.m_base = base;
        spin.m_range = range;
        spin.m_power = power;
        spin.m_flags = static_cast<uint8_t>((spin.m_flags & ~(MODEL_SPIN_HOLD | MODEL_SPIN_REAL_TIME)) |
                                            (realTime ? MODEL_SPIN_REAL_TIME : 0) | (hold ? MODEL_SPIN_HOLD : 0) |
                                            MODEL_SPIN_ON);
        m_active = true;
        return;
    }
}

// 0x004213E0
float CModelSpinAxis::Angle()
{
    float angle = 0.0f;
    if (!m_active)
        return angle;
    m_active = false;
    for (CModelSpin& spin : m_spins) {
        if (spin.m_flags & MODEL_SPIN_ON) {
            m_active = true;
            angle = static_cast<float>(spin.Angle() + angle);
        }
    }
    return angle;
}

// 0x004215F0
void CModelComponent::GetDisplayMatrix(Mth::Matrix* out)
{
    *out = m_object->m_displayMatrix;
    out->row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (m_flip) {
        out->row[2] = {-out->row[2].x, -out->row[2].y, -out->row[2].z, -out->row[2].w};
        out->row[0] = {-out->row[0].x, -out->row[0].y, -out->row[0].z, -out->row[0].w};
    }
    // The spins: about X, then Y, then Z (each one that isn't 0 multiplied in).
    Mth::Matrix rotation;
    bool none = true;
    using CreateFn = Mth::Matrix& (*)(Mth::Matrix&, float);
    static const CreateFn kCreate[3] = {Mth::CreateRotateXMatrix, Mth::CreateRotateYMatrix, Mth::CreateRotateZMatrix};
    for (int axis = 0; axis < 3; ++axis) {
        if (!m_spin[axis].m_active)
            continue;
        float angle = m_spin[axis].Angle();
        if (angle == 0.0f)
            continue;
        if (none)
            rotation.Ident();
        none = false;
        Mth::Matrix turn;
        MatrixMultiply(&rotation, &rotation, &kCreate[axis](turn, angle * kDegreesToRadians));
    }
    Mth::Vector shift = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!none) {
        // About the pivot: the pivot less where the turn takes it, in the display's frame.
        shift = m_pivot;
        if (shift.x != 0.0f || shift.y != 0.0f || shift.z != 0.0f) {
            Mth::Vector turned = shift * rotation;
            shift = {shift.x - turned.x, shift.y - turned.y, shift.z - turned.z, shift.w - turned.w};
            shift = shift * *out;
        }
        MatrixMultiply(out, &rotation, out);
    }
    Mth::Vector& pos = out->row[3];
    pos = m_object->m_pos;
    pos.x = shift.x + pos.x;
    pos.y = shift.y + pos.y;
    pos.z = shift.z + pos.z;
    pos.w = shift.w + pos.w;
    pos.x = m_offset.x + pos.x;
    pos.y = m_offset.y + pos.y;
    pos.z = m_offset.z + pos.z;
    pos.w = m_offset.w + pos.w;
    pos.w = 1.0f;
}

// 0x00421960
int CModelComponent::TestCollision(Mth::Vector* line, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    void* skeleton = SkeletonOf(mp_skeleton); // (read before the check: the original's crashes without one)
    void* volumes = ModelBoneVolumes(mp_model);
    if (!mp_skeleton)
        return 0;
    if (!volumes) {
        Dbg::Printf("No collision data for %s model\n", Script::FindChecksumNameOrUnknown(m_object->m_id));
        return 0;
    }
    Mth::Matrix matrix;
    GetDisplayMatrix(&matrix);
    return ModelTestCollision(mp_model, line, skeleton, &matrix, a, b, c, d);
}

// 0x00420070
void CModelComponent::Hide(bool hide)
{
    if (mp_model)
        ModelHide(mp_model, hide);
}

// 0x00420090
void CModelComponent::SetColorValue(int index, float value)
{
    if (mp_model && index >= 1 && index <= m_colorValueCount)
        m_colorValues[index - 1] = value;
}

// 0x004200C0
void CModelComponent::UpdateBrightness() { LightsUpdateBrightness(ModelLights(mp_model)); }

// 0x004200E0
void CModelComponent::RebuildFromProfile(void* profile, uint32_t buildScript)
{
    if (mp_model)
        reinterpret_cast<void(__thiscall*)(CModelComponent*, void*, bool, int, uint32_t)>(m_vtable[13])(
            this, profile, true, 0, buildScript);
}

// 0x00420110
bool CModelComponent::ReplaceTexture(uint32_t mesh, const char* src, const char* dest)
{
    return mp_model && ModelReplaceTexture(mp_model, mesh, src, dest);
}

// 0x004202A0
bool CModelComponent::HideGeom(uint32_t name, bool hide, bool send)
{
    if (send) {
        struct {
            uint32_t name;
            uint8_t object;
            uint8_t hide;
            uint8_t pad[2];
        } data;
        data.hide = hide;
        data.name = name;
        data.object = static_cast<uint8_t>(m_object->m_id);
        SendToServer(m_object, kMsgHideGeom, 8, &data);
    }
    if (mp_model)
        ModelHideGeom(mp_model, name, hide);
    return true;
}

// 0x00420670
void CModelComponent::ApplyBrightness(Nx::CFeeler* feeler, bool lerp, float rate)
{
    if (!feeler->IsHitTriangleCollision())
        return;
    void* lights = ModelLights(mp_model);
    double value = feeler->GetHitVertexValue();
    if (!lights) {
        SetGlobalBrightness(static_cast<float>(value));
        return;
    }
    double doubled = value + value;
    m_brightness = static_cast<float>(doubled);
    if (lerp)
        LightsLerpBrightness(lights, static_cast<float>(doubled * 0.5), rate * kLerpRateScale);
    else
        LightsSetBrightness(lights, static_cast<float>(doubled * 0.5));
}

// 0x00420700
void CModelComponent::SetColor(uint32_t color)
{
    if (!m_sectorModel)
        return;
    void** geom = ModelGeom(mp_model, 0);
    reinterpret_cast<void(__thiscall*)(void**, uint32_t)>((*reinterpret_cast<void***>(geom))[6])(geom, color);
}

// 0x00420730
void CModelComponent::RotateDisplay(float duration, float start, float end, int power, bool hold, bool x, bool y,
                                    bool z)
{
    float period = duration * kFramesToMs;
    m_pivot = {0.0f, 30.0f, 0.0f, 1.0f};
    uint32_t now = static_cast<uint32_t>(Tmr::GameClockTime());
    uint8_t axes = 0;
    bool flags[3] = {x, y, z};
    for (int axis = 0; axis < 3; ++axis) {
        if (!flags[axis])
            continue;
        m_spin[axis].Add(period, now, start, end - start, power, hold, false);
        axes |= static_cast<uint8_t>(1 << axis);
    }
    struct {
        int period;
        int power;
        int16_t start;
        int16_t range;
        uint8_t object;
        uint8_t hold;
        uint8_t axes;
        uint8_t pad;
    } data;
    void* manager = NetManager();
    void* player = PlayerByObjectId(m_object->m_id);
    if (!player || !IsLocalPlayer(player))
        return;
    Net::MsgDesc message;
    message.Construct();
    Net::Client* app = ClientApp(manager, PlayerSkaterIndex(player));
    data.period = Truncated(period);
    data.start = static_cast<int16_t>(Truncated(start));
    data.range = static_cast<int16_t>(Truncated(static_cast<double>(end) - start));
    data.power = power;
    data.object = static_cast<uint8_t>(m_object->m_id);
    message.m_data = &data;
    data.hold = hold;
    data.axes = axes;
    message.m_length = 0x10;
    message.m_id = kMsgRotateDisplay;
    app->EnqueueMessageToServer(&message);
}

// 0x004208D0
void CModelComponent::CancelRotateDisplay()
{
    for (CModelSpinAxis& axis : m_spin) {
        for (CModelSpin& spin : axis.m_spins)
            spin.m_flags &= ~(MODEL_SPIN_ON | MODEL_SPIN_HOLD);
        axis.m_active = false;
    }
    uint8_t object = static_cast<uint8_t>(m_object->m_id);
    SendToServer(m_object, kMsgCancelRotateDisplay, 1, &object);
}

} // namespace Obj

namespace Tmr {

// 0x00420250
int GameClockTimeSince(int start) { return GameClockElapsed() + (Obj::g_clockOffset - start); }

} // namespace Tmr
