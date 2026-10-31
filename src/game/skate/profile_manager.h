#pragma once

#include <cstddef>
#include <cstdint>

// The player profile manager (0x0057B620-0x0057C165): the skater profiles the game knows (the pros and the custom
// skaters, made by the init_pro_skaters script through AddTemplate), keyed by name in a lookup table, and the
// profiles in use: the first player's is one of those templates itself, the second player's a copy (made by Init),
// and up to three extra profiles by name. The skate module reads the current one (index +0x1C: what
// sio/device.cpp's MenuPort reads, 0x00553E90). One instance, made by Create (0x0057C0D0) and released by Release
// (0x0057C120), counted, at 0x007CE4B4 / 0x007CE4B8.
namespace Scripts {
class CStruct;
}

namespace GameObjects {

// A skater profile (0x64 bytes: a model appearance - constructor 0x00488EA0, destructor body 0x004890F0 - and its
// info structure at +0x5C: name, display_name, is_pro, the stats, specials, ...). Its own code is in
// skater_profile.cpp (0x00585ED0-0x005871A0); the appearance's (0x00488xxx-0x00489xxx) is called through the game.
struct CRiderIdentity {
    uint8_t m_lookData[0x5C];
    uint8_t m_info[8]; // +0x5C (a Scripts::CStruct)

    CRiderIdentity* Construct();                           // 0x00586D60
    uint32_t GetName();                                    // 0x00586150 (its info's name)
    bool IsPro();                                          // 0x00586170 (is_pro)
    const char* GetDisplayName();                          // 0x00586210 (display_name)
    Scripts::CStruct* GetSpecials();                        // 0x00586130 (specials)
    int GetMaxSpecials();                                  // 0x005860A0 (max_specials)
    int GetStat(uint32_t name);                            // 0x00585FB0
    bool SetAbilityLevel(uint32_t name, int value);                // 0x00585FD0 (0-10 only)
    CRiderIdentity* CopyFrom(CRiderIdentity* other);       // 0x00585ED0
    void ReadFromSave(uint32_t name, Scripts::CStruct* save); // 0x00585F00 (its part of a save, by name)
    void WriteToSave(Scripts::CStruct* save);               // 0x00586B00
    bool Reset(Scripts::CStruct* params);                   // 0x00586810 (its defaults, then `params`; true)
    void SetAppearance(Scripts::CStruct* appearance, bool update); // 0x00488F40
    Scripts::CStruct* InfoStructure() { return reinterpret_cast<Scripts::CStruct*>(m_info); }
    // The destructor (inlined by the manager): the info cleared, the appearance's destructor body, freed.
    void Delete();
    // 0x00543010: the destructor body (Delete without the free): the info cleared, the appearance's destructor body.
    void DestroyBody();

    // (skater_profile.cpp) 0x00586000 / 0x00586040: the info's 0x471FC4DD integer increased (true) / read.
    // 0x00586060: max_specials increased (true). 0x005860C0: the info's stance or pushstyle (anything else: 0).
    // 0x00586110: an info structure by name. 0x00586190: the info's 0xF3149AC5 flag. 0x005861A0: its 0x09F350E5
    // integer set. 0x005861C0 / 0x005861F0: its 0xF36C1878 string ("Unimplemented" if none) / set.
    bool AddToInfo471FC4DD(int amount);
    int GetInfo471FC4DD();
    bool AddMaxSpecials(int amount);
    uint32_t GetNameKeyField(uint32_t name);
    Scripts::CStruct* GetInfoStructure(uint32_t name);
    bool HasFlagF3149AC5();
    void SetValue09F350E5(uint8_t value);
    const char* GetStringF36C1878();
    void SetStringF36C1878(const char* text);

    // A special trick slot: the trick's name and keys, and a flag.
    struct SpecialTrick {
        uint32_t trick;
        uint32_t keys;
        bool flag;
        uint8_t pad9[3];
        // 0x00587100: the flag cleared. 0x00587110: whether it's the empty one (0xF60C9090).
        SpecialTrick* Construct();
        bool IsEmpty();
    };
    // 0x00586630: special trick slot `index` (returned through `out`). 0x00586710: set (an empty one stored as
    // 0xF60C9090 both), the skater's trick state told if `apply`. False for a slot out of range.
    SpecialTrick* GetSpecialTrick(SpecialTrick* out, int index);
    bool SetSpecialTrick(int index, SpecialTrick* trick, bool apply);

