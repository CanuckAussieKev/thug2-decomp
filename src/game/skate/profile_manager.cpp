// The player profile manager, 0x0057B620-0x0057C165. See profile_manager.h.

#include "game/skate/profile_manager.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/string.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/model_appearance.h"
#include "game/lst/lookup_table.h"

namespace GameObjects {

GameObjects::CProfileRoster* g_profileManager;
uint32_t g_profileManagerReferences;

namespace {

// Names.
constexpr uint32_t kInitProSkaters = 0x28FD0F0A; // init_pro_skaters (a script)
constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kCurrentSkater = 0x75AC23EF;  // CurrentSkater
constexpr uint32_t kDisplayName = 0x38E23359;    // DisplayName
constexpr uint32_t kCustom = 0x0A7BE964;         // custom
constexpr uint32_t kVersionNumber = 0x1902C8F0;  // version_number
constexpr uint32_t kAppearance = 0x554C7D6F;     // appearance
constexpr uint32_t kFaceTexture = 0xF0D3BC94;    // FaceTexture
constexpr uint32_t kInfo = 0x3476CEA8;           // info
constexpr uint32_t kDeckFlags = 0x95388A60;      // DeckFlags
constexpr uint32_t kMaxSpecials = 0xAC9B9EDA;    // max_specials
// The stats a custom skater keeps over a load (unless the save's are kept), in the order the original reads them.
constexpr uint32_t kStats[] = {
    0x439F4704, // Air
    0xAF895B3F, // Run
    0x9B65D7B8, // Ollie
    0xF0D90109, // speed
    0xEDF5DB70, // spin
    0x6DCB497C, // flip_speed
    0x9016B4E7, // switch
    0xF73A13E3, // rail_balance
    0xAE798769, // lip_balance
    0xB1FC0722, // manual_balance
};

MemSys::Heap* ProfileHeap() { return MemSys::Manager::Get()->m_skaterInfoHeap; }

CProfileRoster*& Instance() { return g_profileManager; }
uint32_t& RefTally() { return g_profileManagerReferences; }

// A new profile (constructed: its defaults).
CRiderIdentity* NewProfile()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderIdentity));
    return memory ? static_cast<CRiderIdentity*>(memory)->Construct() : nullptr;
}

// The face texture object of a model appearance (its +0x54; 0x00576E60, folded with GameModules::Viewer's getter), told a
// face texture (0x004875F0) and whether there is one (0x00487520).
void* FaceOf(CRiderIdentity* profile) { return *reinterpret_cast<void**>(profile->m_lookData + 0x54); }

} // namespace

void CRiderIdentity::SetAppearance(Scripts::CStruct* appearance, bool update)
{
    reinterpret_cast<Gfx2D::CModelLook*>(this)->Load(appearance, update);
}
// 0x00543010
void CRiderIdentity::DestroyBody()
{
    InfoStructure()->Clear();
    reinterpret_cast<Gfx2D::CModelLook*>(this)->DestroyBody();
}
void CRiderIdentity::Delete()
{
    InfoStructure()->Clear();
    reinterpret_cast<Gfx2D::CModelLook*>(this)->DestroyBody();
    MemSys::Manager::Get()->Free(this);
}

// The lookup table's generic code.
CRiderIdentity* ProfileTable::GetByIndex(int index, uint32_t* key)
{
    return static_cast<CRiderIdentity*>(reinterpret_cast<ListKit::LookupTable*>(this)->GetItemByIndex(&index, key));
}
CRiderIdentity* ProfileTable::Get(uint32_t key)
{
    return static_cast<CRiderIdentity*>(reinterpret_cast<ListKit::LookupTable*>(this)->GetItem(&key));
}
bool ProfileTable::Put(uint32_t key, CRiderIdentity* item)
{
    return reinterpret_cast<ListKit::LookupTable*>(this)->StoreEntry(&key, item);
}
void ProfileTable::Remove(uint32_t key) { reinterpret_cast<ListKit::LookupTable*>(this)->EraseEntry(&key); }

// 0x0057B9E0
void ProfileTable::Clear()
{
    for (Node* node = m_first; node;) {
        Node* next = node->next;
        MemSys::Manager::Get()->Free(node);
        node = next;
    }
    m_first = nullptr;
    m_cursor = nullptr;
    m_count = 0;
}

// 0x0057BBF0
void ProfileTable::Destroy() { Clear(); }

