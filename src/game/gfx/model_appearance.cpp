// The model appearance (Gfx::CModelAppearance): see model_appearance.h. From the disassembly of
// 0x00488DB0-0x004897BE (and 0x004890E0 / 0x004891F0, which Ghidra didn't make functions).

#include "game/gfx/model_appearance.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x006488FC);

constexpr uint32_t kAppearanceInitStructure = 0xFE54486D; // appearance_init_structure
constexpr uint32_t kRandomizedDescId = 0xD81C03B0;        // randomized_desc_id
constexpr uint32_t kRandomIndex = 0x4B833E64;             // random_index
constexpr uint32_t kRandomSet = 0x0D7260FD;               // random_set
constexpr uint32_t kDescId = 0x4BB2084E;                  // desc_id
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;           // use_default_hsv
constexpr uint32_t kH = 0x6E94F918;                       // h
constexpr uint32_t kS = 0xE4F130F4;                       // s
constexpr uint32_t kV = 0x949BC47B;                       // v
constexpr uint32_t kPart = 0xB6F08F39;                    // part
constexpr uint32_t kFlag = 0x2E0B1465;                    // flag
constexpr uint32_t kField = 0xA40ABAA7;                   // field
constexpr uint32_t kValue = 0xE288A7CB;                   // value

// The member functions.
constexpr uint32_t kSetPart = 0x83339D0B;     // SetPart
constexpr uint32_t kGetPart = 0x10A225D6;     // GetPart
constexpr uint32_t kClearPart = 0x0A23400C;   // ClearPart
constexpr uint32_t kGotPart = 0xB13906B0;     // GotPart
constexpr uint32_t kPartGotFlag = 0xE961BAFA; // PartGotFlag
constexpr uint32_t kSetChecksum = 0xD27427FF; // SetChecksum

constexpr uint32_t kOwnedStructure = 11; // AddComponentOfType's type: a structure it takes over, by pointer
constexpr int kMaxBufferBytes = 0x2BC; // (more: the tattoos are left out)

const uint32_t kTattoos[] = {
    0x3FA9B96E, // head_tattoo
    0x283DEA37, // left_bicep_tattoo
    0xDE55864B, // left_forearm_tattoo
    0xD408FA96, // right_bicep_tattoo
    0x74FEE7B2, // right_forearm_tattoo
    0x8EBA2BC9, // chest_tattoo
    0x233B7BBA, // back_tattoo
    0x2B359A94, // left_leg_tattoo
    0x609432CA, // right_leg_tattoo
};

// The CAS part lists (not rewritten yet, cdecl): the option `descId` of the slot `part`'s list; a random option of
// it (made with `randomSet`); the option at `index`.
Script::CStruct* CasOption(uint32_t part, uint32_t descId, bool assert)
{
    return GameFunction<Script::CStruct*(__cdecl*)(uint32_t, uint32_t, bool)>(0x00485CF0)(part, descId, assert);
}
Script::CStruct* RandomCasOption(uint32_t part, uint32_t randomSet)
{
    return GameFunction<Script::CStruct*(__cdecl*)(uint32_t, uint32_t)>(0x00485E20)(part, randomSet);
}
Script::CStruct* CasOptionAt(uint32_t part, int index, uint32_t randomSet)
{
    return GameFunction<Script::CStruct*(__cdecl*)(uint32_t, int, uint32_t)>(0x00485FC0)(part, index, randomSet);
}

// A face texture constructed (0x004875C0, returns this).
// Whether a face texture holds one (0x00487510) / set so (0x00487520) - called through the game (the tests script them).
bool FaceTextureIsValid(CFaceTexture* face) { return GameFunction<bool(__thiscall*)(CFaceTexture*)>(0x00487510)(face); }
void SetFaceTextureValid(CFaceTexture* face, bool valid)
{
    GameFunction<void(__thiscall*)(CFaceTexture*, bool)>(0x00487520)(face, valid);
}

CFaceTexture* ConstructFaceTexture(void* memory)
{
    return GameFunction<CFaceTexture*(__thiscall*)(void*)>(0x004875C0)(memory);
}

// A temporary structure (from the structure pool; null if there's no memory) / cleared and given back.
Script::CStruct* NewTempStruct()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteTempStruct(Script::CStruct* s)
{
    if (s) {
        s->Clear();
        Script::DeleteStruct(s);
    }
}

} // namespace


// 0x00488EA0
CModelAppearance* CModelAppearance::Construct()
{
    CObject::Construct();
    m_vtable = kVtable;
    m_structure.Init();
    mp_faceTexture = nullptr;
    m_hasFaceTexture = false;
    return this;
}

// 0x004890F0
void CModelAppearance::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_faceTexture) {
        Mem::Manager::Get()->Free(mp_faceTexture);
        mp_faceTexture = nullptr;
    }
    m_structure.Clear();
    CObject::DestroyBody();
}

