// Script functions (commands) for the save files on the memory card (see save_files_cfuncs.h).

#include "game/save_cards/save_files_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/stubs.h"
#include "game/goals/goal_manager.h"
#include "game/save_cards/memcard.h"
#include "game/save_cards/memcard_cfuncs.h"
#include "game/save_cards/save_contents.h"
#include "game/save_cards/save_types.h"
#include "engine/script/struct_buffer.h"
#include "engine/audio/music_player.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/game_config.h"

namespace save_cards {

namespace {

using script::Script;
using script::Struct;

int os_layer() { return game_config::g_platform; }
SaveCard* the_card() { return g_the_card->get_storage_slot(0, 0); }

// The calling script's own parameters (Scripts::CScript +0x14): where commands leave results for it.
Struct* result_params(Script* script) { return *reinterpret_cast<Struct**>(reinterpret_cast<char*>(script) + 0x14); }

// The console builds' save folder (0x005B5F10, no source yet).
const char* save_folder() { return game_config::get_path_prefix(); }

// The card's delete of a console file or folder entry: on the PC the shared always-true method (0x005BC7F0).
bool delete_card_file(SaveCard* card, const char* name) { return card->always_true(reinterpret_cast<int>(name)); }

// A save file's handle deleted (its vtable's scalar deleting destructor).
void DeleteFile(FileLoad* file)
{
    reinterpret_cast<void(__thiscall*)(FileLoad*, int)>(file->list_link_.vtable[0])(file, 1);
}

// The pool pairs the temporary pools belong to: components, structures, vectors.
memory::PoolPair& component_pools() { return script::g_component_pools; }
memory::PoolPair& struct_pools() { return script::g_struct_pools; }
memory::PoolPair& vector_pools() { return script::g_vector_pools; }

// A pair's second pool made (as the pair's own creator, 0x0046F110 / 0x0046F190 / 0x0046F210, does).
void create_reserve_pool(memory::PoolPair& pools, int item_size, int count, const char* name)
{
    ++pools.current;
    script::create_pool(pools, item_size, count, name);
    --pools.current;
}
// A pair's second pool deleted: its block and itself, if the pair made it.
void delete_reserve_pool(memory::PoolPair& pools)
{
    ++pools.current;
    if (memory::SlabBank* pool = pools.pools[pools.current]) {
        if (reinterpret_cast<uint8_t*>(&pools.unknown4)[pools.current]) {
            memory::Hub::get_pull()->dealloc(pool->outset_); // (its destructor body, 0x005BA4E0)
            memory::Hub::get_pull()->dealloc(pool);
        }
        pools.pools[pools.current] = nullptr;
    }
    --pools.current;
}

} // namespace

// 0x005A6EA0
bool delete_mem_card_file(Struct* params, Script*)
{
    if (os_layer() != 4) {
        audio::pause_music(1);
        audio::freeze_stream(1);
    }
    SaveCard* card = the_card();
    if (!card) {
        if (os_layer() != 4) {
            audio::pause_music(-1);
            audio::freeze_stream(-1);
        }
        return false;
    }
    bool deleted = true;
    const char* xbox_directory = nullptr; // (read, not used on the PC)
    params->fetch_text("XBoxDirectoryName", &xbox_directory, 0);
    const char* card_file_name = nullptr;
    params->fetch_text("CardFileName", &card_file_name, 0);
    uint32_t type = 0;
    params->read_hash_value("Type", &type, 0);
    const char* user_file_name = ""; // ""
    params->fetch_text("UserFileName", &user_file_name, 0);

    bool removed = false;
    char file[20];
    switch (os_layer()) {
    case 1:
    case 2:
    case 3: {
        // the file in its folder: /<folder><file>/<folder><file>, the type from its last letter
        char full_name[104];
        const char* path = card_file_name;
        if (!path) {
            const char* folder = save_folder();
            make_file_name(user_file_name, file, type);
            std::snprintf(full_name, sizeof(full_name), "/%s%s/%s%s", folder, file, folder, file);
            path = full_name;
        }
        uint32_t file_type = type_from_letter(path[std::strlen(path) - 1]);
        // the folder: the first character, then up to the next '/'
        char directory[100];
        directory[0] = path[0];
        int length = 1;
        for (const char* p = path + 1; *p && *p != '/'; ++p)
            directory[length++] = *p;
        directory[length] = '\0';
        if (!delete_card_file(card, path))
            deleted = false;
        char icon[104];
        std::snprintf(icon, sizeof(icon), "%s/icon.sys", directory);
        if (!delete_card_file(card, icon))
            deleted = false;
        const char* icon_file = nullptr;
        switch (file_type) {
        case kSaveCreatedGoals:
            icon_file = "%s/goals.ico";
            break;
        case kSaveCat:
            icon_file = "%s/tricks.ico";
            break;
        case kSavePark:
            icon_file = "%s/parked.ico";
            break;
        case kSaveCreatedGraphic:
            icon_file = "%s/grafitti.ico";
            break;
        case kSaveNetworkSettings:
            icon_file = "%s/network.ico";
            break;
        case kSaveOptionsAndPros:
            icon_file = "%s/thps5.ico";
            break;
        default:
            break; // (icon.sys a second time)
        }
        if (icon_file)
            std::snprintf(icon, sizeof(icon), icon_file, directory);
        if (!delete_card_file(card, icon))
            deleted = false;
        removed = card->remove_file(directory);
        break;
    }
    case 4:
    case 7:
        if (!card_file_name) {
            deleted = false;
            break;
        }
        removed = card->remove_file(card_file_name);
        break;
    case 5: {
        if (card_file_name) {
            removed = delete_card_file(card, card_file_name);
            break;
        }
        const char* folder = save_folder();
        make_file_name(user_file_name, file, type);
        char full_name[104];
        std::snprintf(full_name, sizeof(full_name), "%s%s%s%s", folder, file, folder, file);
        removed = delete_card_file(card, full_name);
        break;
    }
    default:
        deleted = false;
        break;
    }
    if (deleted && !removed)
        deleted = false;
    if (os_layer() != 4) {
        audio::pause_music(-1);
        audio::freeze_stream(-1);
    }
    return deleted;
}

// 0x005A8500
bool get_mem_card_space_required(Struct* params, Script* script)
{
    uint32_t type = 0;
    params->read_hash_value(0u, &type, 0);
    int size = type_file_size(type);
    result_params(script)->put_int(0xEB3860C3u, save_blocks(type, size)); // SpaceRequired
    return true;
}

// 0x005A8550
bool mem_card_file_exists(Struct* params, Script* script)
{
    SaveCard* card = the_card();
    if (!card)
        return false;
    const char* name = nullptr;
    params->fetch_text("Name", &name, 0);
    uint32_t type = 0;
    params->read_hash_value("Type", &type, 0);
    if (!name || !type)
        return false;
    const char* type_file_name = nullptr; // (only made on the PC and platform 7)
    char path[100];
    path[0] = '\0';
    switch (os_layer()) {
    case 4:
    case 7: {
        type_file_name = get_type_file_name(type, name);
        const char* converted = card->convert_filename(type_file_name);
        if (!converted)
            return false;
        std::snprintf(path, sizeof(path), "%s", converted);
        break;
    }
    case 1:
    case 2:
    case 3:
    case 5: {
        const char* folder = save_folder();
        char file[20];
        make_file_name(name, file, type);
        std::snprintf(path, sizeof(path), "/%s%s/%s%s", folder, file, folder, file);
        break;
    }
    default:
        break;
    }
    FileLoad* file = card->unseal(path, 1, 0);
    if (!file) {
        // (on the PC, the file that wouldn't open is removed - by the type's name made above)
        if (os_layer() == 4)
            card->remove_file(type_file_name);
        return false;
    }
    int size = static_cast<int>(file->seek_to(0, 2));
    result_params(script)->put_int("total_file_size", save_blocks(type, size));
    file->drain();
    file->shut();
    DeleteFile(file);
    return true;
}

// 0x005A8BB0
bool clear_mem_card_data_for_upload(Struct*, Script*)
{
    Struct*& kept = g_kept_params;
    if (kept) {
        kept->get_clear();
        script::delete_struct(kept);
        kept = nullptr;
    }
    g_kept_type = 0;
    return true;
}

// 0x005A8DA0
bool create_temporary_mem_card_pools(Struct*, Script*)
{
    if (component_pools().pools[component_pools().current + 1])
        return true;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    memory::push_mem_profile("Mem card CComponent");
    create_reserve_pool(component_pools(), 0x10, 50000, "Reserve CComponent");
    memory::pop_mem_profile();
    memory::push_mem_profile("Mem card CStruct");
    create_reserve_pool(struct_pools(), 0x08, 50000, "Reserve CStruct");
    memory::pop_mem_profile();
    memory::push_mem_profile("Mem card CVector");
    create_reserve_pool(vector_pools(), 0x10, 10000, "Reserve CVector");
    memory::pop_mem_profile();
    memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x005A9700
bool remove_temporary_mem_card_pools(Struct*, Script*)
{
    delete_reserve_pool(component_pools());
    delete_reserve_pool(struct_pools());
    delete_reserve_pool(vector_pools());
    return true;
}

// 0x005A8BF0
bool get_save_info(Struct* params, Script* script)
{
    uint32_t type = 0;
    params->read_hash_value(0x7321A8D6u, &type, 0); // type
    Struct* contents = script::spawn_param_bag();
    if (contents)
        contents = contents->prepare_all();
    write_save_contents(contents, type);
    int main_size = static_cast<int>(script::measure_buffer_bytes(contents, 100000));
    Struct* summary = script::spawn_param_bag();
    if (summary)
        summary = summary->prepare_all();
    write_summary(contents, summary, type);
    int summary_size = static_cast<int>(script::measure_buffer_bytes(summary, 100000));
    Struct* results = result_params(script);
    results->put_int(0x172C1344u, main_size);                       // MainStructureSize
    results->put_int(0x96635475u, summary_size);                    // SummaryInfoSize
    results->put_int(0x6AFD2F7Fu, 100);                             // MaxSummaryInfoSize
    results->put_int(0xECBB8262u, main_size + summary_size + 0x14); // TotalSize
    results->put_int(0xB35EB1D1u, type_file_size(type));            // MaxTotalSize
    results->append_struct_ref(0x703E60CAu, summary);               // SummaryInfo
    results->append_struct_ref(0x671C9C00u, contents);              // MainStructure
    return true;
}

// --- the directory listing and saving ----------------------------------------------------------------------

namespace {

constexpr int kMaxFiles = 75;
constexpr uint32_t kCorrupt = 0x492676A6;  // "corrupt"
constexpr uint32_t kFileName = 0xC3F4169A; // "FileName"

// A save file's header (0x14 bytes, then the summary and the contents).
struct SaveFileHeader {
    uint32_t checksum;     // of the whole file, this field 0
    uint32_t summary_crc_; // of the summary
    int summary_size_;
    int size; // the header, the summary and the contents
    int version;
};
static_assert(sizeof(SaveFileHeader) == 0x14, "the summary follows at +0x14");

// The first 0x78 bytes of a listed file are read here (0x007CEC58): the header and the start of the summary.
struct ListedFile {
    SaveFileHeader header;
    uint8_t summary[0x78 - sizeof(SaveFileHeader)]; // (0x007CEC6C)
};
ListedFile g_listed_file;
SaveFileHeader& listed_header() { return g_listed_file.header; }
const uint8_t* listed_summary() { return g_listed_file.summary; }

// The i-th file of a list (its nodes from the head's next; each file is its own node).
FileLoad* list_entry(memory::ListNode* head, int index)
{
    memory::ListNode* node = head->next;
    for (int i = 0; i < index; ++i)
        node = node->next;
    return static_cast<FileLoad*>(node->data);
}

// A save file's type from its name: on the consoles its letter after the folder, on the PC its extension.
uint32_t listed_file_type(const FileLoad* file, int folder_length)
{
    switch (os_layer()) {
    case 1:
    case 2:
    case 3:
    case 5:
        return type_from_letter(file->label_[folder_length + 7]);
    case 4:
    case 7:
    case 8: {
        // (the music and sounds kept going while the files are read)
        audio::tick_frame();
        sound::update_voice_states();
        const char* extension = file->label_ + std::strlen(file->label_) - 4;
        if (_stricmp(extension, ".NWS") == 0)
            return kSaveNetworkSettings;
        if (_stricmp(extension, ".SKA") == 0)
            return kSaveOptionsAndPros;
        if (_stricmp(extension, ".PRK") == 0)
            return kSavePark;
        if (_stricmp(extension, ".CAS") == 0)
            return kSaveCas;
        if (_stricmp(extension, ".CAT") == 0)
            return kSaveCat;
        if (_stricmp(extension, ".GOA") == 0)
            return kSaveCreatedGoals;
        if (_stricmp(extension, ".GRF") == 0)
            return kSaveCreatedGraphic;
        return 0;
    }
    default:
        return 0;
    }
}

// The entry marked corrupt, with the damaged-file text as its FileName.
void mark_corrupt(Struct* entry, uint32_t text)
{
    entry->put_name_key(0u, kCorrupt);
    entry->append_word(kFileName, script::fetch_text(text));
}

} // namespace

// 0x005A8F70
bool get_mem_card_directory_listing(Struct* params, Script* script)
{
    constexpr uint32_t kDirectoryListing = 0xD8580FB5, kFilesLimitReached = 0x4EEC27B5, kTotalFiles = 0x56B7A7F6;
    result_params(script)->erase_module(kDirectoryListing);
    result_params(script)->erase_module(kFilesLimitReached);
    result_params(script)->erase_module(kTotalFiles);
    uint32_t wanted_type = 0;
    params->read_hash_value(0x11093FB5u, &wanted_type, 0); // FileType
    SaveCard* card = the_card();
    if (!card)
        return true;

    // the card's files (a lists::Foremost<File> on the stack, vtable 0x00658A60)
    memory::ListNode head;
    head.vtable = save_cards::kFileVtable;
    head.data = reinterpret_cast<void*>(-1);
    head.priority = 0;
    head.next = &head;
    head.prev = &head;
    card->list_save_files("*", reinterpret_cast<FileLoad*>(&head));

    // into the temporary component and structure pools, if they exist
    bool switched = false;
    if (component_pools().pools[component_pools().current + 1]) {
        ++component_pools().current;
        ++struct_pools().current;
        switched = true;
    }
    int listed = 0;
    Struct* entries[kMaxFiles];
    const char* folder = save_folder();
    int folder_length = static_cast<int>(std::strlen(folder));
    int file_count = 0;
    for (memory::ListNode* node = head.next; reinterpret_cast<intptr_t>(node->data) != -1 && node; node = node->next)
        ++file_count;
    debug::printf(
        "ScriptGetMemCardDirectoryListing: DirList Start\n"); // "ScriptGetMemCardDirectoryListing: DirList Start\n"
    int game_files = 0;
    for (int i = 0; i < file_count; ++i) {
        FileLoad* file = list_entry(&head, i);
        if (std::memcmp(file->label_, folder, static_cast<size_t>(folder_length)) != 0)
            continue;
        ++game_files;
        uint32_t type = listed_file_type(file, folder_length);
        if (wanted_type && type != wanted_type)
            continue;
        if (listed >= kMaxFiles)
            continue;
        Struct* entry = script::spawn_param_bag();
        if (entry)
            entry = entry->prepare_all();
        entry->put_name_key(0xFADDC0B8u, type);     // file_type
        entry->put_int(0x7F8C98FEu, listed);        // index
        entry->put_int(0x447D8CC8u, file->year_);   // year
        entry->put_int(0x7149EFF9u, file->month_);  // month
        entry->put_int(0x1A5FD66Fu, file->day_);    // day
        entry->put_int(0x8FE1EEB1u, file->hour_);   // hour
        entry->put_int(0x5F94B55Cu, file->minute_); // minutes
        entry->put_int(0xD029F619u, file->second_); // seconds
        char path[100];
        if (os_layer() == 5)
            std::snprintf(path, sizeof(path), "%s", file->label_);
        else
            std::snprintf(path, sizeof(path), "Save/%s", file->label_);
        entry->append_word(0x13C68F74u, path); // actual_file_name
        constexpr uint32_t kDamagedFile = 0x2C7CD632, kNgcDamagedFile = 0xFD4AA64E;
        FileLoad* opened = card->unseal(path, 1, 0);
        if (!opened) {
            mark_corrupt(entry, kDamagedFile);
        } else {
            opened->seek_to(0, 0);
            opened->get_take_in(&g_listed_file, 0x78);
            int summary_size = listed_header().summary_size_;
            bool bad = summary_size > 100 || checksums::generate_crc_from_buffer(listed_summary(), summary_size) !=
                                                 listed_header().summary_crc_;
            int size = static_cast<int>(opened->seek_to(0, 2));
            entry->put_int(0x3D1BCFEDu, save_blocks(type, size)); // total_file_size
            if (bad || listed_header().version != type_version(type)) {
                mark_corrupt(entry, os_layer() == 5 ? kNgcDamagedFile : kDamagedFile);
            } else {
                Struct* summary = script::spawn_param_bag();
                if (summary)
                    summary = summary->prepare_all();
                script::decode_from_bytes(summary, listed_summary());
                entry->append_structure_if_any(summary);
                if (summary) {
                    summary->get_clear();
                    script::delete_struct(summary);
                }
            }
            opened->shut();
            DeleteFile(opened);
        }
        entries[listed++] = entry;
    }
    if (switched) {
        --component_pools().current;
        --struct_pools().current;
    }
    // the list's nodes deleted (each file is its own node)
    for (memory::ListNode* node = head.next; reinterpret_cast<intptr_t>(node->data) != -1;) {
        memory::ListNode* next = node->next;
        memory::ListNode* following = reinterpret_cast<intptr_t>(next->data) == -1 ? nullptr : next;
        reinterpret_cast<void(__thiscall*)(memory::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
        if (!node)
            break;
    }
    result_params(script)->put_int(kTotalFiles, game_files);
    if (game_files >= kMaxFiles)
        result_params(script)->put_name_key(0u, kFilesLimitReached);
    if (listed) {
        // newest first (a bubble sort by the dates)
        bool swapped;
        do {
            swapped = false;
            for (int j = 0; j < listed - 1; ++j) {
                if (is_later(entries[j + 1], entries[j])) {
                    Struct* later = entries[j + 1];
                    entries[j + 1] = entries[j];
                    entries[j] = later;
                    swapped = true;
                }
            }
        } while (swapped);
        script::Array* listing = script::alloc_script_array();
        if (listing)
            listing = listing->prepare_all();
        listing->configure_size_kind(static_cast<uint32_t>(listed), script::kQbvalStruct);
        for (int j = 0; j < listed; ++j)
            listing->set_element(static_cast<uint32_t>(j), reinterpret_cast<uint32_t>(entries[j]));
        result_params(script)->append_array_ref(kDirectoryListing, listing);
    }
    // the head unlinked (its destructor's body)
    head.prev->next = head.next;
    head.next->prev = head.prev;
    return true;
}

// 0x005A7420: emit_into_buffer's callback while a save is written on the PC: the music and sounds kept going.
void __cdecl keep_sound_going()
{
    audio::tick_frame();
    sound::update_voice_states();
}

// 0x005A97E0
bool save_to_memory_card(Struct* params, Script*)
{
    if (os_layer() == 5)
        create_temporary_mem_card_pools(nullptr, nullptr);
    if (os_layer() != 4) {
        audio::pause_music(1);
        audio::freeze_stream(1);
    }
    bool& out_of_space = g_out_of_space;
    out_of_space = false;
    const char* name = ""; // ""
    params->fetch_text("Name", &name, 0);
    uint32_t type = 0;
    params->read_hash_value("Type", &type, 0);
    int vault_data = 0;
    params->fetch_int_value(0xBB609E73u, &vault_data, 0); // savevaultdata
    if (vault_data) {
        type = g_kept_type;
    } else if (type == kSaveOptionsAndPros) {
        // the goal manager's totals brought up to date (0x0055C090, no source yet)
        goals::get_objective_board()->level_unload();
    }
    // into the temporary pools, if they exist
    bool switched = false;
    if (component_pools().pools[component_pools().current + 1]) {
        ++component_pools().current;
        ++struct_pools().current;
        ++vector_pools().current;
        switched = true;
    }
    Struct* contents = script::spawn_param_bag();
    if (contents)
        contents = contents->prepare_all();
    if (vault_data)
        contents->append_structure_if_any(g_kept_params);
    else
        write_save_contents(contents, type);
    Struct* summary = script::spawn_param_bag();
    if (summary)
        summary = summary->prepare_all();
    write_summary(contents, summary, type);
    summary->append_word("filename", name);

    FileLoad* file = nullptr;
    uint8_t* header = nullptr;
    uint8_t* padding = nullptr;
    bool saved = false;
    int file_size = type_file_size(type);
    uint32_t main_size = script::measure_buffer_bytes(contents, static_cast<uint32_t>(file_size));
    uint32_t summary_size = script::measure_buffer_bytes(summary, 100);
    uint32_t total = main_size + summary_size + sizeof(SaveFileHeader);
    uint32_t padding_size = static_cast<uint32_t>(file_size) - total;
    if (padding_size != 0 && os_layer() != 5) {
        padding = static_cast<uint8_t*>(memory::alloc_block(padding_size));
        if (padding_size > 0)
            std::memset(padding, 0x69, padding_size);
    }
    SaveCard* card = the_card();
    // (the second test is a card method that's the shared always-true stub, 0x005B9760)
    if (card && stubs::return_true()) {
        char path[104];
        bool named = true;
        switch (os_layer()) {
        case 5: {
            char folder_name[104];
            make_folder_file_name(type, name, folder_name);
            std::snprintf(path, sizeof(path), "%s%s", folder_name, folder_name);
            break;
        }
        case 4:
        case 7: {
            path[0] = '\0';
            const char* type_name = get_type_file_name(type, name);
            char* out = path;
            while ((*out++ = *type_name++) != '\0') {
            }
            break;
        }
        default:
            named = false;
            break;
        }
        if (named) {
            file = card->unseal(path, 6, file_size);
            out_of_space = card->fault_ == 2;
        }
        if (file) {
            header = static_cast<uint8_t*>(memory::alloc_block(total));
            std::memset(header, 0, sizeof(SaveFileHeader));
            if (os_layer() != 5) {
                auto* file_header = reinterpret_cast<SaveFileHeader*>(header);
                file_header->version = type_version(type);
                file_header->size = static_cast<int>(total);
                file_header->summary_size_ = static_cast<int>(summary_size);
                uint8_t* data = header + sizeof(SaveFileHeader);
                bool pc = os_layer() == 4 || os_layer() == 7;
                script::emit_into_buffer(summary, data, summary_size, 0, pc ? keep_sound_going : nullptr);
                file_header->summary_crc_ = checksums::generate_crc_from_buffer(data, static_cast<int>(summary_size));
                data += summary_size;
                script::emit_into_buffer(contents, data, main_size, 0, nullptr);
                if (type == kSaveNetworkSettings) {
                    for (uint32_t i = 0; i < main_size; ++i)
                        data[i] += 0x69; // (read back with 0x69 taken off)
                }
                file_header->checksum = checksums::generate_crc_from_buffer(header, static_cast<int>(total));
                int written = file->emit_out(header, static_cast<int>(total));
                out_of_space = card->fault_ == 2;
                if (written == static_cast<int>(total)) {
                    if (os_layer() == 5 || padding_size == 0) {
                        saved = true;
                    } else {
                        written = file->emit_out(padding, static_cast<int>(padding_size));
                        out_of_space = card->fault_ == 2;
                        saved = written == static_cast<int>(padding_size);
                    }
                }
            }
        }
    }
    if (padding)
        memory::free_malloc(padding);
    if (header)
        memory::free_malloc(header);
    if (file) {
        if (!file->drain())
            saved = false;
        if (!file->shut())
            saved = false;
        DeleteFile(file);
    }
    if (summary) {
        summary->get_clear();
        script::delete_struct(summary);
    }
    if (contents) {
        contents->get_clear();
        script::delete_struct(contents);
    }
    if (switched) {
        --component_pools().current;
        --struct_pools().current;
        --vector_pools().current;
    }
    if (os_layer() != 4) {
        audio::pause_music(-1);
        audio::freeze_stream(-1);
    }
    if (os_layer() == 5)
        remove_temporary_mem_card_pools(nullptr, nullptr);
    return saved;
}

} // namespace save_cards