// 0x0057BC00
CProfileRoster* CProfileRoster::Construct()
{
    m_templates.m_count = 0;
    m_templates.m_first = nullptr;
    m_templates.m_cursor = nullptr;
    m_cursorPos = 0;
    m_profiles[0] = nullptr;
    m_profiles[1] = nullptr;
    m_locked = false;
    m_extraCount = 0;
    return this;
}

// 0x0057BC20
void CProfileRoster::DestroyBody()
{
    // Every template (the first each time: dropped from the table as it goes; a null one dropped too).
    for (uint32_t left = m_templates.m_count; left != 0; --left) {
        uint32_t key;
        if (CRiderIdentity* profile = m_templates.GetByIndex(0, &key))
            profile->Delete();
        m_templates.Remove(key);
    }
    // The second player's copy (the first player's is a template), and the extras.
    if (CRiderIdentity* copy = m_profiles[1])
        copy->Delete();
    m_profiles[1] = nullptr;
    for (int i = 0; i < m_extraCount; ++i) {
        if (CRiderIdentity* extra = m_extraProfiles[i])
            extra->Delete();
        m_extraProfiles[i] = nullptr;
    }
    m_templates.Clear();
}

// 0x0057B620 / 0x0057B630 / 0x0057B640
void CProfileRoster::SetCurrentIndex(int index) { m_cursorPos = index; }
CRiderIdentity* CProfileRoster::GetCurrentProfile() { return m_profiles[m_cursorPos]; }
CRiderIdentity* CProfileRoster::GetProfile(int index) { return m_profiles[index]; }

// 0x0057B650
CRiderIdentity* CProfileRoster::GetExtraProfile(uint32_t name)
{
    for (int i = 0; i < m_extraCount; ++i) {
        if (m_extraNames[i] == name)
            return m_extraProfiles[i];
    }
    return nullptr;
}

// 0x0057B680
void CProfileRoster::SetLocked(bool locked) { m_locked = locked; }

// 0x0057B690
void CProfileRoster::Init()
{
    MemSys::Manager::Get()->EnterHeapScope(ProfileHeap());
    Scripts::LaunchScript(kInitProSkaters, nullptr, nullptr, false, nullptr);
    uint32_t key;
    m_profiles[0] = m_templates.GetByIndex(0, &key);
    for (int i = 1; i < 2; ++i) {
        m_profiles[i] = NewProfile();
        m_profiles[i]->CopyFrom(m_templates.GetByIndex(i, &key));
    }
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x0057B770
uint32_t CProfileRoster::GetTemplateName(int index)
{
    uint32_t key;
    m_templates.GetByIndex(index, &key);
    return key;
}

// 0x0057B790
void CProfileRoster::ResetSecondProfile()
{
    MemSys::Manager::Get()->EnterHeapScope(ProfileHeap());
    CRiderIdentity* copy = m_profiles[1];
    copy->CopyFrom(m_templates.Get(copy->GetName()));
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x0057B7E0
CRiderIdentity* CProfileRoster::GetTemplate(uint32_t name) { return m_templates.Get(name); }

// 0x0057B800
void CProfileRoster::ResetTemplates()
{
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        m_templates.GetByIndex(static_cast<int>(i), &key)->Reset(nullptr);
    }
}

// 0x0057B840
void CProfileRoster::WritePros(Scripts::CStruct* save)
{
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        CRiderIdentity* profile = m_templates.GetByIndex(static_cast<int>(i), &key);
        if (profile->IsPro())
            profile->WriteToSave(save);
    }
    save->AddComponentOfType(kCurrentSkater, Scripts::QBVAL_NAME, m_profiles[m_cursorPos]->GetName());
    TextUtil::String displayName(m_profiles[m_cursorPos]->GetDisplayName());
    save->AddString(kDisplayName, Scripts::QBVAL_STRING, displayName.c_str());
}

// 0x0057B920
void CProfileRoster::WriteCustomSkaters(Scripts::CStruct* save)
{
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        CRiderIdentity* profile = m_templates.GetByIndex(static_cast<int>(i), &key);
        if (!profile->IsPro())
            profile->WriteToSave(save);
    }
}

// 0x0057B970
bool CProfileRoster::AddExtraProfile(uint32_t name)
{
    m_extraProfiles[m_extraCount] = NewProfile();
    m_extraNames[m_extraCount] = name;
    ++m_extraCount;
    return true;
}

