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
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

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

Mem::Heap* SkaterInfoHeap() { return Mem::Manager::Get()->m_skaterInfoHeap; }

CPlayerProfileManager*& Instance() { return GameVariable<CPlayerProfileManager*>(0x007CE4B4); }
uint32_t& References() { return GameVariable<uint32_t>(0x007CE4B8); }

// A new profile (constructed: its defaults).
CSkaterProfile* NewProfile()
{
    void* memory = Mem::Calloc(sizeof(CSkaterProfile));
    return memory ? static_cast<CSkaterProfile*>(memory)->Construct() : nullptr;
}

// The face texture object of a model appearance (its +0x54; 0x00576E60, folded with Mdl::Viewer's getter), told a
// face texture (0x004875F0) and whether there is one (0x00487520).
void* FaceOf(CSkaterProfile* profile) { return *reinterpret_cast<void**>(profile->m_appearance + 0x54); }

} // namespace

void CSkaterProfile::SetAppearance(Script::CStruct* appearance, bool update)
{
    reinterpret_cast<Gfx::CModelAppearance*>(this)->Load(appearance, update);
}
// 0x00543010
void CSkaterProfile::DestroyBody()
{
    InfoStructure()->Clear();
    reinterpret_cast<Gfx::CModelAppearance*>(this)->DestroyBody();
}
void CSkaterProfile::Delete()
{
    InfoStructure()->Clear();
    reinterpret_cast<Gfx::CModelAppearance*>(this)->DestroyBody();
    Mem::Manager::Get()->Free(this);
}

// The lookup table's generic code.
CSkaterProfile* ProfileTable::GetByIndex(int index, uint32_t* key)
{
    return static_cast<CSkaterProfile*>(reinterpret_cast<Lst::LookupTable*>(this)->GetItemByIndex(&index, key));
}
CSkaterProfile* ProfileTable::Get(uint32_t key)
{
    return static_cast<CSkaterProfile*>(reinterpret_cast<Lst::LookupTable*>(this)->GetItem(&key));
}
bool ProfileTable::Put(uint32_t key, CSkaterProfile* item)
{
    return reinterpret_cast<Lst::LookupTable*>(this)->PutItem(&key, item);
}
void ProfileTable::Remove(uint32_t key) { reinterpret_cast<Lst::LookupTable*>(this)->FlushItem(&key); }

// 0x0057B9E0
void ProfileTable::Clear()
{
    for (Node* node = m_first; node;) {
        Node* next = node->next;
        Mem::Manager::Get()->Free(node);
        node = next;
    }
    m_first = nullptr;
    m_cursor = nullptr;
    m_count = 0;
}

// 0x0057BBF0
void ProfileTable::Destroy() { Clear(); }

// 0x0057BC00
CPlayerProfileManager* CPlayerProfileManager::Construct()
{
    m_templates.m_count = 0;
    m_templates.m_first = nullptr;
    m_templates.m_cursor = nullptr;
    m_currentIndex = 0;
    m_profiles[0] = nullptr;
    m_profiles[1] = nullptr;
    m_locked = false;
    m_extraCount = 0;
    return this;
}

// 0x0057BC20
void CPlayerProfileManager::DestroyBody()
{
    // Every template (the first each time: dropped from the table as it goes; a null one dropped too).
    for (uint32_t left = m_templates.m_count; left != 0; --left) {
        uint32_t key;
        if (CSkaterProfile* profile = m_templates.GetByIndex(0, &key))
            profile->Delete();
        m_templates.Remove(key);
    }
    // The second player's copy (the first player's is a template), and the extras.
    if (CSkaterProfile* copy = m_profiles[1])
        copy->Delete();
    m_profiles[1] = nullptr;
    for (int i = 0; i < m_extraCount; ++i) {
        if (CSkaterProfile* extra = m_extraProfiles[i])
            extra->Delete();
        m_extraProfiles[i] = nullptr;
    }
    m_templates.Clear();
}

// 0x0057B620 / 0x0057B630 / 0x0057B640
void CPlayerProfileManager::SetCurrentIndex(int index) { m_currentIndex = index; }
CSkaterProfile* CPlayerProfileManager::GetCurrentProfile() { return m_profiles[m_currentIndex]; }
CSkaterProfile* CPlayerProfileManager::GetProfile(int index) { return m_profiles[index]; }

// 0x0057B650
CSkaterProfile* CPlayerProfileManager::GetExtraProfile(uint32_t name)
{
    for (int i = 0; i < m_extraCount; ++i) {
        if (m_extraNames[i] == name)
            return m_extraProfiles[i];
    }
    return nullptr;
}

