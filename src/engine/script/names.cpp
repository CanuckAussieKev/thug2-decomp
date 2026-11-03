#include "engine/script/names.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <span>
#include <string_view>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/script/cscript.h"
#include "engine/script/member_code.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/sys/game_config.h"

namespace script {

namespace {
// NOT ORIGINAL grouping - names.cpp's state (the original's globals). Made on first use, never destroyed
// (names_state()).
struct NamesState {
    std::array<char, 100> checksum_name_buffer_{};
    char* names_start_{};
    std::size_t names_used_ = 0; // the bytes of the names' block taken
};

NamesState& names_state()
{
    static core::NoDestroy<NamesState> state;
    return state.get();
}

constexpr std::size_t kNamesBytes = 5000000;

struct HashedLabel {
    uint32_t checksum;
    const char* name;
};
constexpr uint32_t kBucketSize = 10;

constexpr std::size_t kTableEntries = 0x10000 * static_cast<std::size_t>(kBucketSize);
HashedLabel*& checksum_name_table()
{
    static HashedLabel* checksum_name_table{};
    return checksum_name_table;
}
std::span<HashedLabel> name_table() { return {checksum_name_table(), kTableEntries}; }
char*& names_start() { return names_state().names_start_; }
std::span<char> names_block() { return {names_state().names_start_, kNamesBytes}; }

// The table from a checksum's bucket to its end (a full bucket runs on into the next one).
std::span<HashedLabel> get_bucket(uint32_t checksum)
{
    return name_table().subspan(static_cast<std::size_t>(checksum & 0xFFFF) * kBucketSize);
}

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

bool checksum_names_enabled() { return game_config::get_extra_memory(); }

// 0x0046CF00
void allocate_checksum_name_lookup_tables()
{
    if (!checksum_names_enabled())
        return;
    checksum_name_table() = static_cast<HashedLabel*>(memory::alloc_block(0x10000 * kBucketSize * sizeof(HashedLabel)));
    names_start() = static_cast<char*>(memory::alloc_block(kNamesBytes));
    names_state().names_used_ = 0;
    for (HashedLabel& label : name_table())
        label = {.checksum = 0, .name = nullptr};
    core::at(names_block(), 0) = 0;
}

// 0x0046CF60
void register_name_hash(uint32_t checksum, const char* name)
{
    if (!checksum_names_enabled())
        return;
    // (a full bucket runs on into the next one, as in the original)
    const std::span<HashedLabel> bucket = get_bucket(checksum);
    std::size_t at = 0;
    for (; core::at(bucket, at).name; ++at) {
        if (core::at(bucket, at).checksum == checksum) {
            // (the original compares the names here - a checksum clash check whose result is ignored)
            return;
        }
    }
    HashedLabel& entry = core::at(bucket, at);
    entry.checksum = checksum;
    const std::string_view text(name);
    const std::span<char> copy = names_block().subspan(names_state().names_used_, text.size() + 1);
    entry.name = copy.data();
    std::ranges::copy(text, copy.begin());
    core::at(copy, text.size()) = 0;
    names_state().names_used_ += text.size() + 1;
}

// 0x0046D0C0
const char* look_up_hash_name(uint32_t checksum)
{
    if (!checksum_names_enabled())
        return nullptr;
    const std::span<HashedLabel> bucket = get_bucket(checksum);
    for (uint32_t i = 0; i < kBucketSize && core::at(bucket, i).name; ++i) {
        if (core::at(bucket, i).checksum == checksum)
            return core::at(bucket, i).name;
    }
    return nullptr;
}

// 0x0046D110
const char* find_checksum_name_or_unknown(uint32_t checksum)
{
    if (const char* name = look_up_hash_name(checksum))
        return name;
    text::format_to(std::span(names_state().checksum_name_buffer_).first(32), "Unknown(0x%08x)", checksum);
    return names_state().checksum_name_buffer_.data();
}

// 0x0046CD90
void parse_checksum_names(const char* text, int size, void (*add)(uint32_t checksum, const char* name))
{
    const std::span<const char> file(text, static_cast<std::size_t>(size));
    core::Cursor<const char> at(file);
    while (at.position() != size) {
        at += 2; // "0x"
        uint32_t checksum = 0;
        for (int i = 0; i < 8; ++i)
            checksum = (checksum << 4) + static_cast<uint32_t>(hex_digit(at[i]));
        at += 8 + 1; // and the separator
        std::array<char, 256> name{};
        int length = 0;
        while (*at != '\r')
            core::at(name, length++) = *at++;
        core::at(name, length) = 0;
        add(checksum, name.data());
        at += 2; // "\r\n"
    }
}

// 0x0046CFE0
void load_checksum_names(const char* file)
{
    if (!checksum_names_enabled())
        return;
    std::array<char, 260> name{};
    std::memcpy(name.data(), file, std::strlen(file) + 1);
    std::size_t extension = 0;
    while (core::at(name, extension) && core::at(name, extension) != '.')
        ++extension;
    std::memcpy(std::span<char>(name).subspan(extension).data(), ".chk", 5);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    auto* text = static_cast<const char*>(packs::read_in(name.data(), false, nullptr, nullptr, nullptr));
    memory::leave_heap_scope();
    parse_checksum_names(text, packs::get_file_size(name.data()), register_name_hash);
    packs::offload(name.data());
}

// 0x0046EFD0
void install_script_functions(std::span<const NativeCommand> functions)
{
    for (const NativeCommand& function : functions) {
        const uint32_t checksum = checksums::hash_from_text(function.name);
        register_name_hash(checksum, function.name);
        SymbolTableEntry* symbol = create_symbol(checksum);
        symbol->set_type(kQbvalNativeFunc);
        symbol->value() = Cell::of(function.function);
    }
}

// 0x0046F020
void publish_method_table(std::span<const char* const> names)
{
    for (const char* const name : names) {
        const uint32_t checksum = checksums::hash_from_text(name);
        register_name_hash(checksum, name);
        create_symbol(checksum)->set_type(kQbvalNativeMethod);
    }
}

// 0x0046F060
void register_member_function_table(std::span<const MemberFunctionEntry> records)
{
    for (const MemberFunctionEntry& record : records) {
        if (!record.callsign_)
            break;
        SymbolTableEntry* symbol = create_symbol(record.callsign_);
        symbol->value() = Cell::of(&record);
        symbol->set_type(kQbvalNativeMethod);
    }
}

void fill_member_function_code(std::span<MemberFunctionEntry> records, std::span<const MemberCode> code)
{
    const auto count = static_cast<int>(code.size());
    for (int i = 0; i < count; ++i) {
        MemberFunctionEntry& record = core::at(records, i);
        record.method_code_ = core::at(code, i);
        record.this_adjustment_ = 0;
        record.virtual_base_offsets_ = {0, 0};
    }
    MemberFunctionEntry& terminator = core::at(records, count);
    terminator.method_code_ = nullptr;
    terminator.this_adjustment_ = 0;
    terminator.virtual_base_offsets_ = {0, -1};
}

} // namespace script
