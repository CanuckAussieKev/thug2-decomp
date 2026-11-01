#include "engine/script/names.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/script/param_struct.h"
#include "engine/script/cfuncs_scene.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag

namespace script {

static char g_checksum_name_buffer[100];
static char* g_names_start;
static char* g_names_next;

namespace {

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
    int value = c;
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
    HashedLabel* entry = get_bucket(checksum);
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
    char* buffer = g_checksum_name_buffer;
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
        char name[256];
        int length = 0;
        while (*text != '\r')
            name[length++] = *text++;
        name[length] = 0;
        add(checksum, name);
        text += 2; // "\r\n"
    }
}

// 0x0046CFE0
void load_checksum_names(const char* file)
{
    if (!checksum_names_enabled())
        return;
    char name[260];
    std::memcpy(name, file, std::strlen(file) + 1);
    char* extension = name;
    while (*extension && *extension != '.')
        ++extension;
    std::memcpy(extension, ".chk", 5);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    auto* text = static_cast<const char*>(packs::read_in(name, false, nullptr, nullptr, nullptr));
    memory::leave_heap_scope();
    parse_checksum_names(text, packs::get_file_size(name), register_name_hash);
    packs::offload(name);
}

// 0x0046EFD0
void install_script_functions(const NativeCommand* functions, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t checksum = checksums::hash_from_text(functions[i].name);
        register_name_hash(checksum, functions[i].name);
        SymbolTableEntry* symbol = create_symbol(checksum);
        symbol->type = kQbvalNativeFunc;
        symbol->raw = reinterpret_cast<uint32_t>(functions[i].function);
    }
}

// 0x0046F020
void publish_method_table(const char* const* names, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t checksum = checksums::hash_from_text(names[i]);
        register_name_hash(checksum, names[i]);
        create_symbol(checksum)->type = kQbvalNativeMethod;
    }
}

// 0x0046F060
void register_member_function_table(void*, const uint32_t* records, int stride)
{
    while (*records) {
        SymbolTableEntry* symbol = create_symbol(*records);
        symbol->raw = reinterpret_cast<uint32_t>(records);
        records = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(records) + stride);
        symbol->type = kQbvalNativeMethod;
    }
}

void fill_member_function_code(uint32_t* records, const MemberCode* code, int count)
{
    for (int i = 0; i < count; ++i) {
        uint32_t* record = records + 6 * i;
        record[2] = reinterpret_cast<uint32_t>(code[i]);
        record[3] = 0;
        record[4] = 0;
        record[5] = 0;
    }
    uint32_t* terminator = records + 6 * count;
    terminator[2] = 0;
    terminator[3] = 0;
    terminator[4] = 0;
    terminator[5] = 0xFFFFFFFFu;
}

} // namespace script
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
