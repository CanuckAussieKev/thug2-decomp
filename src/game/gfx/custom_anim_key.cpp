// An animation's custom keys (see custom_anim_key.h). From the disassembly of 0x004860C0-0x00486875.

#include "game/gfx/custom_anim_key.h"

#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/obj/cutscene.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

constexpr uint32_t kBaseVtable = 0x006487B0;
constexpr uint32_t kScriptKeyVtable = 0x006487BC;
constexpr uint32_t kFovVtable = 0x0064880C;
constexpr uint32_t kScriptVtable = 0x00648818;
constexpr uint32_t kMarkerVtable = 0x00648824; // (kind 3: no data; its Process is the shared "true" 0x005BC7F0)
constexpr uint32_t kStructureVtable = 0x00648830;
constexpr uint32_t kKillVtable = 0x0064883C;
constexpr uint32_t kCameraVtable = 0x00648848;

constexpr uint32_t kDebugGlobal = 0x2F510F45;     // (a script global each Process reads - and ignores)
constexpr uint32_t kSetHFOVRadians = 0x44EA2B6D;  // the camera's member function (name unknown)
constexpr uint32_t kStructureScript = 0xBDF7F843; // (name unknown) the script a structure key runs
constexpr uint32_t kName = 0xA1DC81F9;            // name

void** Vtable(uint32_t address) { return reinterpret_cast<void**>(address); }

// Each Process reads the global 0x2F510F45 first (compiled-out debug output, presumably).
void ReadDebugGlobal() { Script::GetInteger(kDebugGlobal); }

// A key of `size` bytes with its frame, inactive (from Mem::New; null without memory).
template <typename T>
T* NewKey(uint32_t vtable, int frame, uint32_t size = sizeof(T))
{
    auto* key = static_cast<T*>(Mem::New(size));
    if (!key)
        return nullptr;
    key->m_frame = frame;
    key->m_armed = false;
    key->m_vtable = Vtable(vtable);
    return key;
}

} // namespace