// 0x00489370
CModelAppearance* CModelAppearance::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00488F00
bool CModelAppearance::Reset()
{
    m_structure.Clear();
    if (mp_faceTexture)
        SetFaceTextureValid(mp_faceTexture, false);
    if (Script::CStruct* defaults = Script::GetStructure(kAppearanceInitStructure))
        m_structure.AppendStructureIfAny(defaults);
    m_hasFaceTexture = false;
    return true;
}

// 0x00488F40
bool CModelAppearance::Load(Script::CStruct* appearance, bool resolve)
{
    m_structure.Clear();
    if (mp_faceTexture)
        SetFaceTextureValid(mp_faceTexture, false);
    if (Script::CStruct* defaults = Script::GetStructure(kAppearanceInitStructure))
        m_structure.AppendStructureIfAny(defaults);
    m_hasFaceTexture = false;
    appearance->ExpandInto(&m_structure, 0);
    if (resolve)
        ResolveRandomParts();
    return true;
}

// 0x00488FA0 (the global's lookup asserts: Script::GetStructure(name, 1) - the assert compiled out)
bool CModelAppearance::LoadByName(uint32_t name, bool resolve) { return Load(Script::GetStructure(name), resolve); }

// 0x00488DB0
void CModelAppearance::ResolveRandomParts()
{
    uint32_t randomSet = 0; // (the last choice's random_set, carried on to the next)
    Script::CComponent* next;
    for (Script::CComponent* component = m_structure.GetNextComponent(nullptr); component; component = next) {
        next = m_structure.GetNextComponent(component);
        if (component->Type() != Script::ESYMBOLTYPE_STRUCTURE)
            continue;
        uint32_t part = component->name;
        Script::CStruct* option;
        if (component->structure->ContainsFlag(kRandomizedDescId)) {
            option = RandomCasOption(part, randomSet);
        } else {
            int index;
            if (!component->structure->GetInteger(kRandomIndex, &index, 0))
                continue;
            option = CasOptionAt(part, index, randomSet);
        }
        option->GetChecksum(kRandomSet, &randomSet, 0);
        component->structure->Clear();
        uint32_t descId = 0;
        option->GetChecksum(kDescId, &descId, 0);
        component->structure->AddChecksum(kDescId, descId);
    }
}

// 0x00488FD0
void RemoveTattoos(Script::CStruct* appearance)
{
    for (uint32_t tattoo : kTattoos)
        appearance->RemoveComponent(tattoo);
}

// 0x00489050
Script::CStruct* CModelAppearance::GetPartOption(uint32_t part)
{
    Script::CStruct* slot;
    if (!m_structure.GetStructure(part, &slot, 0))
        return nullptr;
    uint32_t descId = 0;
    slot->GetChecksum(kDescId, &descId, 0);
    return CasOption(part, descId, false);
}

// 0x004890B0
Script::CStruct* CModelAppearance::GetPart(uint32_t part)
{
    Script::CStruct* slot = nullptr;
    m_structure.GetStructure(part, &slot, 0);
    return slot;
}

// 0x004890E0
bool CModelAppearance::HasFaceTexture() { return m_hasFaceTexture; }

// 0x004891F0
void CModelAppearance::SetPart(uint32_t part, uint32_t descId, Script::CStruct* params)
{
    Script::CStruct* slot;
    if (!m_structure.GetStructure(part, &slot, 0)) {
        slot = NewTempStruct();
        m_structure.AddComponentOfType(part, kOwnedStructure, reinterpret_cast<uint32_t>(slot));
    }
    slot->Clear();
    int useDefaultHsv = 1;
    params->GetInteger(kUseDefaultHsv, &useDefaultHsv, 0);
    if (!useDefaultHsv) {
        // (asserted; missing, h is what the original's stack slot held - `params` itself - and s / v garbage)
        int h = static_cast<int>(reinterpret_cast<uintptr_t>(params)), s = 0, v = 0;
        params->GetInteger(kH, &h, 1);
        params->GetInteger(kS, &s, 1);
        params->GetInteger(kV, &v, 1);
        slot->AddInteger(kUseDefaultHsv, 0);
        slot->AddInteger(kH, h);
        slot->AddInteger(kS, s);
        slot->AddInteger(kV, v);
    }
    slot->AddChecksum(kDescId, descId);
}

