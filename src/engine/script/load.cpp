// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/script/load.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <cstring>
#include <string>
#include <string_view>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/token_reader.h"
#include "engine/script/patched.h"
#include "engine/script/precompiled.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/util/lzss.h"
#include "formats/byte_stream.h"
#include "engine/sys/game_config.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace script {

namespace {

// The checksum of the .qb file being loaded (0 outside loading).
uint32_t& loading_file()
{
    static uint32_t loading_file{};
    return loading_file;
}

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
Tokens parse_symbol_definition(Tokens token, uint32_t name, const char* filename)
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
        gsl::owner<Component*> value = new_component();
        const Tokens end =
            *token == 0x0E ? parse_expression(token, nullptr, value) : parse_value(token, nullptr, value);
        symbol->type = value->kind_tag();
        symbol->value.set_word(value->value.word());
        value->value.set_word(0);
        free_component(value);
        return end;
    }
    case 0x03: {
        symbol->type = kQbvalStruct;
        gsl::owner<Struct*> structure = script::new_struct();
        symbol->value.set_structure(structure);
        return parse_structure(structure, token, nullptr);
    }
    case 0x05: {
        symbol->type = kQbvalArray;
        gsl::owner<Array*> array = script::new_array();
        symbol->value.set_array(array);
        return parse_array(array, token, nullptr);
    }
    default:
        return token;
    }
}

// 0x0046FE40
SymbolTableEntry* define_script_symbol(std::span<const uint8_t> code, uint32_t name, uint32_t checksum,
                                       const char* filename)
{
    const auto size = static_cast<uint32_t>(code.size());
    SymbolTableEntry* symbol = create_symbol(name);
    symbol->type = kQbvalScriptBody;
    // (not original: room for LZSS's worst case - the original's 20000 bytes overflowed for a script past ~17.7 KB
    // that doesn't compress)
    const uint32_t room = size + size / 8 + 2 > 20000 ? size + size / 8 + 2 : 20000;
    auto* buffer = static_cast<uint8_t*>(memory::alloc_block(room));
    const std::span<uint8_t> packed(buffer, room);
    int stored = lzss::pack_up(code, packed, false);
    if (stored >= ints::as_signed(size)) {
        // (no smaller: kept as it is)
        std::ranges::copy(code, packed.begin());
        stored = static_cast<int>(size);
    }
    // {checksum, size, stored size, the code} (the script cache's StoredScript; the same in pre-compiled images)
    auto* block = static_cast<uint8_t*>(memory::alloc_block(static_cast<unsigned int>(stored + 12)));
    util::ByteWriter writer(block, static_cast<size_t>(stored) + 12);
    writer.u32(checksum);
    writer.u32(size);
    writer.u32(static_cast<uint32_t>(stored));
    writer.bytes(buffer, static_cast<size_t>(stored));
    symbol->value.set_code(block);
    memory::free_malloc(buffer);
    if (script_store_instance())
        remove_from_script_cache(name);
    symbol->unknown8 = checksums::hash_from_text(filename);
    return symbol;
}

