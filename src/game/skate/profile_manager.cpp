// The player profile manager, 0x0057B620-0x0057C165. See profile_manager.h.

#include "game/skate/profile_manager.h"

#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/text_store.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model_appearance.h"
#include "engine/lists/lookup_table.h"

namespace objects {

objects::ProfileRoster* g_profile_roster;
uint32_t g_profile_manager_references;

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

memory::Arena* profile_heap() { return memory::Hub::get_pull()->skater_info_heap_; }

ProfileRoster*& sole() { return g_profile_roster; }
uint32_t& ref_tally() { return g_profile_manager_references; }

// A new profile (constructed: its defaults).
RiderIdentity* new_profile()
{
    void* memory = memory::alloc_zeroed(sizeof(RiderIdentity));
    return memory ? static_cast<RiderIdentity*>(memory)->make_new() : nullptr;
}

// The face texture object of a model appearance (its +0x54; 0x00576E60, folded with modules::Viewer's getter), told a
// face texture (0x004875F0) and whether there is one (0x00487520).
void* face_of(RiderIdentity* profile) { return *reinterpret_cast<void**>(profile->look_data_ + 0x54); }

} // namespace

void RiderIdentity::set_appearance(script::Struct* appearance, bool update)
{
    reinterpret_cast<gfx::ModelLook*>(this)->read_in(appearance, update);
}
// 0x00543010
void RiderIdentity::destroy_body()
{
    details_params()->get_clear();
    reinterpret_cast<gfx::ModelLook*>(this)->destroy_body();
}
void RiderIdentity::erase_one()
{
    details_params()->get_clear();
    reinterpret_cast<gfx::ModelLook*>(this)->destroy_body();
    memory::Hub::get_pull()->dealloc(this);
}

// The lookup table's generic code.
RiderIdentity* ProfileTable::get_by_index(int index, uint32_t* key)
{
    return static_cast<RiderIdentity*>(reinterpret_cast<lists::ResolveMap*>(this)->fetch_entry_at(&index, key));
}
RiderIdentity* ProfileTable::get_pull(uint32_t key)
{
    return static_cast<RiderIdentity*>(reinterpret_cast<lists::ResolveMap*>(this)->fetch_entry(&key));
}
bool ProfileTable::place_in(uint32_t key, RiderIdentity* item)
{
    return reinterpret_cast<lists::ResolveMap*>(this)->store_entry(&key, item);
}
void ProfileTable::drop_out(uint32_t key) { reinterpret_cast<lists::ResolveMap*>(this)->erase_entry(&key); }

// 0x0057B9E0
void ProfileTable::get_clear()
{
    for (Elem* node = first_; node;) {
        Elem* next = node->next;
        memory::Hub::get_pull()->dealloc(node);
        node = next;
    }
    first_ = nullptr;
    cursor_ = nullptr;
    num_ = 0;
}

// 0x0057BBF0
void ProfileTable::teardown() { get_clear(); }

// 0x0057BC00
ProfileRoster* ProfileRoster::make_new()
{
    templates_.num_ = 0;
    templates_.first_ = nullptr;
    templates_.cursor_ = nullptr;
    cursor_pos_ = 0;
    profiles_[0] = nullptr;
    profiles_[1] = nullptr;
    locked_ = false;
    extra_count_ = 0;
    return this;
}

// 0x0057BC20
void ProfileRoster::destroy_body()
{
    // Every template (the first each time: dropped from the table as it goes; a null one dropped too).
    for (uint32_t left = templates_.num_; left != 0; --left) {
        uint32_t key;
        if (RiderIdentity* profile = templates_.get_by_index(0, &key))
            profile->erase_one();
        templates_.drop_out(key);
    }
    // The second player's copy (the first player's is a template), and the extras.
    if (RiderIdentity* copy = profiles_[1])
        copy->erase_one();
    profiles_[1] = nullptr;
    for (int i = 0; i < extra_count_; ++i) {
        if (RiderIdentity* extra = extra_profiles_[i])
            extra->erase_one();
        extra_profiles_[i] = nullptr;
    }
    templates_.get_clear();
}

// 0x0057B620 / 0x0057B630 / 0x0057B640
void ProfileRoster::set_current_index(int index) { cursor_pos_ = index; }
RiderIdentity* ProfileRoster::fetch_active_account() { return profiles_[cursor_pos_]; }
RiderIdentity* ProfileRoster::fetch_account(int index) { return profiles_[index]; }

// 0x0057B650
RiderIdentity* ProfileRoster::get_extra_profile(uint32_t name)
{
    for (int i = 0; i < extra_count_; ++i) {
        if (extra_names_[i] == name)
            return extra_profiles_[i];
    }
    return nullptr;
}

// 0x0057B680
void ProfileRoster::set_locked(bool locked) { locked_ = locked; }