// 0x004894D0. The names are asserted; a missing one is what the original's stack slot held (the `params` or
// `script` argument's).
bool CModelAppearance::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    auto slotOf = [](const void* argument) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument)); };
    switch (name) {
    case kSetPart: {
        uint32_t part = slotOf(script), descId = slotOf(params);
        params->GetChecksum(kPart, &part, 1);
        params->GetChecksum(kDescId, &descId, 1);
        SetPart(part, descId, params);
        return true;
    }
    case kGetPart: {
        uint32_t part = slotOf(params);
        params->GetChecksum(kPart, &part, 1);
        Script::CStruct* slot = GetPart(part);
        if (!slot)
            return false;
        script->m_params->AppendStructureIfAny(slot);
        return true;
    }
    case kClearPart: {
        uint32_t part = slotOf(params);
        params->GetChecksum(kPart, &part, 1);
        m_structure.RemoveComponent(part);
        return true;
    }
    case kGotPart: {
        uint32_t part = slotOf(params);
        params->GetChecksum(kPart, &part, 1);
        return GetPartOption(part) != nullptr;
    }
    case kPartGotFlag: {
        uint32_t part = slotOf(params), flag = slotOf(script);
        params->GetChecksum(kPart, &part, 1);
        params->GetChecksum(kFlag, &flag, 1);
        Script::CStruct* option = GetPartOption(part);
        if (!option)
            return false;
        return option->ContainsFlag(flag);
    }
    case kSetChecksum: {
        uint32_t field = slotOf(script), value = slotOf(params);
        params->GetChecksum(kField, &field, 1);
        params->GetChecksum(kValue, &value, 1);
        m_structure.AddChecksum(field, value);
        return true;
    }
    default:
        return CObject::CallMemberFunction(name, params, script);
    }
}

// 0x00489160
void CModelAppearance::MakeFaceTexture()
{
    void* memory = Mem::Calloc(sizeof(CFaceTexture));
    mp_faceTexture = memory ? ConstructFaceTexture(memory) : nullptr;
}

// 0x004891D0
void CModelAppearance::FreeFaceTexture()
{
    if (mp_faceTexture) {
        Mem::Manager::Get()->Free(mp_faceTexture);
        mp_faceTexture = nullptr;
    }
}

// 0x004893A0
CModelAppearance* CModelAppearance::CopyFrom(CModelAppearance* other)
{
    if (other != this) {
        m_structure.Assign(&other->m_structure);
        FreeFaceTexture();
        if (other->mp_faceTexture) {
            MakeFaceTexture();
            std::memcpy(mp_faceTexture, other->mp_faceTexture, sizeof(CFaceTexture));
        }
        m_hasFaceTexture = other->m_hasFaceTexture;
    }
    return this;
}

// 0x004896A0
int CModelAppearance::WriteToBuffer(uint8_t* buffer, int size, bool noFaceTexture)
{
    Script::CStruct* copy = NewTempStruct();
    copy->AppendStructureIfAny(&m_structure); // (no memory: a crash, as the original)
    auto bytes = Script::WriteToBuffer(copy, buffer, static_cast<uint32_t>(size), 1, nullptr);
    if (bytes > static_cast<uint32_t>(kMaxBufferBytes)) {
        RemoveTattoos(copy);
        bytes = Script::WriteToBuffer(copy, buffer, static_cast<uint32_t>(size), 1, nullptr);
    }
    DeleteTempStruct(copy);
    uint8_t* end = buffer + bytes;
    if (noFaceTexture)
        *end = 0;
    else
        *end = (mp_faceTexture && FaceTextureIsValid(mp_faceTexture)) || m_hasFaceTexture;
    return static_cast<int>(bytes) + 1;
}

// 0x00489400
uint8_t* CModelAppearance::ReadFromBuffer(uint8_t* buffer)
{
    Script::CStruct* read = NewTempStruct();
    uint8_t* end = const_cast<uint8_t*>(Script::ReadFromBuffer(read, buffer));
    m_structure.Clear();
    m_structure.AppendStructureIfAny(read);
    DeleteTempStruct(read);
    m_hasFaceTexture = *end != 0;
    return end + 1;
}

} // namespace Gfx

REPLACE(0x00488DB0, &Gfx::CModelAppearance::ResolveRandomParts);
REPLACE(0x00488EA0, &Gfx::CModelAppearance::Construct);
REPLACE(0x00488F00, &Gfx::CModelAppearance::Reset);
REPLACE(0x00488F40, &Gfx::CModelAppearance::Load);
REPLACE(0x00488FA0, &Gfx::CModelAppearance::LoadByName);
REPLACE(0x00488FD0, Gfx::RemoveTattoos);
REPLACE(0x00489050, &Gfx::CModelAppearance::GetPartOption);
REPLACE(0x004890B0, &Gfx::CModelAppearance::GetPart);
REPLACE_SOURCE_ONLY(0x004890E0, &Gfx::CModelAppearance::HasFaceTexture); // 4 bytes
REPLACE(0x004890F0, &Gfx::CModelAppearance::DestroyBody);
REPLACE(0x00489160, &Gfx::CModelAppearance::MakeFaceTexture);
REPLACE(0x004891D0, &Gfx::CModelAppearance::FreeFaceTexture);
REPLACE(0x004891F0, &Gfx::CModelAppearance::SetPart);
REPLACE(0x00489370, &Gfx::CModelAppearance::Destroy);
REPLACE(0x004893A0, &Gfx::CModelAppearance::CopyFrom);
REPLACE(0x00489400, &Gfx::CModelAppearance::ReadFromBuffer);
REPLACE(0x004894D0, &Gfx::CModelAppearance::CallMemberFunction);
REPLACE(0x004896A0, &Gfx::CModelAppearance::WriteToBuffer);
