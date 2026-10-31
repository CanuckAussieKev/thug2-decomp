// The model appearance (Gfx2D::CModelLook): see model_appearance.h. From the disassembly of
// 0x00488DB0-0x004897BE (and 0x004890E0 / 0x004891F0, which Ghidra didn't make functions).

#include "engine/gfx/model_appearance.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/face_texture.h"
#include "engine/script/cscript.h"
#include "game/skate/cas_options.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity.h"

namespace Gfx2D {

namespace {

extern void** const kVtable;

constexpr uint32_t kAppearanceInitStructure = 0xFE54486D; // appearance_init_structure
constexpr uint32_t kRandomizedDescId = 0xD81C03B0;        // randomized_desc_id
constexpr uint32_t kShuffleIndex = 0x4B833E64;             // random_index
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

// The CAS part lists (not rewritten yet, cdecl): the option `descriptorId` of the slot `part`'s list; a random option of
// it (made with `randomSet`); the option at `index`.
Scripts::CStruct* CustomiseChoice(uint32_t part, uint32_t descriptorId, bool assert)
{
    return GameObjects::FindCasOption(part, descriptorId, assert);
}
Scripts::CStruct* RandomCasOption(uint32_t part, uint32_t randomSet)
{
    return GameObjects::PickRandomCasOption(part, randomSet);
}
Scripts::CStruct* CasOptionAt(uint32_t part, int index, uint32_t randomSet)
{
    return GameObjects::GetCasOptionByIndex(part, index, randomSet);
}

// A face texture constructed (0x004875C0, returns this).
// Whether a face texture holds one (0x00487510) / set so (0x00487520) - called through the game (the tests script them).
bool FaceTextureIsValid(CFaceSkin* face) { return face->IsSound(); }
void SetFaceTextureValid(CFaceSkin* face, bool valid)
{
    face->MarkUsable(valid);
}

CFaceSkin* ConstructFaceTexture(void* memory)
{
    return static_cast<CFaceSkin*>(memory)->MakeNew();
}

// A temporary structure (from the structure pool; null if there's no memory) / cleared and given back.
Scripts::CStruct* NewTempStruct()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteTempStruct(Scripts::CStruct* s)
{
    if (s) {
        s->Clear();
        Scripts::DeleteStruct(s);
    }
}

} // namespace

// 0x00488EA0
CModelLook* CModelLook::MakeNew()
{
    CEntity::MakeNew();
    m_vtable = kVtable;
    m_structure.PrepareAll();
    m_faceSkin = nullptr;
    m_hasFaceTexture = false;
    return this;
}

// 0x004890F0
void CModelLook::DestroyBody()
{
    m_vtable = kVtable;
    if (m_faceSkin) {
        MemSys::Hub::Pull()->Dealloc(m_faceSkin);
        m_faceSkin = nullptr;
    }
    m_structure.Clear();
    CEntity::DestroyBody();
}

// 0x00489370
CModelLook* CModelLook::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00488F00
bool CModelLook::Reset()
{
    m_structure.Clear();
    if (m_faceSkin)
        SetFaceTextureValid(m_faceSkin, false);
    if (Scripts::CStruct* defaults = Scripts::ReadSubStruct(kAppearanceInitStructure))
        m_structure.AppendStructureIfAny(defaults);
    m_hasFaceTexture = false;
    return true;
}

// 0x00488F40
bool CModelLook::ReadIn(Scripts::CStruct* appearance, bool resolve)
{
    m_structure.Clear();
    if (m_faceSkin)
        SetFaceTextureValid(m_faceSkin, false);
    if (Scripts::CStruct* defaults = Scripts::ReadSubStruct(kAppearanceInitStructure))
        m_structure.AppendStructureIfAny(defaults);
    m_hasFaceTexture = false;
    appearance->InlineInto(&m_structure, 0);
    if (resolve)
        ResolveRandomParts();
    return true;
}

// 0x00488FA0 (the global's lookup asserts: Scripts::ReadSubStruct(name, 1) - the assert compiled out)
bool CModelLook::LoadByName(uint32_t name, bool resolve) { return ReadIn(Scripts::ReadSubStruct(name), resolve); }

// 0x00488DB0
void CModelLook::ResolveRandomParts()
{
    uint32_t randomSet = 0; // (the last choice's random_set, carried on to the next)
    Scripts::CComponent* next;
    for (Scripts::CComponent* component = m_structure.StepToEntry(nullptr); component; component = next) {
        next = m_structure.StepToEntry(component);
        if (component->KindTag() != Scripts::QBVAL_STRUCT)
            continue;
        uint32_t part = component->name;
        Scripts::CStruct* option;
        if (component->structure->HasBareFlag(kRandomizedDescId)) {
            option = RandomCasOption(part, randomSet);
        } else {
            int index;
            if (!component->structure->FetchIntValue(kShuffleIndex, &index, 0))
                continue;
            option = CasOptionAt(part, index, randomSet);
        }
        option->ReadHashValue(kRandomSet, &randomSet, 0);
        component->structure->Clear();
        uint32_t descriptorId = 0;
        option->ReadHashValue(kDescId, &descriptorId, 0);
        component->structure->PutNameKey(kDescId, descriptorId);
    }
}

// 0x00488FD0
void RemoveTattoos(Scripts::CStruct* appearance)
{
    for (uint32_t tattoo : kTattoos)
        appearance->EraseModule(tattoo);
}

// 0x00489050
Scripts::CStruct* CModelLook::GetPartOption(uint32_t part)
{
    Scripts::CStruct* slot;
    if (!m_structure.ReadSubStruct(part, &slot, 0))
        return nullptr;
    uint32_t descriptorId = 0;
    slot->ReadHashValue(kDescId, &descriptorId, 0);
    return CustomiseChoice(part, descriptorId, false);
}

// 0x004890B0
Scripts::CStruct* CModelLook::GetPart(uint32_t part)
{
    Scripts::CStruct* slot = nullptr;
    m_structure.ReadSubStruct(part, &slot, 0);
    return slot;
}

// 0x004890E0
bool CModelLook::HasFaceTexture() { return m_hasFaceTexture; }

// 0x004891F0
void CModelLook::SetPart(uint32_t part, uint32_t descriptorId, Scripts::CStruct* params)
{
    Scripts::CStruct* slot;
    if (!m_structure.ReadSubStruct(part, &slot, 0)) {
        slot = NewTempStruct();
        m_structure.AddComponentOfType(part, kOwnedStructure, reinterpret_cast<uint32_t>(slot));
    }
    slot->Clear();
    int useDefaultHsv = 1;
    params->FetchIntValue(kUseDefaultHsv, &useDefaultHsv, 0);
    if (!useDefaultHsv) {
        // (asserted; missing, h is what the original's stack slot held - `params` itself - and s / v garbage)
        int h = static_cast<int>(reinterpret_cast<uintptr_t>(params)), s = 0, v = 0;
        params->FetchIntValue(kH, &h, 1);
        params->FetchIntValue(kS, &s, 1);
        params->FetchIntValue(kV, &v, 1);
        slot->PutInt(kUseDefaultHsv, 0);
        slot->PutInt(kH, h);
        slot->PutInt(kS, s);
        slot->PutInt(kV, v);
    }
    slot->PutNameKey(kDescId, descriptorId);
}

// 0x004894D0. The names are asserted; a missing one is what the original's stack slot held (the `params` or
// `script` argument's).
bool CModelLook::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    auto slotOf = [](const void* argument) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument)); };
    switch (name) {
    case kSetPart: {
        uint32_t part = slotOf(script), descriptorId = slotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        params->ReadHashValue(kDescId, &descriptorId, 1);
        SetPart(part, descriptorId, params);
        return true;
    }
    case kGetPart: {
        uint32_t part = slotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        Scripts::CStruct* slot = GetPart(part);
        if (!slot)
            return false;
        script->m_args->AppendStructureIfAny(slot);
        return true;
    }
    case kClearPart: {
        uint32_t part = slotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        m_structure.EraseModule(part);
        return true;
    }
    case kGotPart: {
        uint32_t part = slotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        return GetPartOption(part) != nullptr;
    }
    case kPartGotFlag: {
        uint32_t part = slotOf(params), flag = slotOf(script);
        params->ReadHashValue(kPart, &part, 1);
        params->ReadHashValue(kFlag, &flag, 1);
        Scripts::CStruct* option = GetPartOption(part);
        if (!option)
            return false;
        return option->HasBareFlag(flag);
    }
    case kSetChecksum: {
        uint32_t field = slotOf(script), value = slotOf(params);
        params->ReadHashValue(kField, &field, 1);
        params->ReadHashValue(kValue, &value, 1);
        m_structure.PutNameKey(field, value);
        return true;
    }
    default:
        return CEntity::InvokeMethodByName(name, params, script);
    }
}