// 0x0057B690
void ProfileRoster::prepare_all()
{
    memory::Hub::get_pull()->enter_heap_scope(profile_heap());
    script::launch_script(kInitProSkaters, nullptr, nullptr, false, nullptr);
    uint32_t key;
    profiles_[0] = templates_.get_by_index(0, &key);
    for (int i = 1; i < 2; ++i) {
        profiles_[i] = new_profile();
        profiles_[i]->copy_from(templates_.get_by_index(i, &key));
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0057B770
uint32_t ProfileRoster::get_template_name(int index)
{
    uint32_t key;
    templates_.get_by_index(index, &key);
    return key;
}

// 0x0057B790
void ProfileRoster::reset_second_profile()
{
    memory::Hub::get_pull()->enter_heap_scope(profile_heap());
    RiderIdentity* copy = profiles_[1];
    copy->copy_from(templates_.get_pull(copy->fetch_title()));
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0057B7E0
RiderIdentity* ProfileRoster::get_template(uint32_t name) { return templates_.get_pull(name); }

// 0x0057B800
void ProfileRoster::reset_templates()
{
    uint32_t count = templates_.num_;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        templates_.get_by_index(static_cast<int>(i), &key)->get_reset(nullptr);
    }
}

// 0x0057B840
void ProfileRoster::write_pros(script::Struct* save)
{
    uint32_t count = templates_.num_;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        RiderIdentity* profile = templates_.get_by_index(static_cast<int>(i), &key);
        if (profile->is_pro())
            profile->write_to_save(save);
    }
    save->add_component_of_type(kCurrentSkater, script::kQbvalName, profiles_[cursor_pos_]->fetch_title());
    text::Chars display_name(profiles_[cursor_pos_]->fetch_shown_name());
    save->append_word(kDisplayName, script::kQbvalString, display_name.c_str());
}

// 0x0057B920
void ProfileRoster::write_custom_skaters(script::Struct* save)
{
    uint32_t count = templates_.num_;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        RiderIdentity* profile = templates_.get_by_index(static_cast<int>(i), &key);
        if (!profile->is_pro())
            profile->write_to_save(save);
    }
}

// 0x0057B970
bool ProfileRoster::add_extra_profile(uint32_t name)
{
    extra_profiles_[extra_count_] = new_profile();
    extra_names_[extra_count_] = name;
    ++extra_count_;
    return true;
}

// 0x0057BA20
void ProfileRoster::set_current_skater(uint32_t name)
{
    memory::Hub::get_pull()->enter_heap_scope(profile_heap());
    if (cursor_pos_ == 0)
        profiles_[cursor_pos_] = templates_.get_pull(name);
    else
        profiles_[cursor_pos_]->copy_from(templates_.get_pull(name));
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0057BAA0
void ProfileRoster::read_pros(script::Struct* save)
{
    memory::Hub::get_pull()->enter_heap_scope(profile_heap());
    uint32_t count = templates_.num_;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key;
        RiderIdentity* profile = templates_.get_by_index(static_cast<int>(i), &key);
        if (profile->is_pro())
            profile->read_from_save(profile->fetch_title(), save);
    }
    uint32_t current = 0;
    save->read_hash_value(kCurrentSkater, &current, 0);
    set_current_skater(current);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x0057BB40
bool ProfileRoster::add_template(script::Struct* params)
{
    memory::Hub::get_pull()->enter_heap_scope(profile_heap());
    uint32_t name;
    params->read_hash_value(kName, &name, 1);
    RiderIdentity* profile = new_profile();
    profile->get_reset(params);
    templates_.place_in(name, profile);
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x0057BD70
bool ProfileRoster::read_custom_skater(script::Struct* save, bool keep_saved_stats)
{
    set_current_skater(kCustom);
    RiderIdentity* profile = profiles_[cursor_pos_];
    script::Struct* custom = nullptr;
    save->read_sub_struct(kCustom, &custom, 0);
    int version = 0;
    if (!custom->fetch_int_value(kVersionNumber, &version, 0))
        return false;
    if (version != 2)
        return true;
    // The appearance, and the face texture if the save has one.
    script::Struct* appearance = nullptr;
    custom->read_sub_struct(kAppearance, &appearance, 0);
    profile->set_appearance(appearance, true);
    if (void* face = face_of(profile)) {
        script::Struct* texture;
        bool found = custom->read_sub_struct(kFaceTexture, &texture, 0);
        if (found) {
            static_cast<gfx::FaceSkin*>(face)->load_from_structure(texture);
            debug::printf("Face texture found\n");
        } else {
            debug::printf("Face texture not found\n");
        }
        static_cast<gfx::FaceSkin*>(face)->mark_usable(found);
    }
    // The stats and specials as they are, the save's info read over them, then (unless the save's stats are kept)
    // put back.
    int stats[10];
    for (int i = 0; i < 10; ++i)
        stats[i] = profile->fetch_tally(kStats[i]);
    int max_specials = profile->get_max_specials();
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* specials = memory ? memory->prepare_all() : nullptr;
    specials->append_structure_if_any(profile->get_specials());
    script::Struct* info = nullptr;
    custom->read_sub_struct(kInfo, &info, 0);
    profile->details_params()->get_clear();
    profile->details_params()->append_structure_if_any(info);
    if (!keep_saved_stats) {
        for (int i = 0; i < 10; ++i)
            profile->set_ability_level(kStats[i], stats[i]);
        profile->set_ability_level(kMaxSpecials, max_specials);
        profile->get_specials()->get_clear();
        profile->get_specials()->append_structure_if_any(specials);
    }
    if (specials) {
        specials->get_clear();
        script::delete_struct(specials);
    }
    int deck_flags = 1; // (read, never used)
    return custom->fetch_int_value(kDeckFlags, &deck_flags, 0);
}

// 0x0057C0D0
ProfileRoster* ProfileRoster::fabricate(bool create)
{
    if (!sole() && create) {
        void* memory = memory::alloc_zeroed(sizeof(ProfileRoster), nullptr, 1);
        sole() = memory ? static_cast<ProfileRoster*>(memory)->make_new() : nullptr;
    }
    ++ref_tally();
    return sole();
}

// 0x0057C120
void ProfileRoster::get_release()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (ProfileRoster* manager = sole()) {
        manager->destroy_body();
        memory::Hub::get_pull()->dealloc(manager);
    }
    sole() = nullptr;
    ref_tally() = 0;
}

} // namespace objects
