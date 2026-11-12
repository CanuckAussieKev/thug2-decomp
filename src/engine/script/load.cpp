// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/script/load.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <cstring>
#include <string>
#include <string_view>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/file/packs.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
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
#include "base/core/ints.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>

namespace {

// Names (the checksums of their text).
constexpr uint32_t kDebugHeap = 0x70CB0238U;  // DebugHeap
constexpr uint32_t kPIP = 0xB075EF6FU;        // PIP
constexpr uint32_t kScriptHeap = 0x0FA33D9BU; // ScriptHeap

} // namespace

namespace script {
namespace {
// A stored script's header (checksum, size, stored size) before its code.
constexpr int kStoredHeaderBytes = static_cast<int>(kStoredScriptHeader);
// byte 0 of a symbol, without its "new since the last time" bit.
constexpr uint8_t kSymbolNotNewMask = 0xFE;
} // namespace

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
    if (temporary != 0) {
        if (settle_pending_replacement(existing)) { // NOT ORIGINAL (qb.cpp)
            return;
        }
        replace_symbol_later(existing);
        unlink_symbol(existing);
    } else {
        clean_up_and_remove_symbol(existing);
    }
}

QbLoadedHook& qb_loaded_hook()
{
    static QbLoadedHook hook = nullptr;
    return hook;
}

} // namespace

void set_qb_loaded_hook(QbLoadedHook hook) { qb_loaded_hook() = hook; }
namespace {
QbUnloadedHook& qb_unloaded_hook()
{
    static QbUnloadedHook hook = nullptr;
    return hook;
}
} // namespace
void set_qb_unloaded_hook(QbUnloadedHook hook) { qb_unloaded_hook() = hook; }
namespace {
QbStandaloneHook& qb_standalone_hook()
{
    static QbStandaloneHook hook = nullptr;
    return hook;
}
} // namespace
void set_qb_standalone_hook(QbStandaloneHook hook) { qb_standalone_hook() = hook; }
bool has_qb_standalone_hook() { return qb_standalone_hook() != nullptr; }
void replace_symbol_for_definition(SymbolTableEntry* existing, bool temporary)
{
    get_replace(existing, temporary ? 1 : 0);
}

// 0x00472240
Tokens parse_symbol_definition(Tokens token, uint32_t name, const char* filename)
{
    SymbolTableEntry* symbol = create_symbol(name);
    symbol->set_unknown8(checksums::hash_from_text(filename));
    switch (as_token(*token)) {
    case QbToken::kOpenParenthesis:
    case QbToken::kName:
    case QbToken::kInteger:
    case QbToken::kFloat:
    case QbToken::kString:
    case QbToken::kLocalString:
    case QbToken::kVector:
    case QbToken::kPair: {
        gsl::owner<Component*> value = new_component();
        const Tokens end =
            *token == 0x0E ? parse_expression(token, nullptr, value) : parse_value(token, nullptr, value);
        symbol->set_type(value_kind(*value));
        symbol->value() = value->value;
        value->value.clear();
        free_component(value);
        return end;
    }
    case QbToken::kStructStart: {
        symbol->set_type(QbValueKind::kStruct);
        gsl::owner<Struct*> structure = script::new_struct();
        symbol->value().set_structure(structure);
        return parse_structure(structure, token, nullptr);
    }
    case QbToken::kArrayStart: {
        symbol->set_type(QbValueKind::kArray);
        gsl::owner<Array*> array = script::new_array();
        symbol->value().set_array(array);
        return parse_array(array, token, nullptr);
    }
    default:
        return token;
    }
}

// 0x0046FE40
SymbolTableEntry* define_script_symbol(std::span<const uint8_t> code, checksums::Name script, uint32_t checksum,
                                       const char* filename)
{
    const uint32_t name = script.value();
    const auto size = static_cast<uint32_t>(code.size());
    SymbolTableEntry* symbol = create_symbol(name);
    symbol->set_type(QbValueKind::kScriptBody);
    // (not original: room for LZSS's worst case - the original's 20000 bytes overflowed for a script past ~17.7 KB
    // that doesn't compress)
    const uint32_t room = size + (size / 8) + 2 > 20000 ? size + (size / 8) + 2 : 20000;
    auto* buffer = static_cast<uint8_t*>(memory::alloc_block(room));
    const std::span<uint8_t> packed(buffer, room);
    int stored = lzss::pack_up(code, packed, false);
    if (stored >= ints::as_signed(size)) {
        // (no smaller: kept as it is)
        std::ranges::copy(code, packed.begin());
        stored = static_cast<int>(size);
    }
    // {checksum, size, stored size, the code} (the script cache's StoredScript; the same in pre-compiled images)
    auto* block = static_cast<uint8_t*>(memory::alloc_block(static_cast<unsigned int>(stored + kStoredHeaderBytes)));
    util::ByteWriter writer(block, static_cast<size_t>(stored) + kStoredScriptHeader);
    writer.u32(checksum);
    writer.u32(size);
    writer.u32(static_cast<uint32_t>(stored));
    writer.bytes(buffer, static_cast<size_t>(stored));
    symbol->value().set_code(block);
    memory::free_malloc(buffer);
    if (script_store_instance()) {
        remove_from_script_cache(name);
    }
    symbol->set_unknown8(checksums::hash_from_text(filename));
    return symbol;
}

