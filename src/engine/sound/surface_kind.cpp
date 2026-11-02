// The terrain manager: terrain types and terrain sounds (see surface_kind.h).

#include "engine/sound/surface_kind.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "engine/lists/key_map.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/startup_init.h"
#include "formats/byte_stream.h"
#include "wire/play_messages.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/net/net_game_state.h"
#include <gsl/pointers>
#include <string_view>

namespace surface_kind {

using script::Array;
using script::Struct;

namespace {

constexpr uint32_t kTerrainTypes = 0x533AD720;           // "Terrain_Types"
constexpr uint32_t kTerrainActions = 0x828087B8;         // "Terrain_Actions"
constexpr uint32_t kChecksum = 0x21902065;               // "checksum"
constexpr uint32_t kType = 0x7321A8D6;                   // "type"
constexpr uint32_t kAction = 0xB833736D;                 // "action"
constexpr uint32_t kTerrainDefault = 0x64967688;         // "TERRAIN_DEFAULT"
constexpr uint32_t kStandardTerrainDefault = 0x5B80CFCB; // "STANDARD_TERRAIN_DEFAULT"
constexpr uint32_t kSoundActions = 0x5C969C50;           // "SoundActions"
constexpr uint32_t kSoundAction = 0x25F654C8;            // "soundAction"
constexpr uint32_t kChance = 0xDBC4C4DB;                 // "chance"
constexpr uint32_t kUseUpTo = 0x9EE9A09A;                // "useUpTo"
constexpr uint32_t kSound = 0x07713C7B;                  // "sound"
constexpr uint32_t kMaxPitch = 0xFA3E14C5;               // "maxPitch"
constexpr uint32_t kMinPitch = 0x1C5EBB24;               // "minPitch"
constexpr uint32_t kMaxVol = 0x0693DAAF;                 // "maxVol"
constexpr uint32_t kMinVol = 0x4391992D;                 // "floor_loudness_"
constexpr uint32_t kInitialPitchKey = 0xF573682F;        // "load_pitch_"
constexpr uint32_t kLoadVol = 0x312CCBCF;                // "loadVol"

constexpr float kHundred = 100.0f;
constexpr uint32_t kHundredBits = 0x42C80000;
// 0x0064544C: a percentage's factor (0.01 as a float)
constexpr float kPercent = 0.0099999998f;

// The two tables' items.
struct TypeItem {
    uint8_t index; // +0 the terrain's index
    std::array<uint8_t, 3> pad;
    uint32_t checksum; // +4 its checksum (the item's key points here)
};
struct ActionItem {
    uint8_t index; // +0 the table's index (its key)
    std::array<uint8_t, 3> pad;
    uint32_t type;   // +4 TABLE_*
    uint32_t action; // +8 TA_*
};
static_assert(sizeof(TypeItem) == 8 && sizeof(ActionItem) == 12, "layout must match the original");

// NOT ORIGINAL grouping - the terrain types' and actions' tables (built by the start-up initialisers). Made on first
// use, never destroyed (terrain_tables()).
struct TerrainTables {
    lists::KeyMap terrain_types_{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
    lists::KeyMap terrain_actions_{lists::KeyMap::Unconstructed{}}; // (KeyMap(8) at start-up)
};

TerrainTables& terrain_tables()
{
    static core::NoDestroy<TerrainTables> state;
    return state.get();
}
lists::KeyMap& terrain_types() { return terrain_tables().terrain_types_; }
lists::KeyMap& get_terrain_actions() { return terrain_tables().terrain_actions_; }

sound::AudioMixer* sound_manager() { return sound::get_audio_mixer(); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// `percent` of the range min..max (a range of 0 to 100 is the percentage itself).
float scaled_percent(float percent, float max, float min)
{
    if (min == 0.0f && max == kHundred)
        return percent;
    return static_cast<float>((static_cast<double>(max) - min) * percent * kPercent + min);
}

// The soundAction's sound loaded: permanent (load type 1), its load pitch and volume.
void ingest_sound(const SoundInfo& info)
{
    sound_manager()->ingest_sound(info.name.data(), 0, 1, 0, 0.0f, info.load_pitch_, info.load_vol_);
}

} // namespace

// 0x004442C0
uint8_t get_terrain_index(uint32_t checksum)
{
    // (an unknown terrain: the original reads its index through null)
    auto* item = static_cast<volatile TypeItem*>(terrain_types().fetch_entry(checksum));
    return item->index;
}

// 0x00444310
uint32_t get_table_action(uint8_t table)
{
    // (an unknown table: the original reads the action through null)
    auto* item = static_cast<volatile ActionItem*>(get_terrain_actions().fetch_entry(table));
    return item->action;
}

// 0x00444360 (the walk stops at an item without a value - as the original)
uint32_t fetch_surface_key(uint8_t terrain)
{
    lists::KeyMap& types = terrain_types();
    types.set_iter_bucket(-1);
    types.set_iter_item(nullptr);
    while (types.step_iterator() && types.iter_item() && types.iter_item()->value) {
        auto* item = static_cast<TypeItem*>(types.iter_item()->value);
        if (item->index == terrain)
            return item->checksum;
    }
    return 0;
}

// 0x004443B0
uint8_t get_table_index(uint32_t type)
{
    lists::KeyMap& actions = get_terrain_actions();
    actions.set_iter_bucket(-1);
    actions.set_iter_item(nullptr);
    while (actions.step_iterator() && actions.iter_item() && actions.iter_item()->value) {
        auto* item = static_cast<ActionItem*>(actions.iter_item()->value);
        if (item->type == type)
            return item->index;
    }
    return 0;
}

// 0x00444400
bool get_terrain_structure(uint8_t terrain, Struct** out, uint32_t name)
{
    Struct* settings = script::read_sub_struct(fetch_surface_key(terrain));
    if (settings && settings->read_sub_struct(name, out, 0))
        return true;
    Struct* standard = script::read_sub_struct(kStandardTerrainDefault);
    return standard && standard->read_sub_struct(name, out, 0);
}

// 0x00444460
void clear_terrain_types()
{
    terrain_types().empty_table();
    get_terrain_actions().empty_table();
}

// 0x00444480
bool get_terrain_structure_into(Struct** out, uint8_t terrain, uint32_t name)
{
    return get_terrain_structure(terrain, out, name);
}

// 0x004444A0
void load_terrain_types()
{
    Array* types = script::fetch_list(kTerrainTypes);
    for (uint32_t i = 0; i < types->extent(); ++i) {
        auto* item = static_cast<TypeItem*>(memory::alloc(sizeof(TypeItem)));
        item->index = static_cast<uint8_t>(i);
        types->struct_at(i)->read_hash_value(kChecksum, &item->checksum, 0);
        terrain_types().store_entry(item->checksum, item);
    }
    Array* actions = script::fetch_list(kTerrainActions);
    for (uint32_t i = 0; i < actions->extent(); ++i) {
        auto* item = static_cast<ActionItem*>(memory::alloc(sizeof(ActionItem)));
        item->index = static_cast<uint8_t>(i);
        actions->struct_at(i)->read_hash_value(kType, &item->type, 1);
        actions->struct_at(i)->read_hash_value(kAction, &item->action, 1);
        get_terrain_actions().store_entry(i, item);
    }
}

// 0x004445B0
bool get_sound_info(Struct* sound_action, SoundInfo* info)
{
    if (sound::no_sound())
        return false;
    info->min_pitch_ = kHundred;
    info->max_vol_ = kHundred;
    info->floor_loudness_ = 0.0f;
    info->load_pitch_ = kHundred;
    info->load_vol_ = kHundred;
    core::at(info->name, 0) = '\0';
    info->checksum = 0;
    info->max_pitch_ = kHundred;
    const char* sound = "";
    if (!sound_action->fetch_text(kSound, &sound, 0))
        return false;
    // (copied unchecked, as the original)
    int length = 0;
    for (const char c : std::string_view(sound))
        core::at(info->name, length++) = c;
    core::at(info->name, length) = '\0';
    // the checksum of the name without its folders
    const char* base = info->name.data();
    for (int i = 0; i < length; ++i) {
        if (core::at(info->name, i) == '\\' || core::at(info->name, i) == '/')
            base = std::next(info->name.data(), i + 1);
    }
    info->checksum = checksums::hash_from_text(base);
    sound_action->fetch_float(kMaxPitch, &info->max_pitch_, 0);
    sound_action->fetch_float(kMinPitch, &info->min_pitch_, 0);
    sound_action->fetch_float(kMaxVol, &info->max_vol_, 0);
    sound_action->fetch_float(kMinVol, &info->floor_loudness_, 0);
    sound_action->fetch_float(kInitialPitchKey, &info->load_pitch_, 0);
    sound_action->fetch_float(kLoadVol, &info->load_vol_, 0);
    return true;
}

// 0x00444710 (maxVol is compared with 100 by its bits)
double get_scaled_volume(const SoundInfo* info, float percent, bool clamp)
{
    double volume = 0.0;
    uint32_t max_bits = 0;
    std::memcpy(&max_bits, &info->max_vol_, 4);
    if (info->floor_loudness_ == 0.0f && max_bits == kHundredBits)
        volume = percent;
    else
        volume =
            (static_cast<double>(info->max_vol_) - info->floor_loudness_) * percent * kPercent + info->floor_loudness_;
    if (clamp && volume > info->max_vol_)
        volume = info->max_vol_;
    return volume;
}

// 0x00444760
Struct* pick_sound_action(Struct* entry)
{
    Struct* sound_action = nullptr;
    if (entry->read_sub_struct(kSoundAction, &sound_action, 0))
        return sound_action;
    Array* choices = nullptr;
    entry->fetch_list(kSoundAction, &choices, 1);
    int chance =
        0; // (kept from one element to the next when one has none; uninitialised for the first in the original)
    int total = 0;
    for (uint32_t i = choices->extent(); i != 0;) {
        --i;
        sound_action = choices->struct_at(i);
        sound_action->fetch_int_value(kChance, &chance, 1);
        total += chance;
    }
    int pick = maths::rnd(total);
    for (uint32_t i = choices->extent(); i != 0;) {
        --i;
        sound_action = choices->struct_at(i);
        sound_action->fetch_int_value(kChance, &chance, 1);
        pick -= chance;
        if (pick < 0)
            return sound_action;
    }
    return nullptr;
}

// 0x00444830
Struct* find_sound_action(Struct* sound_actions, uint8_t table, float value)
{
    const uint32_t action = get_table_action(table);
    Struct* entry = nullptr;
    if (sound_actions->read_sub_struct(action, &entry, 0))
        return entry;
    Array* entries = nullptr;
    sound_actions->fetch_list(action, &entries, 1);
    for (uint32_t i = 0; i < entries->extent(); ++i) {
        entry = entries->struct_at(i);
        float use_up_to = 0.0f;
        // (a NaN value never fits)
        if (!entry->fetch_float(kUseUpTo, &use_up_to, 0) || value <= use_up_to)
            return pick_sound_action(entry);
    }
    return nullptr;
}

// 0x004448E0 (with sound off the info isn't filled and the load gets what's there - as the original)
void load_sound_action(Struct* sound_action)
{
    SoundInfo info{};
    get_sound_info(sound_action, &info);
    ingest_sound(info);
}

// 0x00444950 (TERRAIN_DEFAULT's own structure for the action counts only if it names the action again inside)
void load_table_sounds(Struct* sound_actions, uint8_t table)
{
    const uint32_t action = get_table_action(table);
    Struct* default_actions = nullptr;
    script::read_sub_struct(kTerrainDefault)->read_sub_struct(kSoundActions, &default_actions, 1);
    Struct* entry = nullptr;
    if (!sound_actions->read_sub_struct(action, &entry, 0)) {
        default_actions->read_sub_struct(action, &entry, 0);
        if (entry && !entry->has_entry_named(action))
            entry = nullptr;
    }
    if (entry) {
        load_sound_action(entry);
        return;
    }
    Array* entries = nullptr;
    if (!sound_actions->fetch_list(action, &entries, 0))
        default_actions->fetch_list(action, &entries, 1);
    for (uint32_t i = 0; i < entries->extent(); ++i) {
        auto* element = entries->struct_at(i);
        Struct* sound_action = nullptr;
        if (element->read_sub_struct(kSoundAction, &sound_action, 0)) {
            load_sound_action(sound_action);
            continue;
        }
        Array* choices = nullptr;
        element->fetch_list(kSoundAction, &choices, 1);
        for (uint32_t j = 0; j < choices->extent(); ++j) {
            SoundInfo info{};
            get_sound_info(choices->struct_at(j), &info);
            ingest_sound(info);
        }
    }
}

// 0x00444B10
bool get_terrain_sound(SoundInfo* info, uint8_t terrain, uint8_t table, float value)
{
    if (sound::no_sound())
        return false;
    Struct* sound_actions = nullptr;
    if (!get_terrain_structure(terrain, &sound_actions, kSoundActions))
        return false;
    if (!sound_actions->has_entry_named(get_table_action(table)))
        script::read_sub_struct(kTerrainDefault)->read_sub_struct(kSoundActions, &sound_actions, 1);
    Struct* sound_action = find_sound_action(sound_actions, table, value);
    return sound_action && get_sound_info(sound_action, info);
}

// 0x00444BB0
void play_terrain_sound(uint8_t table, uint8_t terrain, const maths::Vec4* position, float loudness_percent,
                        float pitch_percent, float value, bool propagate)
{
    if (sound::no_sound())
        return;
    sound::AudioMixer* sounds = sound_manager();
    void* net = online::net_game_state().online_hub();
    Struct* sound_actions = nullptr;
    if (!get_terrain_structure_into(&sound_actions, terrain, kSoundActions))
        return;
    if (!sound_actions->has_entry_named(get_table_action(table)))
        script::read_sub_struct(kTerrainDefault)->read_sub_struct(kSoundActions, &sound_actions, 1);
    Struct* sound_action = find_sound_action(sound_actions, table, value);
    if (!sound_action)
        return;
    SoundInfo info{};
    if (!get_sound_info(sound_action, &info))
        return;
    if (propagate) {
        net::NetMessageInfo desc;
        auto* client = core::down_cast<net::Guest>(static_cast<online::Hub*>(net)->fetch_patron(0));
        // (the message a propagated terrain sound sends: wire/play_messages.h)
        online::TerrainSound message;
        message.table = table;
        message.terrain = terrain;
        message.x = static_cast<int16_t>(get_truncated(position->x));
        message.y = static_cast<int16_t>(get_truncated(position->y));
        message.z = static_cast<int16_t>(get_truncated(position->z));
        message.volume = static_cast<int8_t>(get_truncated(loudness_percent));
        message.pitch = static_cast<int8_t>(get_truncated(pitch_percent));
        message.value = static_cast<int8_t>(get_truncated(value));
        std::array<uint8_t, online::kTerrainSoundSize> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_terrain_sound(writer, message);
        desc.content_ = data.data();
        desc.byte_count_ = sizeof(data);
        desc.ident_ = online::kMsgTerrainSound;
        client->send_to_host(&desc);
    }
    sound::VolumeRef volume;
    sounds->fetch_loudness(&volume, position, sounds->get_dropoff_distance(info.checksum), 0, nullptr, nullptr);
    volume.percent_tweak(scaled_percent(loudness_percent, info.max_vol_, info.floor_loudness_));
    if (!volume.is_audio_off()) {
        const float pitch = scaled_percent(pitch_percent, info.max_pitch_, info.min_pitch_);
        sounds->emit_sound(info.checksum, &volume, pitch, 0, nullptr, 0); // (the original passed &info: not read)
    }
}

// NOT ORIGINAL: play_terrain_sound's playing, for a sound found elsewhere (THAW's BMX: its own sound tables).
void play_sound_info(const SoundInfo& info, const maths::Vec4* position, float loudness_percent, float pitch_percent)
{
    if (sound::no_sound())
        return;
    sound::AudioMixer* sounds = sound_manager();
    sound::VolumeRef volume;
    sounds->fetch_loudness(&volume, position, sounds->get_dropoff_distance(info.checksum), 0, nullptr, nullptr);
    volume.percent_tweak(scaled_percent(loudness_percent, info.max_vol_, info.floor_loudness_));
    if (!volume.is_audio_off()) {
        const float pitch = scaled_percent(pitch_percent, info.max_pitch_, info.min_pitch_);
        sounds->emit_sound(info.checksum, &volume, pitch, 0, nullptr, 0); // (the original passed &info: not read)
    }
}

// 0x00444EB0
bool load_terrain_sounds(uint32_t checksum)
{
    if (sound::no_sound())
        return false;
    const uint8_t terrain = get_terrain_index(checksum);
    Struct* default_actions = nullptr;
    script::read_sub_struct(kTerrainDefault)->read_sub_struct(kSoundActions, &default_actions, 1);
    Struct* terrain_actions = nullptr;
    if (get_terrain_structure(terrain, &terrain_actions, kSoundActions)) {
        gsl::owner<Struct*> merged = script::spawn_param_bag();
        if (merged)
            new (merged) script::Struct;
        merged->append_structure_if_any(default_actions);
        merged->append_structure_if_any(terrain_actions);
        for (uint32_t table = 0; table < ints::as_unsigned(get_terrain_actions().extent()); ++table)
            load_table_sounds(merged, static_cast<uint8_t>(table));
        if (merged) {
            merged->get_clear();
            script::delete_struct(merged);
        }
    }
    return true;
}

} // namespace surface_kind

namespace surface_kind {

// 0x00640950, 0x00640970 (static initialisers): the terrain tables, 256 buckets each.
void construct_terrain_types() { new (&terrain_tables().terrain_types_) lists::KeyMap(8); }
void construct_terrain_actions() { new (&terrain_tables().terrain_actions_) lists::KeyMap(8); }

} // namespace surface_kind
