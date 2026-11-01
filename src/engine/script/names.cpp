#include "engine/script/names.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/script/param_struct.h"
#include "engine/script/cfuncs_scene.h"
#include "platform/code_address.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"


namespace script {

namespace {
std::array<char, 100> g_checksum_name_buffer;
char* g_names_start;
char* g_names_next;

struct HashedLabel {
    uint32_t checksum;
    const char* name;
};
constexpr uint32_t kBucketSize = 10;

HashedLabel* g_checksum_name_table;
HashedLabel*& name_table() { return g_checksum_name_table; }
char*& names_start() { return g_names_start; }
char*& names_next() { return g_names_next; }

HashedLabel* get_bucket(uint32_t checksum) { return name_table() + (checksum & 0xFFFF) * kBucketSize; }

// A hex digit's value (anything else counts as its character code, as in the original).
int hex_digit(char c)
{
    const int value = int{c};
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return value;
}

} // namespace

bool checksum_names_enabled() { return commands::g_got_extra_memory; }

// 0x0046CF00
void allocate_checksum_name_lookup_tables()
{
    if (!checksum_names_enabled())
        return;
    name_table() = static_cast<HashedLabel*>(memory::alloc_block(0x10000 * kBucketSize * sizeof(HashedLabel)));
    char* names = static_cast<char*>(memory::alloc_block(5000000));
    names_start() = names;
    names_next() = names;
    for (uint32_t i = 0; i < 0x10000 * kBucketSize; ++i)
        name_table()[i] = {0, nullptr};
    names[0] = 0;
}

// 0x0046CF60
void register_name_hash(uint32_t checksum, const char* name)
{
    if (!checksum_names_enabled())
        return;
    // (a full bucket runs on into the next one, as in the original)
    HashedLabel* entry = get_bucket(checksum);
    for (; entry->name; ++entry) {
        if (entry->checksum == checksum) {
            // (the original compares the names here - a checksum clash check whose result is ignored)
            return;
        }
    }
    entry->checksum = checksum;
    char* copy = names_next();
    entry->name = copy;
    while (*name)
        *copy++ = *name++;
    *copy++ = 0;
    names_next() = copy;
}

// 0x0046D0C0
const char* look_up_hash_name(uint32_t checksum)
{
    if (!checksum_names_enabled())
        return nullptr;
    const HashedLabel* entry = get_bucket(checksum);
    for (uint32_t i = 0; i < kBucketSize && entry->name; ++i, ++entry) {
        if (entry->checksum == checksum)
            return entry->name;
    }
    return nullptr;
}

// 0x0046D110
const char* find_checksum_name_or_unknown(uint32_t checksum)
{
    if (const char* name = look_up_hash_name(checksum))
        return name;
    char* buffer = g_checksum_name_buffer.data();
    std::snprintf(buffer, 32, "Unknown(0x%08x)", checksum);
    return buffer;
}

// 0x0046CD90
void parse_checksum_names(const char* text, int size, void (*add)(uint32_t checksum, const char* name))
{
    const char* end = text + size;
    while (text != end) {
        text += 2; // "0x"
        uint32_t checksum = 0;
        for (int i = 0; i < 8; ++i)
            checksum = (checksum << 4) + static_cast<uint32_t>(hex_digit(text[i]));
        text += 8 + 1; // and the separator
        std::array<char, 256> name{};
        int length = 0;
        while (*text != '\r')
            core::at(name, length++) = *text++;
        core::at(name, length) = 0;
        add(checksum, name.data());
        text += 2; // "\r\n"
    }
}

// 0x0046CFE0
void load_checksum_names(const char* file)
{
    if (!checksum_names_enabled())
        return;
    std::array<char, 260> name{};
    std::memcpy(name.data(), file, std::strlen(file) + 1);
    char* extension = name.data();
    while (*extension && *extension != '.')
        ++extension;
    std::memcpy(extension, ".chk", 5);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    auto* text = static_cast<const char*>(packs::read_in(name.data(), false, nullptr, nullptr, nullptr));
    memory::leave_heap_scope();
    parse_checksum_names(text, packs::get_file_size(name.data()), register_name_hash);
    packs::offload(name.data());
}

// 0x0046EFD0
void install_script_functions(const NativeCommand* functions, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t checksum = checksums::hash_from_text(functions[i].name);
        register_name_hash(checksum, functions[i].name);
        SymbolTableEntry* symbol = create_symbol(checksum);
        symbol->type = kQbvalNativeFunc;
        symbol->value = Cell::of(functions[i].function);
    }
}

// 0x0046F020
void publish_method_table(const char* const* names, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t checksum = checksums::hash_from_text(names[i]);
        register_name_hash(checksum, names[i]);
        create_symbol(checksum)->type = kQbvalNativeMethod;
    }
}

// 0x0046F060
void register_member_function_table(std::span<const MemberFunctionEntry> records)
{
    for (const MemberFunctionEntry& record : records) {
        if (!record.callsign_)
            break;
        SymbolTableEntry* symbol = create_symbol(record.callsign_);
        symbol->value = Cell::of(&record);
        symbol->type = kQbvalNativeMethod;
    }
}

void fill_member_function_code(std::span<MemberFunctionEntry> records, const MemberCode* code, int count)
{
    for (int i = 0; i < count; ++i) {
        MemberFunctionEntry& record = core::at(records, i);
        record.method_code_ = code[i];
        record.this_adjustment_ = 0;
        record.virtual_base_offsets_ = {0, 0};
    }
    MemberFunctionEntry& terminator = core::at(records, count);
    terminator.method_code_ = nullptr;
    terminator.this_adjustment_ = 0;
    terminator.virtual_base_offsets_ = {0, -1};
}

} // namespace script
