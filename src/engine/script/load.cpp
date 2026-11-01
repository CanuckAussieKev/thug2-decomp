// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/script/load.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/token_reader.h"
#include "engine/script/patched.h"
#include "engine/script/precompiled.h"
#include "engine/script/qb.h"
#include "engine/util/lzss.h"
#include "engine/util/byte_stream.h"
#include "engine/sys/game_config.h"

namespace script {

uint32_t g_loading_file;

namespace {

// The checksum of the .qb file being loaded (0 outside loading).
uint32_t& loading_file() { return g_loading_file; }

// Not rewritten yet: the platform's file extension for pre-compiled scripts.
const char* precompiled_extension() { return game_config::fetch_platform_suffix(); }

// The symbol `existing` makes way for a new definition: set aside (temporary loads) or removed.
void get_replace(SymbolTableEntry* existing, int temporary)
{
    if (temporary) {
        replace_symbol_later(existing);
        unlink_symbol(existing);
    } else {
        clean_up_and_remove_symbol(existing);
    }
}

} // namespace

// 0x00472240
const uint8_t* parse_symbol_definition(const uint8_t* token, uint32_t name, const char* filename)
{
    SymbolTableEntry* symbol = create_symbol(name);
    symbol->unknown8 = checksums::hash_from_text(filename);
    switch (*token) {
    case 0x0E:
    case 0x16:
    case 0x17:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1E:
    case 0x1F: {
        Component* value = new_component();
        const uint8_t* end =
            *token == 0x0E ? parse_expression(token, nullptr, value) : parse_value(token, nullptr, value);
        symbol->type = value->kind_tag();
        symbol->raw = value->raw;
        value->raw = 0;
        free_component(value);
        return end;
    }
    case 0x03: {
        symbol->type = kQbvalStruct;
        Struct* structure = spawn_param_bag();
        structure = structure ? new (structure) script::Struct : nullptr;
        symbol->structure = structure;
        return parse_structure(structure, token, nullptr);
    }
    case 0x05: {
        symbol->type = kQbvalArray;
        Array* array = alloc_script_array();
        array = array ? new (array) script::Array : nullptr;
        symbol->raw = reinterpret_cast<uint32_t>(array);
        return parse_array(array, token, nullptr);
    }
    default:
        return token;
    }
}

// 0x0046FE40
SymbolTableEntry* define_script_symbol(uint32_t size, const uint8_t* code, uint32_t name, uint32_t checksum,
                                       const char* filename)
{
    SymbolTableEntry* symbol = create_symbol(name);
    symbol->type = kQbvalScriptBody;
    // (not original: room for LZSS's worst case - the original's 20000 bytes overflowed for a script past ~17.7 KB
    // that doesn't compress)
    uint32_t room = size + size / 8 + 2 > 20000 ? size + size / 8 + 2 : 20000;
    auto* buffer = static_cast<uint8_t*>(memory::alloc_block(room));
    int stored = lzss::pack_up(code, buffer, static_cast<int>(size), false);
    if (stored >= static_cast<int>(size)) {
        // (no smaller: kept as it is)
        for (uint32_t i = 0; i < size; ++i)
            buffer[i] = code[i];
        stored = static_cast<int>(size);
    }
    // {checksum, size, stored size, the code} (the script cache's StoredScript; the same in pre-compiled images)
    auto* block = static_cast<uint8_t*>(memory::alloc_block(static_cast<unsigned int>(stored + 12)));
    util::ByteWriter writer(block, static_cast<size_t>(stored) + 12);
    writer.u32(checksum);
    writer.u32(size);
    writer.u32(static_cast<uint32_t>(stored));
    writer.bytes(buffer, static_cast<size_t>(stored));
    symbol->raw = reinterpret_cast<uint32_t>(block);
    memory::free_malloc(buffer);
    if (script_store_instance())
        remove_from_script_cache(name);
    symbol->unknown8 = checksums::hash_from_text(filename);
    return symbol;
}

// 0x00472420
void read_qb_file(const char* filename, const uint8_t* data, int unused, int use_qb_names, int temporary)
{
    (void)unused; // (line numbers are skipped either way)
    free_script_checksum_names();
    if (use_qb_names)
        allocate_script_checksum_names(false);

    // First the checksums' names.
    for (const uint8_t* p = data; *p;) {
        if (*p == 0x2B) {
            uint32_t checksum = fetch_word32(p + 1);
            const char* name = reinterpret_cast<const char*>(p + 5);
            if (use_qb_names)
                add_script_checksum_name(checksum, name);
            else
                register_name_hash(checksum, name);
            note_qb_name(checksum, name); // (ours: kept for the level editor when it's on)
            p = reinterpret_cast<const uint8_t*>(name + std::strlen(name) + 1);
        } else {
            p = step_over_token(p);
        }
    }
    register_name_hash(checksums::hash_from_text(filename), filename);
    loading_file() = checksums::hash_from_text(filename);

    memory::Hub* manager = memory::Hub::get_pull();
    memory::enter_heap_scope(manager->script_heap_);
    const uint8_t* p = data;
    for (;;) {
        p = skip_newline_tokens(p);
        switch (*p) {
        case 0x00:
            memory::leave_heap_scope();
            loading_file() = 0;
            return;
        case 0x16: { // name = value
            uint32_t name = fetch_word32(p + 1);
            p = skip_newline_tokens(p + 5);
            p = skip_newline_tokens(p + 1); // (past the '=')
            SymbolTableEntry* existing = find_symbol(name);
            if (existing)
                get_replace(existing, temporary);
            p = parse_symbol_definition(p, name, filename);
            if (!existing && temporary)
                remove_symbol_later(find_symbol(name));
            break;
        }
        case 0x23: { // script name ... endscript
            uint32_t name = fetch_word32(p + 2);
            const uint8_t* start = p + 6;
            p = skip_to_end_of_script(start);
            uint32_t size = static_cast<uint32_t>(p - start);
            uint32_t checksum = calculate_script_checksum(start);
            SymbolTableEntry* existing = find_symbol(name);
            if (existing) {
                // (an unchanged script is left as it is)
                if (existing->type == kQbvalScriptBody && checksum == *reinterpret_cast<uint32_t*>(existing->raw))
                    break;
                get_replace(existing, temporary);
                define_script_symbol(size, start, name, checksum, filename);
            } else {
                SymbolTableEntry* symbol = define_script_symbol(size, start, name, checksum, filename);
                if (temporary)
                    remove_symbol_later(symbol);
            }
            break;
        }
        case 0x2B:
            p = step_over_token(p);
            break;
        case 0x45: { // a section marker
            uint32_t marker = fetch_word32(p + 2);
            p += 6;
            if (marker == 0x0FA33D9B) {
                // The end of a debug section.
                memory::leave_heap_scope();
                memory::enter_heap_scope(manager->script_heap_);
            } else if (marker == 0x70CB0238) {
                // A debug section: kept (on the debug heap) only while checksum names are.
                if (checksum_names_enabled()) {
                    memory::leave_heap_scope();
                    memory::enter_heap_scope(manager->debug_heap_);
                } else {
                    while (*p != 0x45 && *p != 0x00)
                        p = step_over_token(p);
                }
            }
            break;
        }
        default:
            // (other tokens don't move on: the original loops here)
            break;
        }
    }
}

// 0x00472C50
bool script_refers_to(const Script* script, uint32_t name)
{
    if (script->script_title_ == name)
        return true;
    // The calls it's in the middle of.
    for (int i = 0; i < script->call_depth_; ++i) {
        if (script->frame_records_[i].script_id_ == name)
            return true;
    }
    return false;
}

// 0x0046ED20
void restart_changed_scripts()
{
    for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
        // (byte 0 bit 0: new since the last time - create_symbol sets it)
        if ((symbol->unknown0 & 1) && symbol->type == kQbvalScriptBody) {
            for (Script* script = step_script_list(nullptr); script; script = step_script_list(script)) {
                if (script_refers_to(script, symbol->name))
                    script->relaunch();
            }
            symbol->unknown0 &= 0xFE;
        }
    }
}