// 0x0057B680
void CPlayerProfileManager::SetLocked(bool locked) { m_locked = locked; }

// 0x0057B690
void CPlayerProfileManager::Init()
{
    Mem::Manager::Get()->PushContext(SkaterInfoHeap());
    Script::RunScript(kInitProSkaters, nullptr, nullptr, false, nullptr);
    uint32_t key;
    m_profiles[0] = m_templates.GetByIndex(0, &key);
    for (int i = 1; i < 2; ++i) {
        m_profiles[i] = NewProfile();
        m_profiles[i]->CopyFrom(m_templates.GetByIndex(i, &key));
    }
    Mem::Manager::Get()->PopContext();
}

// 0x0057B770
uint32_t CPlayerProfileManager::GetTemplateName(int index)
{
    uint32_t key;
    m_templates.GetByIndex(index, &key);
    return key;
}

// 0x0057B790
void CPlayerProfileManager::ResetSecondProfile()
{
    Mem::Manager::Get()->PushContext(SkaterInfoHeap());
    CSkaterProfile* copy = m_profiles[1];
    copy->CopyFrom(m_templates.Get(copy->GetName()));
    Mem::Manager::Get()->PopContext();
}

// 0x0057B7E0
CSkaterProfile* CPlayerProfileManager::GetTemplate(uint32_t name) { return m_templates.Get(name); }

// 0x0057B800
void CPlayerProfileManager::ResetTemplates()
{
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        m_templates.GetByIndex(static_cast<int>(i), &key)->Reset(nullptr);
    }
}

// 0x0057B840
void CPlayerProfileManager::WritePros(Script::CStruct* save)
{
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        CSkaterProfile* profile = m_templates.GetByIndex(static_cast<int>(i), &key);
        if (profile->IsPro())
            profile->WriteToSave(save);
    }
    save->AddComponentOfType(kCurrentSkater, Script::ESYMBOLTYPE_NAME, m_profiles[m_currentIndex]->GetName());
    Str::String displayName(m_profiles[m_currentIndex]->GetDisplayName());
    save->AddString(kDisplayName, Script::ESYMBOLTYPE_STRING, displayName.c_str());
}

// 0x0057B920
void CPlayerProfileManager::WriteCustomSkaters(Script::CStruct* save)
{
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        CSkaterProfile* profile = m_templates.GetByIndex(static_cast<int>(i), &key);
        if (!profile->IsPro())
            profile->WriteToSave(save);
    }
}

// 0x0057B970
bool CPlayerProfileManager::AddExtraProfile(uint32_t name)
{
    m_extraProfiles[m_extraCount] = NewProfile();
    m_extraNames[m_extraCount] = name;
    ++m_extraCount;
    return true;
}

// 0x0057BA20
void CPlayerProfileManager::SetCurrentSkater(uint32_t name)
{
    Mem::Manager::Get()->PushContext(SkaterInfoHeap());
    if (m_currentIndex == 0)
        m_profiles[m_currentIndex] = m_templates.Get(name);
    else
        m_profiles[m_currentIndex]->CopyFrom(m_templates.Get(name));
    Mem::Manager::Get()->PopContext();
}

// 0x0057BAA0
void CPlayerProfileManager::ReadPros(Script::CStruct* save)
{
    Mem::Manager::Get()->PushContext(SkaterInfoHeap());
    uint32_t count = m_templates.m_count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        CSkaterProfile* profile = m_templates.GetByIndex(static_cast<int>(i), &key);
        if (profile->IsPro())
            profile->ReadFromSave(profile->GetName(), save);
    }
    uint32_t current = 0;
    save->GetChecksum(kCurrentSkater, &current, 0);
    SetCurrentSkater(current);
    Mem::Manager::Get()->PopContext();
}

// 0x0057BB40
bool CPlayerProfileManager::AddTemplate(Script::CStruct* params)
{
    Mem::Manager::Get()->PushContext(SkaterInfoHeap());
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    CSkaterProfile* profile = NewProfile();
    profile->Reset(params);
    m_templates.Put(name, profile);
    Mem::Manager::Get()->PopContext();
    return true;
}