    // 0x00586230: from `params` (none: nothing): with `info`, the appearance (by name) and the info's names, stats
    // and checksums (a custom skater gets a face texture); with `specials`, the specials, max_specials, the trick
    // style structure and every stat named in the global stat list. True.
    bool SetFromParams(Scripts::CStruct* params, bool info, bool specials);
    // The network copy (in the script heap): 0x00587050 the appearance, the trick style structure, the specials and
    // the chosen info names written to `buffer` (returns the bytes); 0x00586DC0 read back (returns the end).
    // 0x00586E70 / 0x00586A40: the info's part alone.
    int EmitIntoBuffer(uint8_t* buffer, int size, int flags);
    uint8_t* DecodeFromBytes(uint8_t* buffer);
    int WriteInfoToBuffer(uint8_t* buffer, int size);
    uint8_t* ReadInfoFromBuffer(uint8_t* buffer);
};
static_assert(sizeof(CRiderIdentity) == 0x64, "layout must match the original");

// 0x00587120 / 0x00587130: a list of distinct checksums (up to 256, its count at +0x400): emptied / filled from the
// global array 0x5004A967.
struct ChecksumSet {
    uint32_t m_items[256];
    int m_count; // +0x400
    ChecksumSet* Construct();
    void Fill();
};

// ListKit::LookupTable<CRiderIdentity> (0x14 bytes): a singly linked list of {key, item, next} nodes (0xC bytes) with a
// cursor for walking it by index. Its generic code (0x00557F10 by index, 0x00561430 by key, 0x005628B0 add,
// 0x00562970 remove) is outside this range: called through the game.
struct ProfileTable {
    struct Node {
        uint32_t key;
        CRiderIdentity* item;
        Node* next;
    };
    uint32_t m_count;  // +0x00
    Node* m_first;     // +0x04
    Node* m_last;      // +0x08
    Node* m_cursor;    // +0x0C
    int m_cursorIndex; // +0x10

    CRiderIdentity* GetByIndex(int index, uint32_t* key);
    CRiderIdentity* Get(uint32_t key);
    bool Put(uint32_t key, CRiderIdentity* item);
    void Remove(uint32_t key);
    // 0x0057B9E0: every node freed (not the items); no nodes, no cursor, none counted (m_last is left as it was).
    void Clear();
    // 0x0057BBF0: the destructor (a jump to Clear).
    void Destroy();
};
static_assert(sizeof(ProfileTable) == 0x14, "layout must match the original");

class CProfileRoster {
public:
    // 0x0057BC00: the constructor: nothing in it. Returns this.
    CProfileRoster* Construct();
    // 0x0057BC20: the destructor body: every template deleted and dropped from the table, the second player's
    // copy and the extra profiles deleted, the table cleared.
    void DestroyBody();
    // 0x0057B690: the templates made (the init_pro_skaters script, in the skater info heap); the first player's
    // profile is the first template, the second's a copy of the second.
    void Init();
    // 0x0057BB40: a template from `params` (its name; a new profile reset to `params`), added to the table. True.
    bool AddTemplate(Scripts::CStruct* params);
    // 0x0057B970: an extra profile named `name` (a new one). True. 0x0057B650: the extra profile named `name`, or
    // null.
    bool AddExtraProfile(uint32_t name);
    CRiderIdentity* GetExtraProfile(uint32_t name);
    // 0x0057B7E0: the template named `name` (null if none). 0x0057B770: the name of the template at `index`.
    CRiderIdentity* GetTemplate(uint32_t name);
    uint32_t GetTemplateName(int index);
    // 0x0057B620: which profile is current. 0x0057B630: the current profile. 0x0057B640: the profile at `index`.
    void SetCurrentIndex(int index);
    CRiderIdentity* GetCurrentProfile();
    CRiderIdentity* GetProfile(int index);
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
    void WritePros(Scripts::CStruct* save);
    void WriteCustomSkaters(Scripts::CStruct* save);
    // 0x0057BAA0: every pro read from `save`, then the current skater set from its CurrentSkater.
    void ReadPros(Scripts::CStruct* save);
    // 0x0057BD70: the custom skater read from `save`'s custom structure (version 2 only): its appearance (the face
    // texture, if any), its info - and, unless `keepSavedStats`, the stats and specials it had before put back.
    // Returns what the last structure lookup did (the deck flags' in a version 2 save).
    bool ReadCustomSkater(Scripts::CStruct* save, bool keepSavedStats);

    // 0x0057C0D0: the instance (made first, if `create`); counted. 0x0057C120: one count released; the last
    // deletes it.
    static CProfileRoster* Create(bool create);
    static void Release();

    ProfileTable m_templates;            // +0x00
    CRiderIdentity* m_profiles[2];       // +0x14 (the first: a template; the second: a copy)
    int m_cursorPos;                  // +0x1C
    bool m_locked;                       // +0x20
    uint8_t pad21[3];
    int m_extraCount;                    // +0x24
    uint32_t m_extraNames[3];            // +0x28
    CRiderIdentity* m_extraProfiles[3];  // +0x34
};
static_assert(offsetof(CProfileRoster, m_profiles) == 0x14 &&
                  offsetof(CProfileRoster, m_extraCount) == 0x24 &&
                  offsetof(CProfileRoster, m_extraProfiles) == 0x34 && sizeof(CProfileRoster) == 0x40,
              "layout must match the original");

extern GameObjects::CProfileRoster* g_profileManager;

} // namespace Obj