// 0x004860C0
bool CCustomAnimKey::WithinRange(float start, float end, bool inclusive)
{
    double frame = m_frame;
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
void CCustomAnimKey::SetActive(bool active) { m_armed = active; }

// 0x00486160
bool CCustomAnimKey::ProcessIfActive(void* object)
{
    if (!m_armed)
        return false;
    m_armed = false;
    return reinterpret_cast<bool(__thiscall*)(CCustomAnimKey*, void*)>(m_vtable[2])(this, object);
}

// 0x00486460
CCustomAnimKey* CCustomAnimKey::Destroy(unsigned int flags)
{
    GameFunction<void(__thiscall*)(CCustomAnimKey*)>(0x004819E0)(this); // (the base's destructor body: its vtable)
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004868A0
bool CCameraFovKey::Process(void* camera)
{
    ReadDebugGlobal();
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* params = memory ? memory->Init() : nullptr;
    params->AddFloat(0u, m_fov);
    Script::RunScript(kSetHFOVRadians, params, static_cast<Obj::CObject*>(camera), false, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    return true;
}

// 0x00486180
bool CScriptKey::Process(void* object)
{
    ReadDebugGlobal();
    if (m_script == 0x4E917BDA)
        m_script = 0x69F35FA0;
    else if (m_script == 0x208D7A0D)
        m_script = 0x04D887B1;
    Script::RunScript(m_script, nullptr, static_cast<Obj::CObject*>(object), false, nullptr);
    return true;
}

// 0x00486300
bool CStructureScriptKey::Process(void*)
{
    ReadDebugGlobal();
    if (Script::CStruct* structure = Script::GetStructure(m_structure))
        Script::RunScript(kStructureScript, structure, nullptr, false, nullptr);
    return true;
}

// 0x00486340
bool CKillObjectKey::Process(void*)
{
    ReadDebugGlobal();
    if (Script::CStruct* structure = Script::GetStructure(m_structure)) {
        uint32_t name = 0; // (the original's is its own argument slot)
        structure->GetChecksum(kName, &name, 1);
        Obj::CObject* object = Obj::FindObject(name);
        reinterpret_cast<void(__thiscall*)(Obj::CObject*)>((*reinterpret_cast<void***>(object))[6])(object);
    }
    return true;
}

// 0x00486490
CCameraKey* CCameraKey::Construct(int frame, const Mth::Quat* rotation, const Mth::Vector* pos, bool cut)
{
    m_frame = frame;
    m_armed = false;
    m_vtable = Vtable(kCameraVtable);
    m_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector axis = {1.0f, 0.0f, 0.0f, 1.0f};
    Mth::Quat quarterTurn;
    quarterTurn.InitFromAxisAngle(axis, 1.57079637f); // (0x3FC90FDB)
    Mth::Quat turned = *rotation;
    turned *= quarterTurn;
    m_rotation = turned;
    m_pos.x = pos->x;
    m_pos.y = pos->z;
    m_pos.z = -pos->y;
    m_pos.w = 1.0f;
    m_cut = cut;
    return this;
}

// 0x004861D0
bool CCameraKey::WithinRange(float start, float end, bool inclusive)
{
    double frame = m_frame;
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
bool CCameraKey::Process(void* object)
{
    if (!m_frame)
        return false;
    ReadDebugGlobal();
    auto* camera = static_cast<Camera*>(object);
    Mth::Vector* pos = camera->GetPos();
    Mth::Matrix* matrix = camera->GetMatrix();
    *pos = m_pos;
    float rotation[4] = {m_rotation.x, m_rotation.y, m_rotation.z, m_rotation.w};
    const float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Obj::QuatToMatrix(rotation, origin, reinterpret_cast<float*>(matrix));
    m_armed = true;
    return true;
}

// 0x004865D0
void CAnimScriptKey::Set(int frame, uint32_t event, Script::CStruct* params)
{
    m_frame = frame;
    m_armed = false;
    m_type = event;
    Script::CStruct* memory = Script::NewStruct();
    mp_params = memory ? memory->Init() : nullptr;
    mp_params->AppendStructureIfAny(params);
}

// 0x00486560
void CAnimScriptKey::DestroyBody()
{
    m_vtable = Vtable(kScriptKeyVtable);
    if (Script::CStruct* params = mp_params) {
        params->Clear();
        Script::DeleteStruct(params);
        mp_params = nullptr;
    }
    m_vtable = Vtable(kBaseVtable);
}

// 0x00486440
bool CAnimScriptKey::Process(void* object)
{
    static_cast<Obj::CObject*>(object)->SelfEvent(m_type, mp_params);
    return true;
}

// 0x00486680
CCustomAnimKey* __cdecl CreateCustomAnimKey(const uint8_t** cursor)
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
        *reinterpret_cast<uint32_t*>(&key->m_fov) = fov;
        return key;
    }
    case 2:
    case 7: {
        Mth::Vector pos = *reinterpret_cast<const Mth::Vector*>(data);
        *cursor = reinterpret_cast<const uint8_t*>(data + 4);
        Mth::Quat rotation = *reinterpret_cast<const Mth::Quat*>(data + 4);
        *cursor = reinterpret_cast<const uint8_t*>(data + 8);
        auto* key = static_cast<CCameraKey*>(Mem::New(sizeof(CCameraKey)));
        if (!key)
            return nullptr;
        return key->Construct(frame, &rotation, &pos, kind == 7);
    }
    case 3: {
        *cursor = reinterpret_cast<const uint8_t*>(data + 8); // (32 bytes skipped)
        return NewKey<CCustomAnimKey>(kMarkerVtable, frame);
    }
    case 4:
    case 5:
    case 6: {
        uint32_t value = *data;
        *cursor = reinterpret_cast<const uint8_t*>(data + 1);
        uint32_t vtables[] = {kScriptVtable, kStructureVtable, kKillVtable};
        auto* key = NewKey<CScriptKey>(vtables[kind - 4], frame);
        if (!key)
            return nullptr;
        key->m_script = value; // (each kind's one word)
        return key;
    }
    default:
        *cursor = reinterpret_cast<const uint8_t*>(data) + size;
        return nullptr;
    }
}

} // namespace Gfx

REPLACE(0x004860C0, &Gfx::CCustomAnimKey::WithinRange);
REPLACE(0x00486150, &Gfx::CCustomAnimKey::SetActive);
REPLACE(0x00486160, &Gfx::CCustomAnimKey::ProcessIfActive);
REPLACE(0x00486460, &Gfx::CCustomAnimKey::Destroy);
REPLACE(0x004868A0, &Gfx::CCameraFovKey::Process);
REPLACE(0x00486180, &Gfx::CScriptKey::Process);
REPLACE(0x00486300, &Gfx::CStructureScriptKey::Process);
REPLACE(0x00486340, &Gfx::CKillObjectKey::Process);
REPLACE(0x00486490, &Gfx::CCameraKey::Construct);
REPLACE(0x004861D0, &Gfx::CCameraKey::WithinRange);
REPLACE(0x00486390, &Gfx::CCameraKey::Process);
REPLACE(0x004865D0, &Gfx::CAnimScriptKey::Set);
REPLACE(0x00486560, &Gfx::CAnimScriptKey::DestroyBody);
REPLACE(0x00486440, &Gfx::CAnimScriptKey::Process);
REPLACE(0x00486680, Gfx::CreateCustomAnimKey);