// 0x00489160
void CModelLook::MakeFaceTexture()
{
    void* memory = MemSys::AllocZeroed(sizeof(CFaceSkin));
    m_faceSkin = memory ? ConstructFaceTexture(memory) : nullptr;
}

// 0x004891D0
void CModelLook::FreeFaceTexture()
{
    if (m_faceSkin) {
        MemSys::Hub::Pull()->Dealloc(m_faceSkin);
        m_faceSkin = nullptr;
    }
}

// 0x004893A0
CModelLook* CModelLook::CopyFrom(CModelLook* other)
{
    if (other != this) {
        m_structure.Assign(&other->m_structure);
        FreeFaceTexture();
        if (other->m_faceSkin) {
            MakeFaceTexture();
            std::memcpy(m_faceSkin, other->m_faceSkin, sizeof(CFaceSkin));
        }
        m_hasFaceTexture = other->m_hasFaceTexture;
    }
    return this;
}

// 0x004896A0
int CModelLook::EmitIntoBuffer(uint8_t* buffer, int size, bool noFaceTexture)
{
    Scripts::CStruct* copy = NewTempStruct();
    copy->AppendStructureIfAny(&m_structure); // (no memory: a crash, as the original)
    auto bytes = Scripts::EmitIntoBuffer(copy, buffer, static_cast<uint32_t>(size), 1, nullptr);
    if (bytes > static_cast<uint32_t>(kMaxBufferBytes)) {
        RemoveTattoos(copy);
        bytes = Scripts::EmitIntoBuffer(copy, buffer, static_cast<uint32_t>(size), 1, nullptr);
    }
    DeleteTempStruct(copy);
    uint8_t* end = buffer + bytes;
    if (noFaceTexture)
        *end = 0;
    else
        *end = (m_faceSkin && FaceTextureIsValid(m_faceSkin)) || m_hasFaceTexture;
    return static_cast<int>(bytes) + 1;
}

// 0x00489400
uint8_t* CModelLook::DecodeFromBytes(uint8_t* buffer)
{
    Scripts::CStruct* read = NewTempStruct();
    uint8_t* end = const_cast<uint8_t*>(Scripts::DecodeFromBytes(read, buffer));
    m_structure.Clear();
    m_structure.AppendStructureIfAny(read);
    DeleteTempStruct(read);
    m_hasFaceTexture = *end != 0;
    return end + 1;
}

namespace {
static VtableSlot kVtable_slots[] = {&Gfx2D::CModelLook::Teardown, &GameObjects::CEntity::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &Gfx2D::CModelLook::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
} // namespace

} // namespace Gfx
