#pragma once

#include <cstddef>
#include <cstdint>

// The player profile manager (0x0057B620-0x0057C165): the skater profiles the game knows (the pros and the custom
// skaters, made by the init_pro_skaters script through add_template), keyed by name in a lookup table, and the
// profiles in use: the first player's is one of those templates itself, the second player's a copy (made by Init),
// and up to three extra profiles by name. The skate module reads the current one (index +0x1C: what
// sio/device.cpp's get_menu_port reads, 0x00553E90). One instance, made by Create (0x0057C0D0) and released by Release
// (0x0057C120), counted, at 0x007CE4B4 / 0x007CE4B8.
namespace script {
class Struct;
}
namespace gfx {
class ModelLook;
}

namespace objects {

// A skater profile (0x64 bytes: a model appearance - constructor 0x00488EA0, destructor body 0x004890F0 - and its
// info structure at +0x5C: name, display_name, is_pro, the stats, specials, ...). Its own code is in
// skater_profile.cpp (0x00585ED0-0x005871A0); the appearance's (0x00488xxx-0x00489xxx) is called through the game.
struct RiderIdentity {
    alignas(4) uint8_t look_data_[0x5C]; // a gfx::ModelLook, made in place by make_new (appearance())
    uint8_t details_[8];                 // +0x5C (a Scripts::CStruct)

    RiderIdentity* make_new();                                    // 0x00586D60
    uint32_t fetch_title();                                       // 0x00586150 (its info's name)
    bool is_pro();                                                // 0x00586170 (is_pro)
    const char* fetch_shown_name();                               // 0x00586210 (display_name)
    script::Struct* get_specials();                               // 0x00586130 (specials)
    int get_max_specials();                                       // 0x005860A0 (max_specials)
    int fetch_tally(uint32_t name);                               // 0x00585FB0
    bool set_ability_level(uint32_t name, int value);             // 0x00585FD0 (0-10 only)
    RiderIdentity* copy_from(RiderIdentity* other);               // 0x00585ED0
    void read_from_save(uint32_t name, script::Struct* save);     // 0x00585F00 (its part of a save, by name)
    void write_to_save(script::Struct* save);                     // 0x00586B00
    bool get_reset(script::Struct* params);                       // 0x00586810 (its defaults, then `params`; true)
    void set_appearance(script::Struct* appearance, bool update); // 0x00488F40
    script::Struct* details_params() { return reinterpret_cast<script::Struct*>(details_); }
    gfx::ModelLook* appearance() { return reinterpret_cast<gfx::ModelLook*>(look_data_); }
    // The destructor (inlined by the manager): the info cleared, the appearance's destructor body, freed.
    void erase_one();
    // 0x00543010: the destructor body (Delete without the free): the info cleared, the appearance's destructor body.
    void destroy_body();

    // (skater_profile.cpp) 0x00586000 / 0x00586040: the info's 0x471FC4DD integer increased (true) / read.
    // 0x00586060: max_specials increased (true). 0x005860C0: the info's stance or pushstyle (anything else: 0).
    // 0x00586110: an info structure by name. 0x00586190: the info's 0xF3149AC5 flag. 0x005861A0: its 0x09F350E5
    // integer set. 0x005861C0 / 0x005861F0: its 0xF36C1878 string ("Unimplemented" if none) / set.
    bool add_to_info_471f_c_4d_d(int amount);
    int get_info_471f_c_4d_d();
    bool add_max_specials(int amount);
    uint32_t get_name_key_field(uint32_t name);
    script::Struct* fetch_details_params(uint32_t name);
    bool has_flag_f_3149a_c5();
    void set_value_09f_350e_5(uint8_t value);
    const char* get_string_f_36c_1878();
    void set_string_f_36c_1878(const char* text);

    // A special trick slot: the trick's name and keys, and a flag.
    struct SpecialTrick {
        uint32_t trick;
        uint32_t keys;
        bool flag;
        uint8_t pad9[3];
        // 0x00587100: the flag cleared. 0x00587110: whether it's the empty one (0xF60C9090).
        SpecialTrick* make_new();
        bool is_vacant();
    };
    // 0x00586630: special trick slot `index` (returned through `out`). 0x00586710: set (an empty one stored as
    // 0xF60C9090 both), the skater's trick state told if `apply`. False for a slot out of range.
    SpecialTrick* get_special_trick(SpecialTrick* out, int index);
    bool set_special_trick(int index, SpecialTrick* trick, bool apply);