// 0x00472420
void read_qb_file(const char* filename, std::span<const uint8_t> file, int unused, int use_qb_names, int temporary)
{
    (void)unused; // (line numbers are skipped either way)
    free_script_checksum_names();
    if (use_qb_names)
        allocate_script_checksum_names(false);

    // First the checksums' names.
    for (Tokens p(file); *p;) {
        if (*p == 0x2B) {
            uint32_t checksum = 0;
            const char* name = nullptr;
            const Tokens next = read_name_token(p, &checksum, &name);
            if (use_qb_names)
                add_script_checksum_name(checksum, name);
            else
                register_name_hash(checksum, name);
            note_qb_name(checksum, name); // (ours: kept for the level editor when it's on)
            p = next;
        } else {
            p = step_over_token(p);
        }
    }
    register_name_hash(checksums::hash_from_text(filename), filename);
    loading_file() = checksums::hash_from_text(filename);

    const memory::Hub* manager = memory::Hub::get_pull();
    memory::enter_heap_scope(manager->script_heap_);
    Tokens p(file);
    for (;;) {
        p = skip_newline_tokens(p);
        switch (*p) {
        case 0x00:
            memory::leave_heap_scope();
            loading_file() = 0;
            return;
        case 0x16: { // name = value
            const uint32_t name = fetch_word32(p + 1);
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
            const uint32_t name = fetch_word32(p + 2);
            const Tokens start = p + 6;
            p = skip_to_end_of_script(start);
            const std::span<const uint8_t> code = code_between(start, p);
            const uint32_t checksum = calculate_script_checksum(start);
            SymbolTableEntry* existing = find_symbol(name);
            if (existing) {
                // (an unchanged script is left as it is)
                if (existing->type == kQbvalScriptBody && checksum == read_stored_script(existing->value.code()).checksum_)
                    break;
                get_replace(existing, temporary);
                define_script_symbol(code, name, checksum, filename);
            } else {
                SymbolTableEntry* symbol = define_script_symbol(code, name, checksum, filename);
                if (temporary)
                    remove_symbol_later(symbol);
            }
            break;
        }
        case 0x2B:
            p = step_over_token(p);
            break;
        case 0x45: { // a section marker
            const uint32_t marker = fetch_word32(p + 2);
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
        if (core::at(script->call_stack(), i).script_id_ == name)
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
void load_qb_data(const char* filename, std::span<uint8_t> data, int unused, int use_qb_names, int temporary)
{
    if (fetch_word32(Tokens(data)) == 0xB075EF6F)
        parse_precompiled_qb(filename, data, unused); // (relocated in place)
    else
        read_qb_file(filename, data, unused, use_qb_names, temporary);
    restart_changed_scripts();
}

// 0x0046EDE0
void load_qb(const char* filename, int unused, int temporary)
{
    const std::string_view name = filename;
    const char* extension = precompiled_extension();
    const memory::Hub* manager = memory::Hub::get_pull();
    // (the name's last 4 characters: '.' and the platform's extension - the original read before a shorter name)
    if (name.size() >= 4 && name.substr(name.size() - 4, 1) == "." &&
        _stricmp(std::string(name.substr(name.size() - 3)).c_str(), extension) == 0) {
        // Pre-compiled: kept loaded.
        memory::enter_heap_scope(manager->script_heap_);
        auto* data = static_cast<uint8_t*>(packs::read_in(filename, false, nullptr, nullptr, nullptr));
        memory::leave_heap_scope();
        const int size = packs::get_file_size(checksums::hash_from_text(filename));
        parse_precompiled_qb(filename, std::span<uint8_t>(data, size > 0 ? static_cast<size_t>(size) : 0), unused);
        load_checksum_names(filename);
    } else {
        memory::enter_heap_scope(manager->top_down_heap_);
        // Originally from PartyMod Fixes (its call at 0x0046EE80): packs::ReadIn, then the binary patch for the file if there is one
        // (scripts\engine\menu\keyboard.qb), in place of the original's plain packs::ReadIn.
        const std::span<const uint8_t> data = load_patched_script(filename);
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
        auto** block = static_cast<SymbolTableEntry**>(memory::alloc_block(static_cast<unsigned int>(count) * 4));
        memory::leave_heap_scope();
        const std::span<SymbolTableEntry*> symbols(block, static_cast<size_t>(count));
        size_t next = 0;
        for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
            if (symbol->unknown8 == file_checksum)
                core::at(symbols, next++) = symbol;
        }
        for (SymbolTableEntry* symbol : symbols)
            clean_up_and_remove_symbol(symbol);
        memory::free_malloc(static_cast<void*>(block));
    }
    // Scripts left running code that's gone are stopped.
    for (Script* script = step_script_list(nullptr); script; script = step_script_list(script)) {
        if (!script->all_scripts_still_defined()) {
            script->reset_script_state();
            script->report_stopped();
        }
    }
    // (a pre-compiled file stays loaded until now)
    forget_precompiled_image(file_checksum);
    packs::offload(file_checksum);
}

} // namespace script
