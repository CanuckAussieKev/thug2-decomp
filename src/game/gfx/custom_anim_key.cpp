// An animation's custom keys (see custom_anim_key.h). From the disassembly of 0x004860C0-0x00486875.

#include <cstdint>

#include "game/gfx/custom_anim_key.h"

#include "game/core/mem.h"
#include "game/gfx/anim_track.h"
#include "game/gfx/view_cam.h"
#include "game/obj/cutscene.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/mc/memcard.h"

namespace Gfx2D {

namespace {

void** const& kBaseVtable = Vt::kAnimEventBase;
void** const& kScriptKeyVtable = Vt::kAnimEvent;
extern void** const kFovVtable;
extern void** const kScriptVtable;
extern void** const kMarkerVtable; // (kind 3: no data; its Process is the shared "true" 0x005BC7F0)
extern void** const kStructureVtable;
extern void** const kKillVtable;
extern void** const kCameraVtable;

constexpr uint32_t kDebugGlobal = 0x2F510F45;     // (a script global each Process reads - and ignores)
constexpr uint32_t kSetHFOVRadians = 0x44EA2B6D;  // the camera's member function (name unknown)
constexpr uint32_t kStructureScript = 0xBDF7F843; // (name unknown) the script a structure key runs
constexpr uint32_t kName = 0xA1DC81F9;            // name

// Each Process reads the global 0x2F510F45 first (compiled-out debug output, presumably).
void ReadDebugGlobal() { Scripts::FetchIntValue(kDebugGlobal); }

// A key of `size` bytes with its frame, inactive (from MemSys::Alloc; null without memory).
template <typename T>
T* NewKey(void** vtable, int frame, uint32_t size = sizeof(T))
{
    auto* key = static_cast<T*>(MemSys::Alloc(size));
    if (!key)
        return nullptr;
    key->m_tick = frame;
    key->m_armed = false;
    key->m_vtable = vtable;
    return key;
}

} // namespace

// 0x004860C0
bool CUserClipKey::InsideRange(float start, float end, bool inclusive)
{
    double frame = m_tick;
    if (inclusive) {
        if (start < end)
            return frame >= start && frame <= end;
        return frame <= start && frame >= end;
    }
    if (start < end)
        return frame >= start && frame < end;
    return frame <= start && frame > end;
}

// 0x00486150
void CUserClipKey::ChangeLive(bool active) { m_armed = active; }

// 0x00486160
bool CUserClipKey::ProcessIfActive(void* object)
{
    if (!m_armed)
        return false;
    m_armed = false;
    return reinterpret_cast<bool(__thiscall*)(CUserClipKey*, void*)>(m_vtable[2])(this, object);
}

// 0x00486460
CUserClipKey* CUserClipKey::Teardown(unsigned int flags)
{
    reinterpret_cast<CAnimEventBase*>(this)->DestroyBody(); // (the base's destructor body: its vtable)
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004868A0
bool CCameraFovKey::WorkOn(void* camera)
{
    ReadDebugGlobal();
    Scripts::CStruct* memory = Scripts::NewStruct();
    Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
    params->PushFloat(0u, m_viewAngle);
    Scripts::LaunchScript(kSetHFOVRadians, params, static_cast<GameObjects::CEntity*>(camera), false, nullptr);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    return true;
}

// 0x00486180
bool CScriptKey::WorkOn(void* object)
{
    ReadDebugGlobal();
    if (m_runScript == 0x4E917BDA)
        m_runScript = 0x69F35FA0;
    else if (m_runScript == 0x208D7A0D)
        m_runScript = 0x04D887B1;
    Scripts::LaunchScript(m_runScript, nullptr, static_cast<GameObjects::CEntity*>(object), false, nullptr);
    return true;
}

// 0x00486300
bool CStructureScriptKey::WorkOn(void*)
{
    ReadDebugGlobal();
    if (Scripts::CStruct* structure = Scripts::ReadSubStruct(m_structure))
        Scripts::LaunchScript(kStructureScript, structure, nullptr, false, nullptr);
    return true;
}

// 0x00486340
bool CKillObjectKey::WorkOn(void*)
{
    ReadDebugGlobal();
    if (Scripts::CStruct* structure = Scripts::ReadSubStruct(m_structure)) {
        uint32_t name = 0; // (the original's is its own argument slot)
        structure->ReadHashValue(kName, &name, 1);
        GameObjects::CEntity* object = GameObjects::FindObject(name);
        reinterpret_cast<void(__thiscall*)(GameObjects::CEntity*)>((*reinterpret_cast<void***>(object))[6])(object);
    }
    return true;
}

// 0x00486490
CCameraKey* CCameraKey::MakeNew(int frame, const Maths::QuatRotation* rotation, const Maths::Vec4* pos, bool cut)
{
    m_tick = frame;
    m_armed = false;
    m_vtable = kCameraVtable;
    m_orientationTurn = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Vec4 axis = {1.0f, 0.0f, 0.0f, 1.0f};
    Maths::QuatRotation quarterTurn;
    quarterTurn.InitFromAxisAngle(axis, 1.57079637f); // (0x3FC90FDB)
    Maths::QuatRotation turned = *rotation;
    turned *= quarterTurn;
    m_orientationTurn = turned;
    m_atPoint.x = pos->x;
    m_atPoint.y = pos->z;
    m_atPoint.z = -pos->y;
    m_atPoint.w = 1.0f;
    m_cut = cut;
    return this;
}

// 0x004861D0
bool CCameraKey::InsideRange(float start, float end, bool inclusive)
{
    double frame = m_tick;
    if (start < end) {
        if (m_cut ? end > frame : frame < static_cast<double>(end) - 1.0)
            return false;
        if (!(frame >= static_cast<double>(start) - 1.0))
            return false;
        return inclusive ? frame <= end : frame < end;
    }
    if (!(frame <= static_cast<double>(start) + 1.0))
        return false;
    return inclusive ? frame >= end : frame > end;
}

// 0x00486390
bool CCameraKey::WorkOn(void* object)
{
    if (!m_tick)
        return false;
    ReadDebugGlobal();
    auto* camera = static_cast<ViewCam*>(object);
    Maths::Vec4* pos = camera->FetchLocation();
    Maths::Mat4* matrix = camera->FetchMat();
    *pos = m_atPoint;
    float rotation[4] = {m_orientationTurn.x, m_orientationTurn.y, m_orientationTurn.z, m_orientationTurn.w};
    const float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    GameObjects::QuatToMatrix(rotation, origin, reinterpret_cast<float*>(matrix));
    m_armed = true;
    return true;
}

// 0x004865D0
void CAnimScriptKey::AssignValue(int frame, uint32_t event, Scripts::CStruct* params)
{
    m_tick = frame;
    m_armed = false;
    m_kindTag = event;
    Scripts::CStruct* memory = Scripts::NewStruct();
    mp_params = memory ? memory->PrepareAll() : nullptr;
    mp_params->AppendStructureIfAny(params);
}

// 0x00486560
void CAnimScriptKey::DestroyBody()
{
    m_vtable = kScriptKeyVtable;
    if (Scripts::CStruct* params = mp_params) {
        params->Clear();
        Scripts::DeleteStruct(params);
        mp_params = nullptr;
    }
    m_vtable = kBaseVtable;
}

// 0x00486440
bool CAnimScriptKey::WorkOn(void* object)
{
    static_cast<GameObjects::CEntity*>(object)->PostOwnEvent(m_kindTag, mp_params);
    return true;
}

// 0x00486680
CUserClipKey* __cdecl CreateCustomAnimKey(const uint8_t** cursor)
{
    auto* header = reinterpret_cast<const uint32_t*>(*cursor);
    int frame = static_cast<int>(header[0]);
    uint32_t kind = header[1];
    uint32_t size = header[2];
    const uint32_t* data = header + 3;
    *cursor = reinterpret_cast<const uint8_t*>(data);
    switch (kind) {
    case 1: {
        uint32_t fov = *data;
        *cursor = reinterpret_cast<const uint8_t*>(data + 1);
        auto* key = NewKey<CCameraFovKey>(kFovVtable, frame);
        if (!key)
            return nullptr;
        *reinterpret_cast<uint32_t*>(&key->m_viewAngle) = fov;
        return key;
    }
    case 2:
    case 7: {
        Maths::Vec4 pos = *reinterpret_cast<const Maths::Vec4*>(data);
        *cursor = reinterpret_cast<const uint8_t*>(data + 4);
        Maths::QuatRotation rotation = *reinterpret_cast<const Maths::QuatRotation*>(data + 4);
        *cursor = reinterpret_cast<const uint8_t*>(data + 8);
        auto* key = static_cast<CCameraKey*>(MemSys::Alloc(sizeof(CCameraKey)));
        if (!key)
            return nullptr;
        return key->MakeNew(frame, &rotation, &pos, kind == 7);
    }
    case 3: {
        *cursor = reinterpret_cast<const uint8_t*>(data + 8); // (32 bytes skipped)
        return NewKey<CUserClipKey>(kMarkerVtable, frame);
    }
    case 4:
    case 5:
    case 6: {
        uint32_t value = *data;
        *cursor = reinterpret_cast<const uint8_t*>(data + 1);
        void** vtables[] = {kScriptVtable, kStructureVtable, kKillVtable};
        auto* key = NewKey<CScriptKey>(vtables[kind - 4], frame);
        if (!key)
            return nullptr;
        key->m_runScript = value; // (each kind's one word)
        return key;
    }
    default:
        *cursor = reinterpret_cast<const uint8_t*>(data) + size;
        return nullptr;
    }
}

namespace {
static VtableSlot kFovVtable_slots[] = {&Gfx2D::CUserClipKey::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &Gfx2D::CCameraFovKey::WorkOn};
void** const kFovVtable = reinterpret_cast<void**>(kFovVtable_slots);
static VtableSlot kScriptVtable_slots[] = {&Gfx2D::CUserClipKey::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &Gfx2D::CScriptKey::WorkOn};
void** const kScriptVtable = reinterpret_cast<void**>(kScriptVtable_slots);
static VtableSlot kMarkerVtable_slots[] = {&Gfx2D::CUserClipKey::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &MemCard::SaveCard::AlwaysTrue};
void** const kMarkerVtable = reinterpret_cast<void**>(kMarkerVtable_slots);
static VtableSlot kStructureVtable_slots[] = {&Gfx2D::CUserClipKey::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &Gfx2D::CStructureScriptKey::WorkOn};
void** const kStructureVtable = reinterpret_cast<void**>(kStructureVtable_slots);
static VtableSlot kKillVtable_slots[] = {&Gfx2D::CUserClipKey::Teardown, &Gfx2D::CUserClipKey::InsideRange,
                &Gfx2D::CKillObjectKey::WorkOn};
void** const kKillVtable = reinterpret_cast<void**>(kKillVtable_slots);
static VtableSlot kCameraVtable_slots[] = {&Gfx2D::CUserClipKey::Teardown, &Gfx2D::CCameraKey::InsideRange,
                &Gfx2D::CCameraKey::WorkOn};
void** const kCameraVtable = reinterpret_cast<void**>(kCameraVtable_slots);
} // namespace

} // namespace Gfx
