#include "engine/script/qb.h"
#include "base/core/layout_32bit.h"
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

#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/memory_sizes.h"
#include "engine/memory/slab_bank.h"
#include "base/maths/random.h"
#include "engine/script/cache.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/token_reader.h"
#include "engine/script/cscript.h"
#include "engine/script/tokens.h"
#include "platform/run_log.h"
#include "base/core/bits_as.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/script/script_state.h"

namespace script {
namespace {
// The pools' item sizes: the classes' sizes (on x86 the original's - asserted in engine/memory/memory_sizes.cpp).
template <typename T>
constexpr int kItemBytes = static_cast<int>(sizeof(T));
constexpr int kScriptCacheEntryPoolBytes = kItemBytes<ScriptCacheEntry>;
constexpr int kComponentPoolBytes = kItemBytes<Component>;
constexpr int kStructPoolBytes = kItemBytes<Struct>;
constexpr int kVectorPoolBytes = kItemBytes<Vector>;
constexpr int kPairPoolBytes = kItemBytes<Pair>;
constexpr int kArrayPoolBytes = kItemBytes<Array>;
constexpr int kSymbolPoolBytes = kItemBytes<SymbolTableEntry>;
constexpr int kScriptPoolBytes = kItemBytes<Script>;
constexpr int kStoredRandomPoolBytes = kItemBytes<StoredRandom>;
// The byte-by-byte word readers and writers.
constexpr int kShiftByte1 = 8;
constexpr int kShiftByte2 = 16;
constexpr int kShiftByte3 = 24;
constexpr uint32_t kChecksumInitial = 0xFFFFFFFFU;
// The names of checksums given in .qb files: 0x1000 buckets (ScriptChecksumName below: three words each on x86).
constexpr uint32_t kLabelBucketMask = 0xFFF;
// A shuffle swaps this many pairs for each choice.
// The permanent string table: one entry per string (8 bytes on x86: memory_sizes.cpp).
constexpr uint32_t kPermanentStringEntryBytes = sizeof(PermanentString);
// (NOT ORIGINAL: 500 in the original, whose 501st pending change runs off the table - a crash. More room changes
// nothing that worked: a temporary load of a big level file - or one with its TypeScript module, which replaces each
// script again - fits)
constexpr std::size_t kPendingSymbolCapacity = 16384;
} // namespace

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

static_assert(core::layout_32bit(offsetof(StoredRandom, num_) == kStoredRandomNumOffset), "layout must match the original");
static_assert(core::layout_32bit(offsetof(StoredRandom, order_) == kStoredRandomOrderOffset), "layout must match the original");
static_assert(core::layout_32bit(offsetof(StoredRandom, last_) == kStoredRandomLastOffset && offsetof(StoredRandom, succ_) == kStoredRandomSuccOffset),
              "layout must match the original");
static_assert(core::layout_32bit(sizeof(StoredRandom) == kStoredRandomBytes), "layout must match the original");

namespace {

// The table of the .qb files' own checksum names: {checksum, name, next}, the first entry in the bucket.
struct ScriptChecksumName {
    uint32_t checksum;
    char* name;
    ScriptChecksumName* next;
};
constexpr size_t kLabelBucketBytes = 12; // the original's bucket
static_assert(core::layout_32bit(sizeof(ScriptChecksumName) == kLabelBucketBytes), "layout must match the original");
ScriptChecksumName*& script_label_table()
{
    static ScriptChecksumName* script_labels{};
    return script_labels;
}
// The table's 0x1000 buckets (the checksums' low 12 bits; the first entry of each in place).
constexpr uint32_t kLabelBuckets = 0x1000;
// (the buckets hold pointers: the table is sized by the record - 12 bytes a bucket on x86, 24 on x64)
constexpr auto kLabelTableBytes = static_cast<unsigned int>(kLabelBuckets * sizeof(ScriptChecksumName));
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
    if (!memory::manager_ready_flag()) {
        memory::ensure_manager();
    }
    void* memory = memory::Hub::get_pull()->carve(sizeof(memory::SlabBank), 1, nullptr);
    memory::SlabBank* pool = memory ? memory::construct_in<memory::SlabBank>(memory, item_size, count, name) : nullptr;
    core::at(pools.pools(), pools.current()) = pool;
    core::at(pools.made_here(), pools.current()) = 1;
}

// 0x0046F690
Tokens step_over_token(Tokens token)
{
    switch (as_token(*token)) {
    case QbToken::kEndOfLine:
    case QbToken::kStructStart:
    case QbToken::kStructEnd:
    case QbToken::kArrayStart:
    case QbToken::kArrayEnd:
    case QbToken::kEquals:
    case QbToken::kDot:
    case QbToken::kComma:
    case QbToken::kMinus:
    case QbToken::kPlus:
    case QbToken::kDivide:
    case QbToken::kMultiply:
    case QbToken::kOpenParenthesis:
    case QbToken::kCloseParenthesis:
    case QbToken::kEqualTo:
    case QbToken::kLessThan:
    case QbToken::kLessThanOrEqual:
    case QbToken::kGreaterThan:
    case QbToken::kGreaterThanOrEqual:
    case QbToken::kLoopStart:
    case QbToken::kLoopRepeat:
    case QbToken::kLoopBreak:
    case QbToken::kScriptStart:
    case QbToken::kScriptEnd:
    case QbToken::kIf:
    case QbToken::kElse:
    case QbToken::kUnknown27:
    case QbToken::kEndIf:
    case QbToken::kReturn:
    case QbToken::kAllArguments:
    case QbToken::kLocalArgument:
    case QbToken::kRandomRange:
    case QbToken::kOr:
    case QbToken::kAnd:
    case QbToken::kXor:
    case QbToken::kUnknown35:
    case QbToken::kUnknown36:
    case QbToken::kRandomRangeSideStream:
    case QbToken::kNot:
    case QbToken::kUnknown3A:
    case QbToken::kUnknown3B:
    case QbToken::kSwitch:
    case QbToken::kEndSwitch:
    case QbToken::kCase:
    case QbToken::kDefault:
    case QbToken::kColon:
    case QbToken::kSectionMarker:
        return token + 1;
    case QbToken::kEndOfLineNumbered:
    case QbToken::kName:
    case QbToken::kInteger:
    case QbToken::kHexInteger:
    case QbToken::kFloat:
    case QbToken::kLongJump:
    case QbToken::kNativeFunction:
    case QbToken::kMemberFunctionName:
    case QbToken::kMemberFunction:
    case QbToken::kTsBody: // (NOT ORIGINAL)
        return token + kTokenWithWordBytes;
    case QbToken::kString:
    case QbToken::kLocalString: // strings: the length, then the text
        return token + kTokenWithWordBytes + fetch_word32(token + 1);
    case QbToken::kVector: // vector
        return token + kTokenWithVectorBytes;
    case QbToken::kPair: // pair
        return token + kTokenWithPairBytes;
    case QbToken::kChecksumName: { // a checksum's name: the checksum, then the text
        Tokens cursor = token + kTokenWithWordBytes;
        while (*cursor != 0U) {
            ++cursor;
        }
        return cursor + 1;
    }
    case QbToken::kRandom:
    case QbToken::kRandomSideStream:
    case QbToken::kRandomNoRepeat:
    case QbToken::kRandomPermute: // tables of 6-byte entries
        return token + kTokenWithWordBytes + fetch_word32(token + 1) * kJumpTableEntryBytes;
    case QbToken::kShortIf:
    case QbToken::kShortElse:
    case QbToken::kShortJump:
        return token + 3;
    default: // (0x00, 0x10, 0x19, 0x1D, 0x2A, 0x31 and unknown tokens: no step)
        return token;
    }
}

// 0x0046F7E0
uint32_t fetch_word32(Tokens bytes)
{
    return bytes[0] | (bytes[1] << kShiftByte1) | (bytes[2] << kShiftByte2) |
           (static_cast<uint32_t>(bytes[3]) << kShiftByte3);
}

// 0x0046F800
uint16_t fetch_word16(Tokens bytes) { return static_cast<uint16_t>(bytes[0] | (bytes[1] << kShiftByte1)); }

// 0x0046F810
MutableTokens store_word32(MutableTokens bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> kShiftByte1);
    bytes[2] = static_cast<uint8_t>(value >> kShiftByte2);
    bytes[3] = static_cast<uint8_t>(value >> kShiftByte3);
    return bytes + 4;
}

