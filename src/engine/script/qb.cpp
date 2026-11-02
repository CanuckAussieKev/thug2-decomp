#include "engine/script/qb.h"
#include "formats/byte_stream.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <span>
#include <new>
#include <string>
#include <unordered_map>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/memory_sizes.h"
#include "engine/core/slab_bank.h"
#include "engine/maths/random.h"
#include "engine/script/cache.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/token_reader.h"
#include "engine/script/cscript.h"
#include "engine/script/tokens.h"
#include "platform/run_log.h"
#include "engine/core/bits_as.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/script/shared_state.h"

namespace script {

StoredScript read_stored_script(const uint8_t* record)
{
    util::ByteReader header(record, kStoredScriptHeader);
    StoredScript out;
    out.checksum_ = header.u32();
    out.size_ = header.u32();
    out.stored_size_ = header.u32();
    out.code_ = std::span<const uint8_t>(header.cursor(), out.stored_size_);
    return out;
}

static_assert(offsetof(StoredRandom, num_) == 8, "layout must match the original");
static_assert(offsetof(StoredRandom, order_) == 0x12, "layout must match the original");
static_assert(offsetof(StoredRandom, last_) == 0x54 && offsetof(StoredRandom, succ_) == 0x58,
              "layout must match the original");
static_assert(sizeof(StoredRandom) == 0x60, "layout must match the original");

QbShared& qb_shared()
{
    static core::NoDestroy<QbShared> state;
    return state.get();
}

namespace {

// The table of the .qb files' own checksum names: {checksum, name, next}, the first entry in the bucket.
struct ScriptChecksumName {
    uint32_t checksum;
    char* name;
    ScriptChecksumName* next;
};
ScriptChecksumName*& script_label_table()
{
    static ScriptChecksumName* script_labels{};
    return script_labels;
}
// The table's 0x1000 buckets (the checksums' low 12 bits; the first entry of each in place).
constexpr uint32_t kLabelBuckets = 0x1000;
std::span<ScriptChecksumName, kLabelBuckets> label_buckets()
{
    return std::span<ScriptChecksumName, kLabelBuckets>(script_label_table(), kLabelBuckets);
}

char* copy_string(const char* string)
{
    const size_t size = std::strlen(string) + 1;
    auto* copy = static_cast<char*>(memory::alloc_block(static_cast<unsigned int>(size)));
    std::memcpy(copy, string, size);
    return copy;
}

} // namespace

// Create a pool of `count` items of `item_size` as the current pool of `pools`.
void create_pool(memory::PoolPair& pools, int item_size, int count, const char* name)
{
    if (!memory::manager_ready_flag())
        memory::ensure_manager();
    void* memory = memory::Hub::get_pull()->carve(sizeof(memory::SlabBank), 1, nullptr);
    memory::SlabBank* pool = memory ? memory::construct_in<memory::SlabBank>(memory, item_size, count, name) : nullptr;
    core::at(pools.pools, pools.current) = pool;
    core::at(pools.made_here_, pools.current) = 1;
}

// 0x0046F690
Tokens step_over_token(Tokens token)
{
    switch (*token) {
    case 0x01:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2C:
    case 0x2D:
    case 0x30:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x38:
    case 0x39:
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F:
    case 0x42:
    case 0x45:
        return token + 1;
    case 0x02:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x1A:
    case 0x2E:
    case 0x43:
    case 0x44:
    case 0x46:
        return token + 5;
    case 0x1B:
    case 0x1C: // strings: the length, then the text
        return token + 5 + fetch_word32(token + 1);
    case 0x1E: // vector
        return token + 13;
    case 0x1F: // pair
        return token + 9;
    case 0x2B: { // a checksum's name: the checksum, then the text
        Tokens p = token + 5;
        while (*p)
            ++p;
        return p + 1;
    }
    case 0x2F:
    case 0x37:
    case 0x40:
    case 0x41: // tables of 6-byte entries
        return token + 5 + fetch_word32(token + 1) * 6;
    case 0x47:
    case 0x48:
    case 0x49:
        return token + 3;
    default: // (0x00, 0x10, 0x19, 0x1D, 0x2A, 0x31 and unknown tokens: no step)
        return token;
    }
}

// 0x0046F7E0
uint32_t fetch_word32(Tokens p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// 0x0046F800
uint16_t fetch_word16(Tokens p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// 0x0046F810
MutableTokens store_word32(MutableTokens p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
    return p + 4;
}

// 0x0046F840
MutableTokens store_word16(MutableTokens p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    return p + 2;
}

// 0x0046F850
Tokens skip_newline_tokens(Tokens token)
{
    for (;;) {
        if (*token == 0x01)
            token += 1;
        else if (*token == 0x02)
            token += 5;
        else
            return token;
    }
}

// 0x0046FB30
Tokens skip_to_end_of_statement(Tokens token)
{
    int braces = 0, brackets = 0, parentheses = 0;
    for (;;) {
        const uint8_t t = *token;
        if (t == 0x03)
            ++braces;
        if (t == 0x04)
            --braces;
        if (t == 0x05)
            ++brackets;
        if (t == 0x06)
            --brackets;
        if (t == 0x0E)
            ++parentheses;
        if (t == 0x0F)
            --parentheses;
        if (braces == 0 && brackets == 0 && parentheses == 0 && (t == 0x01 || t == 0x02 || t == 0x00))
            return step_over_token(token);
        token = step_over_token(token);
    }
}

// 0x0046F920
Tokens skip_to_end_of_script(Tokens token)
{
    while (*token != 0x24)
        token = step_over_token(token);
    token = step_over_token(token);
    // (a script whose '$' ends its block - a structure's script body, a private copy - has no line number after it:
    // the original read the byte past the block)
    if (token.remaining() > 0 && *token == 0x02)
        return step_over_token(token);
    return token;
}

// 0x0046F960
uint32_t calculate_script_checksum(Tokens token)
{
    uint32_t checksum = 0xFFFFFFFF;
    while (*token != 0x24) {
        const Tokens next = step_over_token(token);
        if (*token != 0x01 && *token != 0x02) {
            const std::span<const uint8_t> bytes = code_between(token, next);
            checksum = checksums::extend_crc_with_buffer(bytes.data(), static_cast<int>(bytes.size()), checksum);
        }
        token = next;
    }
    return checksum;
}

// 0x0046FF00
void prepare_script_tokens(std::span<uint8_t> code)
{
    // (the code is changed in place: its tokens walked as read-only, each rewritten through a writable cursor at the
    // same place)
    Tokens token = skip_to_end_of_statement(Tokens(code));
    while (*token != 0x24) {
        if (*token == 0x16) {
            // A statement starting with a name: a call?
            const MutableTokens name(code, token.position());
            token += 5;
            if (const SymbolTableEntry* symbol = resolve_symbol(fetch_word32(name + 1))) {
                if (symbol->type == kQbvalNativeFunc) {
                    name[0] = 0x43;
                    store_word32(name + 1, symbol->value.word());
                } else if (symbol->type == kQbvalNativeMethod) {
                    if (symbol->value.word()) {
                        store_word32(name + 1, symbol->value.word());
                        name[0] = 0x46;
                    } else {
                        name[0] = 0x44;
                    }
                }
            }
            token = skip_to_end_of_statement(token);
        } else if (*token == 0x47) {
            token = step_over_token(token);
            if (*token == 0x39)
                ++token;
        } else {
            token = skip_to_end_of_statement(token);
        }
    }
}

// 0x0046F9B0
const char* find_script_checksum_name(uint32_t checksum)
{
    if (!script_label_table())
        return nullptr;
    for (const ScriptChecksumName* entry = &core::at(label_buckets(), checksum & 0xFFF); entry; entry = entry->next) {
        if (entry->checksum == checksum)
            return entry->name;
    }
    return nullptr;
}

// 0x0046F9F0
void free_script_checksum_names()
{
    if (!script_label_table())
        return;
    for (const ScriptChecksumName& bucket : label_buckets()) {
        for (ScriptChecksumName* entry = bucket.next; entry;) {
            ScriptChecksumName* next = entry->next;
            memory::free_malloc(entry->name);
            memory::free_malloc(entry);
            entry = next;
        }
        if (bucket.name)
            memory::free_malloc(bucket.name);
    }
    memory::free_malloc(script_label_table());
    script_label_table() = nullptr;
}

namespace {
bool& keep_qb_names()
{
    static bool keep_qb_names{};
    return keep_qb_names;
}
std::unordered_map<uint32_t, std::string>& kept_names()
{
    static std::unordered_map<uint32_t, std::string> names;
    return names;
}
} // namespace

void keep_qb_names(bool keep) { keep_qb_names() = keep; }

void note_qb_name(uint32_t checksum, const char* name)
{
    if (keep_qb_names() && name && *name)
        kept_names()[checksum] = name;
}

const char* kept_qb_name(uint32_t checksum)
{
    auto it = kept_names().find(checksum);
    return it == kept_names().end() ? nullptr : it->second.c_str();
}

// 0x0046FA60 (a checksum already there is added again)
void add_script_checksum_name(uint32_t checksum, const char* name)
{
    ScriptChecksumName& bucket = core::at(label_buckets(), checksum & 0xFFF);
    if (bucket.name) {
        auto* entry = static_cast<ScriptChecksumName*>(memory::alloc_block(sizeof(ScriptChecksumName)));
        entry->checksum = checksum;
        entry->name = copy_string(name);
        entry->next = bucket.next;
        bucket.next = entry;
    } else {
        bucket.checksum = checksum;
        bucket.name = copy_string(name);
        bucket.next = nullptr;
    }
    register_name_hash(checksum, name);
}

// 0x0046F870
void forget_stored_randoms_of(const Script* owner)
{
    // The stored random states (newest first), their owning scripts let go of.
    for (StoredRandom* entry = token_reader_shared().stored_randoms_newest_; entry; entry = entry->succ_) {
        if (entry->owner_ == owner)
            entry->owner_ = nullptr;
    }
}

// 0x0046F8A0
void StoredRandom::shuffle(bool avoid_repeat)
{
    const uint8_t last = core::at(order_, num_ - 1);
    for (int swaps = num_ * 10; swaps; --swaps) {
        const int a = maths::rnd(num_);
        const int b = maths::rnd(num_);
        const uint8_t swapped = core::at(order_, a);
        core::at(order_, a) = core::at(order_, b);
        core::at(order_, b) = swapped;
    }
    if (avoid_repeat && core::at(order_, 0) == last) {
        const int other = maths::rnd(num_ - 1) + 1;
        const uint8_t swapped = core::at(order_, 0);
        core::at(order_, 0) = core::at(order_, other);
        core::at(order_, other) = swapped;
    }
}

// 0x00476210
void allocate_permanent_string_heap(uint32_t size, uint32_t count)
{
    param_struct_shared().permanent_strings_size_ = size;
    param_struct_shared().permanent_string_capacity_ = count;
    char* strings = static_cast<char*>(memory::alloc_block(size));
    param_struct_shared().permanent_strings_start_ = strings;
    param_struct_shared().permanent_strings_next_ = strings;
    memory::bits_as<void*>(param_struct_shared().permanent_string_table_) = memory::alloc_block(count * 8);
    param_struct_shared().permanent_string_count_ = 0;
}

// 0x0046F520
void create_script_pools()
{
    // Not original - the counts are ours (engine/core/memory_sizes.h: pool sizes as THUGPRO's, thug2-decomp.ini
    // [memory]); the script region they come from is grown to match (memory::Hub::InitStandardHeaps).
    // Original behaviour: CScriptCacheEntry 300, CComponent 86000, CStruct 19800, CVector 9500, CPair 800, CArray
    // 7000, CSymbolTableEntry 12500, CScript 300, CStoredRandom 100, the string heap 190000 bytes / 7000 strings.
    const memory::sizes::Pools& sizes = memory::sizes::at_hand();
    memory::push_mem_profile("AllocateChecksumNameLookupTables");
    allocate_checksum_name_lookup_tables();
    memory::pop_mem_profile();
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    // (0x0046F090, 0x0046F110, 0x0046F190, 0x0046F210, 0x0046F290, 0x0046F310, 0x0046F390, 0x0046F410,
    // 0x0046F4A0: one function per class)
    memory::push_mem_profile("CScriptCacheEntry");
    create_pool(cache_shared().cache_entry_pools_, 0x14, sizes.script_cache_entries_, "CScriptCacheEntry");
    memory::pop_mem_profile();
    memory::push_mem_profile("CComponent and Reserve CComponent");
    create_pool(g_component_pools, 0x10, sizes.components, "CComponent");
    memory::pop_mem_profile();
    memory::push_mem_profile("CStruct");
    create_pool(g_struct_pools, 0x08, sizes.structs, "CStruct");
    memory::pop_mem_profile();
    memory::push_mem_profile("CVector");
    create_pool(g_vector_pools, 0x10, sizes.vectors, "CVector");
    memory::pop_mem_profile();
    memory::push_mem_profile("CPair");
    create_pool(param_struct_shared().pair_pools_, 0x0C, sizes.pairs, "CPair");
    memory::pop_mem_profile();
    memory::push_mem_profile("CArray");
    create_pool(g_array_pools, 0x0C, sizes.arrays, "CArray");
    memory::pop_mem_profile();
    memory::push_mem_profile("CSymbolTableEntry");
    create_pool(g_symbol_pools, 0x14, sizes.symbols, "CSymbolTableEntry");
    memory::pop_mem_profile();
    memory::push_mem_profile("CScript");
    create_pool(g_script_pools, 0xF4, sizes.scripts, "CScript");
    memory::pop_mem_profile();
    memory::push_mem_profile("CStoredRandom");
    create_pool(qb_shared().stored_random_pools_, 0x60, sizes.stored_randoms_, "CStoredRandom");
    memory::pop_mem_profile();
    memory::push_mem_profile("AllocatePermanentStringHeap");
    allocate_permanent_string_heap(sizes.string_bytes_, sizes.strings);
    memory::pop_mem_profile();
    memory::leave_heap_scope();
    // (not original either: the sizes in the log)
    get_log(
        "script pools: CComponent %d, CStruct %d, CVector %d, CPair %d, CArray %d, CSymbolTableEntry %d, CScript %d, "
        "CScriptCacheEntry %d, strings %u bytes / %u (thug2-decomp.ini [memory]; the original's CComponent 86000); "
        "script region %u bytes",
        sizes.components, sizes.structs, sizes.vectors, sizes.pairs, sizes.arrays, sizes.symbols, sizes.scripts,
        sizes.script_cache_entries_, sizes.string_bytes_, sizes.strings, memory::sizes::script_region_size());
}

void log_script_pool_use(const char* when)
{
    // Not original (log only).
    struct Named {
        const char* name;
        memory::PoolPair* pair;
    };
    const auto pools = std::to_array<Named>({{.name = "CComponent", .pair = &g_component_pools}, {.name = "CStruct", .pair = &g_struct_pools},
                           {.name = "CVector", .pair = &g_vector_pools},       {.name = "CPair", .pair = &param_struct_shared().pair_pools_},
                           {.name = "CArray", .pair = &g_array_pools},         {.name = "CSymbolTableEntry", .pair = &g_symbol_pools},
                           {.name = "CScript", .pair = &g_script_pools}});
    std::string line;
    for (const Named& p : pools) {
        int used = 0, peak = 0, count = 0;
        for (const memory::SlabBank* pool : p.pair->pools) {
            if (!pool)
                continue;
            used += pool->taken_count_;
            peak += pool->peak_used_;
            count += pool->num_;
        }
        std::array<char, 96> part{};
        text::format_to(part, "%s%s %d (peak %d) of %d", line.empty() ? "" : ", ", p.name, used, peak, count);
        line += part.data();
    }
    // (and the hash tables' items, Main's HashItem pool - memory_sizes.h hash_item_pool)
    if (const memory::SlabBank* items = memory::slot_pool_setup::get_pool(0)) {
        std::array<char, 96> part{};
        text::format_to(part, ", HashItem %d (peak %d) of %d", items->taken_count_, items->peak_used_, items->num_);
        line += part.data();
    }
    get_log("script pools in use %s: %s", when, line.c_str());
}

} // namespace script

// --- symbol bookkeeping for the loader --------------------------------------------------------------------

namespace script {

// The script cache (0x006F3558, or null): the scripts' decompressed code, by name.
ScriptStore* script_store_instance() { return g_script_store.get(); }

void remove_from_script_cache(uint32_t name) { script_store_instance()->mark_stale(name); }

namespace {

// Symbols waiting to be removed (or replaced) once a .qb file has been loaded: {symbol, owned}.
struct PendingSymbol {
    SymbolTableEntry* symbol;
    bool owned;
};
std::array<PendingSymbol, 500>& pending_symbols()
{
    static core::NoDestroy<std::array<PendingSymbol, 500>> pending_symbols;
    return pending_symbols.get();
}
int& pending_symbol_count()
{
    static int pending_symbol_count{};
    return pending_symbol_count;
}

void add_pending_symbol(SymbolTableEntry* symbol, bool owned)
{
    PendingSymbol& pending = core::at(pending_symbols(), pending_symbol_count());
    pending.symbol = symbol;
    pending.owned = owned;
    ++pending_symbol_count();
    if (symbol->type == kQbvalScriptBody && script_store_instance())
        remove_from_script_cache(symbol->name);
}

} // namespace

// 0x00470000 / 0x0046FFF0
void clear_script_flag_6f_2564() { token_reader_shared().evaluator_.unknown1f4_ = false; } // (0x006F2564: the evaluator's +0x1F4)
void set_script_flag_6f_2564() { token_reader_shared().evaluator_.unknown1f4_ = true; }

// 0x00470010
void allocate_script_checksum_names(bool debug_heap)
{
    const memory::Hub* manager = memory::Hub::get_pull();
    memory::enter_heap_scope(debug_heap ? manager->debug_heap_ : manager->script_heap_);
    auto* names = static_cast<uint32_t*>(memory::alloc_block(0x1000 * 12));
    memory::bits_as<uint32_t*>(script_label_table()) = names;
    memory::leave_heap_scope();
    std::ranges::fill(std::span<uint32_t>(names, 0x1000 * 3), 0u);
}

// 0x0044C1D0 / 0x0044C230 / 0x00470070 / 0x004700D0
void free_pair(void* pair) { param_struct_shared().pair_pools_.dealloc(pair); }
void free_vector(void* vector) { g_vector_pools.dealloc(vector); }
void free_stored_random(void* random) { qb_shared().stored_random_pools_.dealloc(random); }
void free_symbol(SymbolTableEntry* symbol) { g_symbol_pools.dealloc(symbol); }

// 0x004711D0
void clean_up_and_remove_symbol(SymbolTableEntry* symbol)
{
    unlink_symbol(symbol);
    if (symbol->unknown0 & 4) // (kept: not owned by the table?)
        return;
    switch (symbol->type) {
    case kQbvalString:
    case kQbvalInlineText:
        free_string(symbol->value.text());
        break;
    case kQbvalPair:
        free_pair(symbol->value.pair());
        break;
    case kQbvalVector:
        free_vector(symbol->value.vector());
        break;
    case kQbvalScriptBody:
        memory::free_malloc(symbol->value.code());
        break;
    case kQbvalStruct:
        if (Struct* structure = symbol->value.structure()) {
            structure->get_clear();
            delete_struct(structure);
        }
        break;
    case kQbvalArray:
        release_array_storage(symbol->value.array());
        if (auto* array = symbol->value.array()) {
            array->get_clear();
            delete_array(array);
        }
        break;
    default:
        break;
    }
    symbol->value.set_word(0);
    free_symbol(symbol);
}

// 0x004727B0: `symbol` goes once loading is done.
void remove_symbol_later(SymbolTableEntry* symbol) { add_pending_symbol(symbol, true); }
// 0x004727F0: `symbol` (not in the table) replaces the symbol of its name once loading is done.
void replace_symbol_later(SymbolTableEntry* symbol) { add_pending_symbol(symbol, false); }

// 0x00472830
void process_pending_symbols()
{
    for (int i = 0; i < pending_symbol_count(); ++i) {
        const PendingSymbol& pending = core::at(pending_symbols(), i);
        if (pending.owned) {
            clean_up_and_remove_symbol(pending.symbol);
        } else {
            clean_up_and_remove_symbol(find_symbol(pending.symbol->name));
            link_symbol(pending.symbol);
        }
        // (reads the symbol even when it has just been freed, as the original does)
        if (pending.symbol->type == kQbvalScriptBody && script_store_instance())
            remove_from_script_cache(pending.symbol->name);
    }
    pending_symbol_count() = 0;
}

} // namespace script