// 0x0057BD70
bool CPlayerProfileManager::ReadCustomSkater(Script::CStruct* save, bool keepSavedStats)
{
    SetCurrentSkater(kCustom);
    CSkaterProfile* profile = m_profiles[m_currentIndex];
    Script::CStruct* custom = nullptr;
    save->GetStructure(kCustom, &custom, 0);
    int version = 0;
    if (!custom->GetInteger(kVersionNumber, &version, 0))
        return false;
    if (version != 2)
        return true;
    // The appearance, and the face texture if the save has one.
    Script::CStruct* appearance = nullptr;
    custom->GetStructure(kAppearance, &appearance, 0);
    profile->SetAppearance(appearance, true);
    if (void* face = FaceOf(profile)) {
        Script::CStruct* texture;
        bool found = custom->GetStructure(kFaceTexture, &texture, 0);
        if (found) {
            static_cast<Gfx::CFaceTexture*>(face)->LoadFromStructure(texture);
            Dbg::Printf("Face texture found\n");
        } else {
            Dbg::Printf("Face texture not found\n");
        }
        static_cast<Gfx::CFaceTexture*>(face)->SetValid(found);
    }
    // The stats and specials as they are, the save's info read over them, then (unless the save's stats are kept)
    // put back.
    int stats[10];
    for (int i = 0; i < 10; ++i)
        stats[i] = profile->GetStat(kStats[i]);
    int maxSpecials = profile->GetMaxSpecials();
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* specials = memory ? memory->Init() : nullptr;
    specials->AppendStructureIfAny(profile->GetSpecials());
    Script::CStruct* info = nullptr;
    custom->GetStructure(kInfo, &info, 0);
    profile->InfoStructure()->Clear();
    profile->InfoStructure()->AppendStructureIfAny(info);
    if (!keepSavedStats) {
        for (int i = 0; i < 10; ++i)
            profile->SetStat(kStats[i], stats[i]);
        profile->SetStat(kMaxSpecials, maxSpecials);
        profile->GetSpecials()->Clear();
        profile->GetSpecials()->AppendStructureIfAny(specials);
    }
    if (specials) {
        specials->Clear();
        Script::DeleteStruct(specials);
    }
    int deckFlags = 1; // (read, never used)
    return custom->GetInteger(kDeckFlags, &deckFlags, 0);
}

// 0x0057C0D0
CPlayerProfileManager* CPlayerProfileManager::Create(bool create)
{
    if (!Instance() && create) {
        void* memory = Mem::Calloc(sizeof(CPlayerProfileManager), nullptr, 1);
        Instance() = memory ? static_cast<CPlayerProfileManager*>(memory)->Construct() : nullptr;
    }
    ++References();
    return Instance();
}

// 0x0057C120
void CPlayerProfileManager::Release()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (CPlayerProfileManager* manager = Instance()) {
        manager->DestroyBody();
        Mem::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

} // namespace Obj

REPLACE(0x0057B620, &Obj::CPlayerProfileManager::SetCurrentIndex);
REPLACE(0x0057B630, &Obj::CPlayerProfileManager::GetCurrentProfile);
REPLACE(0x0057B640, &Obj::CPlayerProfileManager::GetProfile);
REPLACE(0x0057B650, &Obj::CPlayerProfileManager::GetExtraProfile);
REPLACE(0x0057B680, &Obj::CPlayerProfileManager::SetLocked);
REPLACE(0x0057B690, &Obj::CPlayerProfileManager::Init);
REPLACE(0x0057B770, &Obj::CPlayerProfileManager::GetTemplateName);
REPLACE(0x0057B790, &Obj::CPlayerProfileManager::ResetSecondProfile);
REPLACE(0x0057B7E0, &Obj::CPlayerProfileManager::GetTemplate);
REPLACE(0x0057B800, &Obj::CPlayerProfileManager::ResetTemplates);
REPLACE(0x0057B840, &Obj::CPlayerProfileManager::WritePros);
REPLACE(0x0057B920, &Obj::CPlayerProfileManager::WriteCustomSkaters);
REPLACE(0x0057B970, &Obj::CPlayerProfileManager::AddExtraProfile);
REPLACE(0x0057B9E0, &Obj::ProfileTable::Clear);
REPLACE(0x0057BA20, &Obj::CPlayerProfileManager::SetCurrentSkater);
REPLACE(0x0057BAA0, &Obj::CPlayerProfileManager::ReadPros);
REPLACE(0x0057BB40, &Obj::CPlayerProfileManager::AddTemplate);
REPLACE(0x0057BBF0, &Obj::ProfileTable::Destroy);
REPLACE(0x0057BC00, &Obj::CPlayerProfileManager::Construct);
REPLACE(0x0057BC20, &Obj::CPlayerProfileManager::DestroyBody);
REPLACE(0x0057BD70, &Obj::CPlayerProfileManager::ReadCustomSkater);
REPLACE(0x0057C0D0, Obj::CPlayerProfileManager::Create);
REPLACE(0x0057C120, Obj::CPlayerProfileManager::Release);
REPLACE(0x00543010, &Obj::CSkaterProfile::DestroyBody); // (its address among the network players' code)