// 0x00472420
void read_qb_file(const char* filename, std::span<const uint8_t> file, int /*unused*/, bool use_qb_names,
                  bool temporary)
{
    // (the third argument: line numbers are skipped either way)
    free_script_checksum_names();
    if (use_qb_names) {
        allocate_script_checksum_names(false);
    }

    // First the checksums' names.
    for (Tokens cursor(file); *cursor != 0U;) {
        if (as_token(*cursor) == QbToken::kChecksumName) {
            uint32_t checksum = 0;
            const char* name = nullptr;
            const Tokens next = read_name_token(cursor, &checksum, &name);
            if (use_qb_names) {
                add_script_checksum_name(checksum, name);
            } else {
                register_name_hash(checksum, name);
            }
            note_qb_name(checksum, name); // (ours: kept for the level editor when it's on)
            cursor = next;
        } else {
            cursor = step_over_token(cursor);
        }
    }
    register_name_hash(checksums::hash_from_text(filename), filename);
    loading_file() = checksums::hash_from_text(filename);

    const memory::Hub* manager = memory::Hub::get_pull();
    memory::enter_heap_scope(manager->script_heap());
    Tokens cursor(file);
    for (;;) {
        cursor = skip_newline_tokens(cursor);
        switch (as_token(*cursor)) {
        case QbToken::kEndOfFile:
            memory::leave_heap_scope();
            loading_file() = 0;
            if (qb_loaded_hook()) { // NOT ORIGINAL - the file's TypeScript module, if any (ts_bridge.h)
                qb_loaded_hook()(filename, file, temporary);
            }
            return;
        case QbToken::kName: { // name = value
            const uint32_t name = fetch_word32(cursor + 1);
            cursor = skip_newline_tokens(cursor + kTokenWithWordBytes);
            cursor = skip_newline_tokens(cursor + 1); // (past the '=')
            SymbolTableEntry* existing = find_symbol(name);
            if (existing) {
                get_replace(existing, static_cast<int>(temporary));
            }
            cursor = parse_symbol_definition(cursor, name, filename);
            if (!existing && temporary) {
                remove_symbol_later(find_symbol(name));
            }
            break;
        }
        case QbToken::kScriptStart: { // script name ... endscript
            const uint32_t name = fetch_word32(cursor + 2);
            const Tokens start = cursor + 6;
            cursor = skip_to_end_of_script(start);
            const std::span<const uint8_t> code = code_between(start, cursor);
            const uint32_t checksum = calculate_script_checksum(start);
            SymbolTableEntry* existing = find_symbol(name);
            if (existing) {
                // (an unchanged script is left as it is)
                if (existing->type() == QbValueKind::kScriptBody &&
                    checksum == read_stored_script(existing->value().code()).checksum_) {
                    break;
                }
                get_replace(existing, static_cast<int>(temporary));
                define_script_symbol(code, checksums::Name{name}, checksum, filename);
            } else {
                SymbolTableEntry* symbol = define_script_symbol(code, checksums::Name{name}, checksum, filename);
                if (temporary) {
                    remove_symbol_later(symbol);
                }
            }
            break;
        }
        case QbToken::kChecksumName:
            cursor = step_over_token(cursor);
            break;
        case QbToken::kSectionMarker: { // a section marker
            const uint32_t marker = fetch_word32(cursor + 2);
            cursor += kTokenWithNamedWordBytes;
            if (marker == kScriptHeap) {
                // The end of a debug section.
                memory::leave_heap_scope();
                memory::enter_heap_scope(manager->script_heap());
            } else if (marker == kDebugHeap) {
                // A debug section: kept (on the debug heap) only while checksum names are.
                if (checksum_names_enabled()) {
                    memory::leave_heap_scope();
                    memory::enter_heap_scope(manager->debug_heap());
                } else {
                    while (as_token(*cursor) != QbToken::kSectionMarker && as_token(*cursor) != QbToken::kEndOfFile) {
                        cursor = step_over_token(cursor);
                    }
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
    if (script->script_title() == name) {
        return true;
    }
    // The calls it's in the middle of.
    for (int i = 0; i < script->call_depth(); ++i) {
        if (core::at(script->call_stack(), i).script_id_ == name) {
            return true;
        }
    }
    return false;
}

// 0x0046ED20
void restart_changed_scripts()
{
    for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
        // (byte 0 bit 0: new since the last time - create_symbol sets it)
        if (((symbol->unknown0() & 1) != 0) && symbol->type() == QbValueKind::kScriptBody) {
            host().relaunch_scripts_referring_to(symbol->name_value());
            symbol->unknown0() &= kSymbolNotNewMask;
        }
    }
}

// 0x0046ED90
void load_qb_data(const char* filename, std::span<uint8_t> data, int unused, int use_qb_names, int temporary)
{
    // NOT ORIGINAL - its standalone TypeScript module defines the file (load.h; a level's node array, read as an asset:
    // in an unpacked game - tools/qbjs/unpack_game.ts - with nothing read)
    if (qb_standalone_hook() && qb_standalone_hook()(filename, temporary != 0)) {
        free_script_checksum_names(); // (as read_qb_file does first)
        restart_changed_scripts();
        return;
    }
    if (data.size() < sizeof(uint32_t)) {
        debug::printf("%s: no .qb to read\n", filename);
        return;
    }
    if (fetch_word32(Tokens(data)) == kPIP) {
        parse_precompiled_qb(filename, data, unused); // (unpacked: precompiled.h)
    } else {
        read_qb_file(filename, data, unused, use_qb_names != 0, temporary != 0);
    }
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
        memory::enter_heap_scope(manager->script_heap());
        auto* data = static_cast<uint8_t*>(packs::read_in(filename, false, nullptr, nullptr, nullptr));
        memory::leave_heap_scope();
        const int size = packs::get_file_size(checksums::hash_from_text(filename));
        parse_precompiled_qb(filename, std::span<uint8_t>(data, size > 0 ? static_cast<size_t>(size) : 0), unused);
        load_checksum_names(filename);
    } else if (qb_standalone_hook() && qb_standalone_hook()(filename, temporary != 0)) {
        // NOT ORIGINAL - its standalone TypeScript module defined the file (load.h): no .qb read
        free_script_checksum_names(); // (as read_qb_file does first)
    } else {
        memory::enter_heap_scope(manager->top_down_heap());
        // Originally from PartyMod Fixes (its call at 0x0046EE80): packs::ReadIn, then the binary patch for the file if there is one
        // (scripts\engine\menu\keyboard.qb), in place of the original's plain packs::ReadIn.
        const std::span<const uint8_t> data = load_patched_script(filename);
        memory::leave_heap_scope();
        read_qb_file(filename, data, unused, false, temporary != 0);
        packs::offload(filename);
    }
    restart_changed_scripts();
}

// 0x0046EEC0
void unload_qb(uint32_t file_checksum)
{
    if (file_checksum == 0U) {
        return;
    }
    // The file's symbols: listed first (removing them changes the table), then removed.
    int count = 0;
    for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
        if (symbol->unknown8() == file_checksum) {
            ++count;
        }
    }
    if (count != 0) {
        memory::enter_heap_scope(memory::Hub::get_pull()->top_down_heap());
        auto** block = static_cast<SymbolTableEntry**>(
            memory::alloc_block(static_cast<unsigned int>(count) * memory::kPointerBytes));
        memory::leave_heap_scope();
        const std::span<SymbolTableEntry*> symbols(block, static_cast<size_t>(count));
        size_t next = 0;
        for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
            if (symbol->unknown8() == file_checksum) {
                core::at(symbols, next++) = symbol;
            }
        }
        for (SymbolTableEntry* symbol : symbols) {
            clean_up_and_remove_symbol(symbol);
        }
        memory::free_malloc(static_cast<void*>(block));
    }
    if (qb_unloaded_hook()) { // NOT ORIGINAL - the file's TypeScript functions let go of (ts_bridge.h)
        qb_unloaded_hook()(file_checksum);
    }
    // Scripts left running code that's gone are stopped.
    host().stop_scripts_left_without_code();
    // (a pre-compiled file stays loaded until now)
    packs::offload(file_checksum);
}

} // namespace script