// 0x0046ED90
void load_qb_data(const char* filename, const uint8_t* data, int unused, int use_qb_names, int temporary)
{
    if (fetch_word32(data) == 0xB075EF6F)
        parse_precompiled_qb(filename, const_cast<uint8_t*>(data), unused); // (relocated in place)
    else
        read_qb_file(filename, data, unused, use_qb_names, temporary);
    restart_changed_scripts();
}

// 0x0046EDE0
void load_qb(const char* filename, int unused, int temporary)
{
    size_t length = std::strlen(filename);
    const char* extension = precompiled_extension();
    memory::Hub* manager = memory::Hub::get_pull();
    if (filename[length - 4] == '.' && _stricmp(filename + length - 3, extension) == 0) {
        // Pre-compiled: kept loaded.
        memory::enter_heap_scope(manager->script_heap_);
        auto* data = static_cast<uint8_t*>(packs::read_in(filename, false, nullptr, nullptr, nullptr));
        memory::leave_heap_scope();
        parse_precompiled_qb(filename, data, unused);
        load_checksum_names(filename);
    } else {
        memory::enter_heap_scope(manager->top_down_heap_);
        // Originally from PartyMod Fixes (its call at 0x0046EE80): packs::ReadIn, then the binary patch for the file if there is one
        // (scripts\engine\menu\keyboard.qb), in place of the original's plain packs::ReadIn.
        auto* data = load_patched_script(filename);
        memory::leave_heap_scope();
        read_qb_file(filename, data, unused, 0, temporary);
        packs::offload(filename);
    }
    restart_changed_scripts();
}

// 0x0046EEC0
void unload_qb(uint32_t file_checksum)
{
    if (!file_checksum)
        return;
    // The file's symbols: listed first (removing them changes the table), then removed.
    int count = 0;
    for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
        if (symbol->unknown8 == file_checksum)
            ++count;
    }
    if (count) {
        memory::enter_heap_scope(memory::Hub::get_pull()->top_down_heap_);
        auto** symbols = static_cast<SymbolTableEntry**>(memory::alloc_block(static_cast<unsigned int>(count) * 4));
        memory::leave_heap_scope();
        SymbolTableEntry** next = symbols;
        for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
            if (symbol->unknown8 == file_checksum)
                *next++ = symbol;
        }
        for (int i = 0; i < count; ++i)
            clean_up_and_remove_symbol(symbols[i]);
        memory::free_malloc(symbols);
    }
    // Scripts left running code that's gone are stopped.
    for (Script* script = step_script_list(nullptr); script; script = step_script_list(script)) {
        if (!script->all_scripts_still_defined()) {
            script->reset_script_state();
            script->report_stopped();
        }
    }
    // (a pre-compiled file stays loaded until now)
    packs::offload(file_checksum);
}

} // namespace script

namespace {
struct ScriptAdapter {
    bool refers_to(uint32_t name) { return script::script_refers_to(reinterpret_cast<script::Script*>(this), name); }
};
} // namespace