// 0x0046F840
MutableTokens store_word16(MutableTokens bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> kShiftByte1);
    return bytes + 2;
}

// 0x0046F850
Tokens skip_newline_tokens(Tokens token)
{
    for (;;) {
        if (as_token(*token) == QbToken::kEndOfLine) {
            token += 1;
        } else if (as_token(*token) == QbToken::kEndOfLineNumbered) {
            token += kTokenWithWordBytes;
        } else {
            return token;
        }
    }
}

// 0x0046FB30
Tokens skip_to_end_of_statement(Tokens token)
{
    int braces = 0;
    int brackets = 0;
    int parentheses = 0;
    for (;;) {
        const QbToken kind = as_token(*token);
        if (kind == QbToken::kStructStart) {
            ++braces;
        }
        if (kind == QbToken::kStructEnd) {
            --braces;
        }
        if (kind == QbToken::kArrayStart) {
            ++brackets;
        }
        if (kind == QbToken::kArrayEnd) {
            --brackets;
        }
        if (kind == QbToken::kOpenParenthesis) {
            ++parentheses;
        }
        if (kind == QbToken::kCloseParenthesis) {
            --parentheses;
        }
        if (braces == 0 && brackets == 0 && parentheses == 0 &&
            (kind == QbToken::kEndOfLine || kind == QbToken::kEndOfLineNumbered || kind == QbToken::kEndOfFile)) {
            return step_over_token(token);
        }
        token = step_over_token(token);
    }
}

