#pragma once

#include <cstddef>
#include <cstdint>

// The player profile manager (0x0057B620-0x0057C165): the skater profiles the game knows (the pros and the custom
// skaters, made by the init_pro_skaters script through AddTemplate), keyed by name in a lookup table, and the
// profiles in use: the first player's is one of those templates itself, the second player's a copy (made by Init),
// and up to three extra profiles by name. The skate module reads the current one (index +0x1C: what
// sio/device.cpp's MenuPort reads, 0x00553E90). One instance, made by Create (0x0057C0D0) and released by Release
// (0x0057C120), counted, at 0x007CE4B4 / 0x007CE4B8.
namespace Script {
class CStruct;
}

namespace Obj {

// A skater profile (0x64 bytes: a model appearance - constructor 0x00488EA0, destructor body 0x004890F0 - and its
// info structure at +0x5C: name, display_name, is_pro, the stats, specials, ...). Its own code (0x00585xxx-
// 0x00586xxx) is a later batch: called through the game here.
struct CSkaterProfile {
    uint8_t m_appearance[0x5C];
    uint8_t m_info[8]; // +0x5C (a Script::CStruct)

    CSkaterProfile* Construct();                           // 0x00586D60
    uint32_t GetName();                                    // 0x00586150 (its info's name)
    bool IsPro();                                          // 0x00586170 (is_pro)
    const char* GetDisplayName();                          // 0x00586210 (display_name)
    Script::CStruct* GetSpecials();                        // 0x00586130 (specials)
    int GetMaxSpecials();                                  // 0x005860A0 (max_specials)
    int GetStat(uint32_t name);                            // 0x00585FB0
    bool SetStat(uint32_t name, int value);                // 0x00585FD0 (0-10 only)
    CSkaterProfile* CopyFrom(CSkaterProfile* other);       // 0x00585ED0
    void ReadFromSave(uint32_t name, Script::CStruct* save); // 0x00585F00 (its part of a save, by name)
    void WriteToSave(Script::CStruct* save);               // 0x00586B00
    void Reset(Script::CStruct* params);                   // 0x00586810 (its defaults, then `params`)
    void SetAppearance(Script::CStruct* appearance, bool update); // 0x00488F40
    Script::CStruct* InfoStructure() { return reinterpret_cast<Script::CStruct*>(m_info); }
    // The destructor (inlined by the manager): the info cleared, the appearance's destructor body, freed.
    void Delete();
};
static_assert(sizeof(CSkaterProfile) == 0x64, "layout must match the original");

// Lst::LookupTable<CSkaterProfile> (0x14 bytes): a singly linked list of {key, item, next} nodes (0xC bytes) with a
// cursor for walking it by index. Its generic code (0x00557F10 by index, 0x00561430 by key, 0x005628B0 add,
// 0x00562970 remove) is outside this range: called through the game.
struct ProfileTable {
    struct Node {
        uint32_t key;
        CSkaterProfile* item;
        Node* next;
    };
    uint32_t m_count;  // +0x00
    Node* m_first;     // +0x04
    Node* m_last;      // +0x08
    Node* m_cursor;    // +0x0C
    int m_cursorIndex; // +0x10

    CSkaterProfile* GetByIndex(int index, uint32_t* key);
    CSkaterProfile* Get(uint32_t key);
    bool Put(uint32_t key, CSkaterProfile* item);
    void Remove(uint32_t key);
    // 0x0057B9E0: every node freed (not the items); no nodes, no cursor, none counted (m_last is left as it was).
    void Clear();
    // 0x0057BBF0: the destructor (a jump to Clear).
    void Destroy();
};
static_assert(sizeof(ProfileTable) == 0x14, "layout must match the original");

class CPlayerProfileManager {
public:
    // 0x0057BC00: the constructor: nothing in it. Returns this.
    CPlayerProfileManager* Construct();
    // 0x0057BC20: the destructor body: every template deleted and dropped from the table, the second player's
    // copy and the extra profiles deleted, the table cleared.
    void DestroyBody();
    // 0x0057B690: the templates made (the init_pro_skaters script, in the skater info heap); the first player's
    // profile is the first template, the second's a copy of the second.
    void Init();
    // 0x0057BB40: a template from `params` (its name; a new profile reset to `params`), added to the table. True.
    bool AddTemplate(Script::CStruct* params);
    // 0x0057B970: an extra profile named `name` (a new one). True. 0x0057B650: the extra profile named `name`, or
    // null.
    bool AddExtraProfile(uint32_t name);
    CSkaterProfile* GetExtraProfile(uint32_t name);
    // 0x0057B7E0: the template named `name` (null if none). 0x0057B770: the name of the template at `index`.
    CSkaterProfile* GetTemplate(uint32_t name);
    uint32_t GetTemplateName(int index);
    // 0x0057B620: which profile is current. 0x0057B630: the current profile. 0x0057B640: the profile at `index`.
    void SetCurrentIndex(int index);
    CSkaterProfile* GetCurrentProfile();
    CSkaterProfile* GetProfile(int index);
    // 0x0057B680: the lock flag (+0x20).
    void SetLocked(bool locked);
    // 0x0057BA20: the current skater made the template named `name`: for the first player the template itself,
    // otherwise copied into the current profile (in the skater info heap).
    void SetCurrentSkater(uint32_t name);
    // 0x0057B790: the second player's profile copied again from its template.
    void ResetSecondProfile();
    // 0x0057B800: every template reset to its defaults.
    void ResetTemplates();
    // 0x0057B840: every pro written to `save`, then the current skater's name (CurrentSkater) and display name
    // (DisplayName). 0x0057B920: every skater that isn't a pro written to `save`.
    void WritePros(Script::CStruct* save);
    void WriteCustomSkaters(Script::CStruct* save);
    // 0x0057BAA0: every pro read from `save`, then the current skater set from its CurrentSkater.
    void ReadPros(Script::CStruct* save);
    // 0x0057BD70: the custom skater read from `save`'s custom structure (version 2 only): its appearance (the face
    // texture, if any), its info - and, unless `keepSavedStats`, the stats and specials it had before put back.
    // Returns what the last structure lookup did (the deck flags' in a version 2 save).
    bool ReadCustomSkater(Script::CStruct* save, bool keepSavedStats);

    // 0x0057C0D0: the instance (made first, if `create`); counted. 0x0057C120: one count released; the last
    // deletes it.
    static CPlayerProfileManager* Create(bool create);
    static void Release();

    ProfileTable m_templates;            // +0x00
    CSkaterProfile* m_profiles[2];       // +0x14 (the first: a template; the second: a copy)
    int m_currentIndex;                  // +0x1C
    bool m_locked;                       // +0x20
    uint8_t pad21[3];
    int m_extraCount;                    // +0x24
    uint32_t m_extraNames[3];            // +0x28
    CSkaterProfile* m_extraProfiles[3];  // +0x34
};
static_assert(offsetof(CPlayerProfileManager, m_profiles) == 0x14 &&
                  offsetof(CPlayerProfileManager, m_extraCount) == 0x24 &&
                  offsetof(CPlayerProfileManager, m_extraProfiles) == 0x34 && sizeof(CPlayerProfileManager) == 0x40,
              "layout must match the original");

} // namespace Obj