// 0x0057BA20
void CProfileRoster::SetCurrentSkater(uint32_t name)
{
    MemSys::Manager::Get()->EnterHeapScope(ProfileHeap());
    if (m_cursorPos == 0)
        m_profiles[m_cursorPos] = m_templates.Get(name);
    else
        m_profiles[m_cursorPos]->CopyFrom(m_templates.Get(name));
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x0057BAA0
void CProfileRoster::ReadPros(Scripts::CStruct* save)
{
    MemSys::Manager::Get()->EnterHeapScope(ProfileHeap());
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        CRiderIdentity* profile = m_templates.GetByIndex(static_cast<int>(i), &key);
        if (profile->IsPro())
            profile->ReadFromSave(profile->GetName(), save);
    }
    uint32_t current = 0;
    save->ReadHashValue(kCurrentSkater, &current, 0);
    SetCurrentSkater(current);
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x0057BB40
bool CProfileRoster::AddTemplate(Scripts::CStruct* params)
{
    MemSys::Manager::Get()->EnterHeapScope(ProfileHeap());
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    CRiderIdentity* profile = NewProfile();
    profile->Reset(params);
    m_templates.Put(name, profile);
    MemSys::Manager::Get()->LeaveHeapScope();
    return true;
}

// 0x0057BD70
bool CProfileRoster::ReadCustomSkater(Scripts::CStruct* save, bool keepSavedStats)
{
    SetCurrentSkater(kCustom);
    CRiderIdentity* profile = m_profiles[m_cursorPos];
    Scripts::CStruct* custom = nullptr;
    save->ReadSubStruct(kCustom, &custom, 0);
    int version = 0;
    if (!custom->FetchIntValue(kVersionNumber, &version, 0))
        return false;
    if (version != 2)
        return true;
    // The appearance, and the face texture if the save has one.
    Scripts::CStruct* appearance = nullptr;
    custom->ReadSubStruct(kAppearance, &appearance, 0);
    profile->SetAppearance(appearance, true);
    if (void* face = FaceOf(profile)) {
        Scripts::CStruct* texture;
        bool found = custom->ReadSubStruct(kFaceTexture, &texture, 0);
        if (found) {
            static_cast<Gfx2D::CFaceSkin*>(face)->LoadFromStructure(texture);
            DebugOut::Printf("Face texture found\n");
        } else {
            DebugOut::Printf("Face texture not found\n");
        }
        static_cast<Gfx2D::CFaceSkin*>(face)->MarkUsable(found);
    }
    // The stats and specials as they are, the save's info read over them, then (unless the save's stats are kept)
    // put back.
    int stats[10];
    for (int i = 0; i < 10; ++i)
        stats[i] = profile->GetStat(kStats[i]);
    int maxSpecials = profile->GetMaxSpecials();
    Scripts::CStruct* memory = Scripts::NewStruct();
    Scripts::CStruct* specials = memory ? memory->Init() : nullptr;
    specials->AppendStructureIfAny(profile->GetSpecials());
    Scripts::CStruct* info = nullptr;
    custom->ReadSubStruct(kInfo, &info, 0);
    profile->InfoStructure()->Clear();
    profile->InfoStructure()->AppendStructureIfAny(info);
    if (!keepSavedStats) {
        for (int i = 0; i < 10; ++i)
            profile->SetAbilityLevel(kStats[i], stats[i]);
        profile->SetAbilityLevel(kMaxSpecials, maxSpecials);
        profile->GetSpecials()->Clear();
        profile->GetSpecials()->AppendStructureIfAny(specials);
    }
    if (specials) {
        specials->Clear();
        Scripts::DeleteStruct(specials);
    }
    int deckFlags = 1; // (read, never used)
    return custom->FetchIntValue(kDeckFlags, &deckFlags, 0);
}

// 0x0057C0D0
CProfileRoster* CProfileRoster::Create(bool create)
{
    if (!Instance() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CProfileRoster), nullptr, 1);
        Instance() = memory ? static_cast<CProfileRoster*>(memory)->Construct() : nullptr;
    }
    ++RefTally();
    return Instance();
}

// 0x0057C120
void CProfileRoster::Release()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (CProfileRoster* manager = Instance()) {
        manager->DestroyBody();
        MemSys::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    RefTally() = 0;
}

} // namespace Obj