// 0x0046F920
Tokens skip_to_end_of_script(Tokens token)
{
    while (as_token(*token) != QbToken::kScriptEnd) {
        token = step_over_token(token);
    }
    token = step_over_token(token);
    // (a script whose '$' ends its block - a structure's script body, a private copy - has no line number after it:
    // the original read the byte past the block)
    if (token.remaining() > 0 && as_token(*token) == QbToken::kEndOfLineNumbered) {
        return step_over_token(token);
    }
    return token;
}

// 0x0046F960
uint32_t calculate_script_checksum(Tokens token)
{
    uint32_t checksum = kChecksumInitial;
    while (as_token(*token) != QbToken::kScriptEnd) {
        const Tokens next = step_over_token(token);
        if (as_token(*token) != QbToken::kEndOfLine && as_token(*token) != QbToken::kEndOfLineNumbered) {
            const std::span<const uint8_t> bytes = code_between(token, next);
            checksum = checksums::extend_crc_with_buffer(bytes, checksum);
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
    while (as_token(*token) != QbToken::kScriptEnd) {
        if (as_token(*token) == QbToken::kName) {
            // A statement starting with a name: a call?
            const MutableTokens name(code, token.position());
            token += kTokenWithWordBytes;
            if (const SymbolTableEntry* symbol = resolve_symbol(fetch_word32(name + 1))) {
                if (symbol->type() == QbValueKind::kNativeFunc) {
                    name[0] = token_byte(QbToken::kNativeFunction);
                    // (NOT ORIGINAL: the function's number, not its address - names.h)
                    store_word32(name + 1, native_function_number(symbol->value().function()));
                } else if (symbol->type() == QbValueKind::kNativeMethod) {
                    if (!symbol->value().empty()) {
                        store_word32(name + 1, member_entry_number(symbol->value().method())); // (NOT ORIGINAL: its number)
                        name[0] = token_byte(QbToken::kMemberFunction);
                    } else {
                        name[0] = token_byte(QbToken::kMemberFunctionName);
                    }
                }
            }
            token = skip_to_end_of_statement(token);
        } else if (as_token(*token) == QbToken::kShortIf) {
            token = step_over_token(token);
            if (as_token(*token) == QbToken::kNot) {
                ++token;
            }
        } else {
            token = skip_to_end_of_statement(token);
        }
    }
}

// 0x0046F9B0
const char* find_script_checksum_name(uint32_t checksum)
{
    if (!script_label_table()) {
        return nullptr;
    }
    for (const ScriptChecksumName* entry = &core::at(label_buckets(), checksum & kLabelBucketMask); entry; entry = entry->next) {
        if (entry->checksum == checksum) {
            return entry->name;
        }
    }
    return nullptr;
}

// 0x0046F9F0
void free_script_checksum_names()
{
    if (!script_label_table()) {
        return;
    }
    for (const ScriptChecksumName& bucket : label_buckets()) {
        for (ScriptChecksumName* entry = bucket.next; entry;) {
            ScriptChecksumName* next = entry->next;
            memory::free_malloc(entry->name);
            memory::free_malloc(entry);
            entry = next;
        }
        if (bucket.name) {
            memory::free_malloc(bucket.name);
        }
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
    if (keep_qb_names() && name && (*name != '\0')) {
        kept_names()[checksum] = name;
    }
}

const char* kept_qb_name(uint32_t checksum)
{
    auto it = kept_names().find(checksum);
    return it == kept_names().end() ? nullptr : it->second.c_str();
}

// 0x0046FA60 (a checksum already there is added again)
void add_script_checksum_name(uint32_t checksum, const char* name)
{
    ScriptChecksumName& bucket = core::at(label_buckets(), checksum & kLabelBucketMask);
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
    for (StoredRandom* entry = script_state().stored_randoms_newest(); entry; entry = entry->succ_) {
        if (entry->owner_ == owner) {
            entry->owner_ = nullptr;
        }
    }
}

// 0x0046F8A0
void shuffle(StoredRandom& random, bool avoid_repeat)
{
    const uint8_t last = core::at(random.order_, random.num_ - 1);
    for (int swaps = random.num_ * kShuffleSwapsPerChoice; swaps != 0; --swaps) {
        const int first = maths::rnd(random.num_);
        const int second = maths::rnd(random.num_);
        const uint8_t swapped = core::at(random.order_, first);
        core::at(random.order_, first) = core::at(random.order_, second);
        core::at(random.order_, second) = swapped;
    }
    if (avoid_repeat && core::at(random.order_, 0) == last) {
        const int other = maths::rnd(random.num_ - 1) + 1;
        const uint8_t swapped = core::at(random.order_, 0);
        core::at(random.order_, 0) = core::at(random.order_, other);
        core::at(random.order_, other) = swapped;
    }
}

// 0x00476210
void allocate_permanent_string_heap(const memory::sizes::Pools& sizes)
{
    const uint32_t size = sizes.string_bytes_;
    const uint32_t count = sizes.strings;
    script_state().set_permanent_strings_size(size);
    script_state().set_permanent_string_capacity(count);
    char* strings = static_cast<char*>(memory::alloc_block(size));
    script_state().set_permanent_strings_start(strings);
    script_state().set_permanent_strings_next(strings);
    memory::bits_as<void*>(script_state().permanent_string_table()) = memory::alloc_block(count * kPermanentStringEntryBytes);
    script_state().permanent_string_count() = 0;
}

// 0x0046F520
void create_script_pools()
{
    // Not original - the counts are ours (engine/memory/memory_sizes.h: pool sizes as THUGPRO's, thug2-decomp.ini
    // [memory]); the script region they come from is grown to match (memory::Hub::InitStandardHeaps).
    // Original behaviour: CScriptCacheEntry 300, CComponent 86000, CStruct 19800, CVector 9500, CPair 800, CArray
    // 7000, CSymbolTableEntry 12500, CScript 300, CStoredRandom 100, the string heap 190000 bytes / 7000 strings.
    const memory::sizes::Pools& sizes = memory::sizes::at_hand();
    memory::push_mem_profile("AllocateChecksumNameLookupTables");
    allocate_checksum_name_lookup_tables();
    memory::pop_mem_profile();
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    // (0x0046F090, 0x0046F110, 0x0046F190, 0x0046F210, 0x0046F290, 0x0046F310, 0x0046F390, 0x0046F410,
    // 0x0046F4A0: one function per class)
    memory::push_mem_profile("CScriptCacheEntry");
    create_pool(script_state().cache_entry_pools(), kScriptCacheEntryPoolBytes, sizes.script_cache_entries_, "CScriptCacheEntry");
    memory::pop_mem_profile();
    memory::push_mem_profile("CComponent and Reserve CComponent");
    create_pool(component_pools(), kComponentPoolBytes, sizes.components, "CComponent");
    memory::pop_mem_profile();
    memory::push_mem_profile("CStruct");
    create_pool(struct_pools(), kStructPoolBytes, sizes.structs, "CStruct");
    memory::pop_mem_profile();
    memory::push_mem_profile("CVector");
    create_pool(vector_pools(), kVectorPoolBytes, sizes.vectors, "CVector");
    memory::pop_mem_profile();
    memory::push_mem_profile("CPair");
    create_pool(script_state().pair_pools(), kPairPoolBytes, sizes.pairs, "CPair");
    memory::pop_mem_profile();
    memory::push_mem_profile("CArray");
    create_pool(array_pools(), kArrayPoolBytes, sizes.arrays, "CArray");
    memory::pop_mem_profile();
    memory::push_mem_profile("CSymbolTableEntry");
    create_pool(symbol_pools(), kSymbolPoolBytes, sizes.symbols, "CSymbolTableEntry");
    memory::pop_mem_profile();
    memory::push_mem_profile("CScript");
    create_pool(script_pools(), kScriptPoolBytes, sizes.scripts, "CScript");
    memory::pop_mem_profile();
    memory::push_mem_profile("CStoredRandom");
    create_pool(script_state().stored_random_pools(), kStoredRandomPoolBytes, sizes.stored_randoms_, "CStoredRandom");
    memory::pop_mem_profile();
    memory::push_mem_profile("AllocatePermanentStringHeap");
    allocate_permanent_string_heap(sizes);
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
    const auto pools = std::to_array<Named>({{.name = "CComponent", .pair = &component_pools()}, {.name = "CStruct", .pair = &struct_pools()},
                           {.name = "CVector", .pair = &vector_pools()},       {.name = "CPair", .pair = &script_state().pair_pools()},
                           {.name = "CArray", .pair = &array_pools()},         {.name = "CSymbolTableEntry", .pair = &symbol_pools()},
                           {.name = "CScript", .pair = &script_pools()}});
    std::string line;
    for (const Named& named : pools) {
        int used = 0;
        int peak = 0;
        int count = 0;
        for (const memory::SlabBank* pool : named.pair->pools()) {
            if (!pool) {
                continue;
            }
            used += pool->taken_count();
            peak += pool->peak_used();
            count += pool->num();
        }
        constexpr size_t kPartSize = 96;
        std::array<char, kPartSize> part{};
        text::format_to(part, "%s%s %d (peak %d) of %d", line.empty() ? "" : ", ", named.name, used, peak, count);
        line += part.data();
    }
    // (and the hash tables' items, Main's HashItem pool - memory_sizes.h hash_item_pool)
    if (const memory::SlabBank* items = memory::slot_pool_setup::get_pool(0)) {
        constexpr size_t kPartSize = 96;
        std::array<char, kPartSize> part{};
        text::format_to(part, ", HashItem %d (peak %d) of %d", items->taken_count(), items->peak_used(), items->num());
        line += part.data();
    }
    get_log("script pools in use %s: %s", when, line.c_str());
}

} // namespace script

// --- symbol bookkeeping for the loader --------------------------------------------------------------------

namespace script {

// The script cache (0x006F3558, or null): the scripts' decompressed code, by name.
ScriptStore* script_store_instance() { return script_state().script_store().get(); }

void remove_from_script_cache(uint32_t name) { script_store_instance()->mark_stale(name); }

namespace {

// Symbols waiting to be removed (or replaced) once a .qb file has been loaded: {symbol, owned}.
struct PendingSymbol {
    SymbolTableEntry* symbol;
    bool owned;
};
std::array<PendingSymbol, kPendingSymbolCapacity>& pending_symbols()
{
    static core::NoDestroy<std::array<PendingSymbol, kPendingSymbolCapacity>> pending_symbols;
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
    if (symbol->type() == QbValueKind::kScriptBody && script_store_instance()) {
        remove_from_script_cache(symbol->name_value());
    }
}

} // namespace

// 0x00470000 / 0x0046FFF0
void clear_script_flag_6f_2564() { script_state().evaluator().set_unknown1f4(false); } // (0x006F2564: the evaluator's +0x1F4)
void set_script_flag_6f_2564() { script_state().evaluator().set_unknown1f4(true); }

// 0x00470010
void allocate_script_checksum_names(bool debug_heap)
{
    const memory::Hub* manager = memory::Hub::get_pull();
    memory::enter_heap_scope(debug_heap ? manager->debug_heap() : manager->script_heap());
    auto* names = static_cast<uint32_t*>(memory::alloc_block(kLabelTableBytes));
    memory::bits_as<uint32_t*>(script_label_table()) = names;
    memory::leave_heap_scope();
    std::ranges::fill(std::span<uint32_t>(names, kLabelTableBytes / sizeof(uint32_t)), 0U);
}

// 0x0044C1D0 / 0x0044C230 / 0x00470070 / 0x004700D0
void free_pair(void* pair) { script_state().pair_pools().dealloc(pair); }
void free_vector(void* vector) { vector_pools().dealloc(vector); }
void free_stored_random(void* random) { script_state().stored_random_pools().dealloc(random); }
void free_symbol(SymbolTableEntry* symbol) { symbol_pools().dealloc(symbol); }

// 0x004711D0
void clean_up_and_remove_symbol(SymbolTableEntry* symbol)
{
    unlink_symbol(symbol);
    if ((symbol->unknown0() & 4) != 0) { // (kept: not owned by the table?)
        return;
    }
    switch (symbol->type()) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        free_string(symbol->value().text());
        break;
    case QbValueKind::kPair:
        free_pair(symbol->value().pair());
        break;
    case QbValueKind::kVector:
        free_vector(symbol->value().vector());
        break;
    case QbValueKind::kScriptBody:
        memory::free_malloc(symbol->value().code());
        break;
    case QbValueKind::kStruct:
        if (Struct* structure = symbol->value().structure()) {
            structure->get_clear();
            delete_struct(structure);
        }
        break;
    case QbValueKind::kArray:
        release_array_storage(symbol->value().array());
        if (auto* array = symbol->value().array()) {
            array->get_clear();
            delete_array(array);
        }
        break;
    default:
        break;
    }
    symbol->value().clear();
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
            clean_up_and_remove_symbol(find_symbol(pending.symbol->name_value()));
            link_symbol(pending.symbol);
        }
        // (reads the symbol even when it has just been freed, as the original does)
        if (pending.symbol->type() == QbValueKind::kScriptBody && script_store_instance()) {
            remove_from_script_cache(pending.symbol->name_value());
        }
    }
    pending_symbol_count() = 0;
}

} // namespace script