    // 0x00586230: from `params` (none: nothing): with `info`, the appearance (by name) and the info's names, stats
    // and checksums (a custom skater gets a face texture); with `specials`, the specials, max_specials, the trick
    // style structure and every stat named in the global stat list. True.
    bool set_from_params(script::Struct* params, bool info, bool specials);
    // The network copy (in the script heap): 0x00587050 the appearance, the trick style structure, the specials and
    // the chosen info names written to `buffer` (returns the bytes); 0x00586DC0 read back (returns the end).
    // 0x00586E70 / 0x00586A40: the info's part alone.
    int emit_into_buffer(uint8_t* buffer, int size, int flags);
    uint8_t* decode_from_bytes(uint8_t* buffer);
    int write_info_to_buffer(uint8_t* buffer, int size);
    uint8_t* read_info_from_buffer(uint8_t* buffer);
};
static_assert(sizeof(RiderIdentity) == 0x64, "layout must match the original");

// 0x00587120 / 0x00587130: a list of distinct checksums (up to 256, its count at +0x400): emptied / filled from the
// global array 0x5004A967.
struct ChecksumSet {
    uint32_t items_[256];
    int num_; // +0x400
    ChecksumSet* make_new();
    void get_fill();
};

// lists::ResolveMap<RiderIdentity> (0x14 bytes): a singly linked list of {key, item, next} nodes (0xC bytes) with a
// cursor for walking it by index. Its generic code (0x00557F10 by index, 0x00561430 by key, 0x005628B0 add,
// 0x00562970 remove) is outside this range: called through the game.
struct ProfileTable {
    struct Elem {
        uint32_t key;
        RiderIdentity* item;
        Elem* next;
    };
    uint32_t num_;     // +0x00
    Elem* first_;      // +0x04
    Elem* last_;       // +0x08
    Elem* cursor_;     // +0x0C
    int cursor_index_; // +0x10

    RiderIdentity* get_by_index(int index, uint32_t* key);
    RiderIdentity* get_pull(uint32_t key);
    bool place_in(uint32_t key, RiderIdentity* item);
    void drop_out(uint32_t key);
    // 0x0057B9E0: every node freed (not the items); no nodes, no cursor, none counted (last_ is left as it was).
    void get_clear();
    // 0x0057BBF0: the destructor (a jump to Clear).
    void teardown();
};
static_assert(sizeof(ProfileTable) == 0x14, "layout must match the original");

class ProfileRoster {
public:
    // 0x0057BC00: the constructor: nothing in it. Returns this.
    ProfileRoster* make_new();
    // 0x0057BC20: the destructor body: every template deleted and dropped from the table, the second player's
    // copy and the extra profiles deleted, the table cleared.
    void destroy_body();
    // 0x0057B690: the templates made (the init_pro_skaters script, in the skater info heap); the first player's
    // profile is the first template, the second's a copy of the second.
    void prepare_all();
    // 0x0057BB40: a template from `params` (its name; a new profile reset to `params`), added to the table. True.
    bool add_template(script::Struct* params);
    // 0x0057B970: an extra profile named `name` (a new one). True. 0x0057B650: the extra profile named `name`, or
    // null.
    bool add_extra_profile(uint32_t name);
    RiderIdentity* get_extra_profile(uint32_t name);
    // 0x0057B7E0: the template named `name` (null if none). 0x0057B770: the name of the template at `index`.
    RiderIdentity* get_template(uint32_t name);
    uint32_t get_template_name(int index);
    // 0x0057B620: which profile is current. 0x0057B630: the current profile. 0x0057B640: the profile at `index`.
    void set_current_index(int index);
    RiderIdentity* fetch_active_account();
    RiderIdentity* fetch_account(int index);
    // 0x0057B680: the lock flag (+0x20).
    void set_locked(bool locked);
    // 0x0057BA20: the current skater made the template named `name`: for the first player the template itself,
    // otherwise copied into the current profile (in the skater info heap).
    void set_current_skater(uint32_t name);
    // 0x0057B790: the second player's profile copied again from its template.
    void reset_second_profile();
    // 0x0057B800: every template reset to its defaults.
    void reset_templates();
    // 0x0057B840: every pro written to `save`, then the current skater's name (CurrentSkater) and display name
    // (DisplayName). 0x0057B920: every skater that isn't a pro written to `save`.
    void write_pros(script::Struct* save);
    void write_custom_skaters(script::Struct* save);
    // 0x0057BAA0: every pro read from `save`, then the current skater set from its CurrentSkater.
    void read_pros(script::Struct* save);
    // 0x0057BD70: the custom skater read from `save`'s custom structure (version 2 only): its appearance (the face
    // texture, if any), its info - and, unless `keep_saved_stats`, the stats and specials it had before put back.
    // Returns what the last structure lookup did (the deck flags' in a version 2 save).
    bool read_custom_skater(script::Struct* save, bool keep_saved_stats);

    // 0x0057C0D0: the instance (made first, if `create`); counted. 0x0057C120: one count released; the last
    // deletes it.
    static ProfileRoster* fabricate(bool create);
    static void get_release();

    ProfileTable templates_;     // +0x00
    RiderIdentity* profiles_[2]; // +0x14 (the first: a template; the second: a copy)
    int cursor_pos_;             // +0x1C
    bool locked_;                // +0x20
    uint8_t pad21[3];
    int extra_count_;                  // +0x24
    uint32_t extra_names_[3];          // +0x28
    RiderIdentity* extra_profiles_[3]; // +0x34
};
static_assert(offsetof(ProfileRoster, profiles_) == 0x14 && offsetof(ProfileRoster, extra_count_) == 0x24 &&
                  offsetof(ProfileRoster, extra_profiles_) == 0x34 && sizeof(ProfileRoster) == 0x40,
              "layout must match the original");

extern objects::ProfileRoster* g_profile_roster;

} // namespace objects
