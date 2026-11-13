// NOT ORIGINAL - a script file's data read into the game's cells (ts_data.h; the format: tools/qbjs/qb_data.ts).
//
// Each value is made as ts_bridge.cpp js_to_cell makes the runtime's value object of it - the same allocations, in
// the same heap - so a file's globals are what its TypeScript defined before its data moved out of it.

#include "script_qb/ts_data.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/script/cell.h"
#include "engine/script/param_struct.h"

namespace script::qb::ts {

namespace {

constexpr std::string_view kFormat = "qb-data 1";
constexpr size_t kMaxDepth = 64;
constexpr std::string_view kChecksumPrefix = "_0x"; // a name written as its checksum: _0x and 8 hex digits
constexpr size_t kChecksumDigits = 8;
constexpr size_t kEscapeDigits = 4;      // \uXXXX
constexpr uint32_t kLastByte = 0xFF;     // a string's characters are bytes
constexpr uint32_t kUtf8Lead = 0x80;     // a byte from here on starts or continues a UTF-8 sequence
constexpr uint32_t kUtf8Bits = 0x3F;     // the bits a UTF-8 continuation byte carries
constexpr unsigned kUtf8Shift = 6;       // ... so many
constexpr uint32_t kUtf8Two = 0xC0;      // the lead byte of a sequence of 2
constexpr uint32_t kUtf8Three = 0xE0;    // ... of 3
constexpr uint32_t kUtf8Four = 0xF0;     // ... of 4
constexpr int64_t kLowestInteger = -0x80000000LL; // an integer: a 32-bit word, signed or not
constexpr int64_t kHighestInteger = 0xFFFFFFFFLL;
constexpr unsigned kHexBits = 4;
constexpr uint32_t kHexLetter = 10; // 'a'

// A value read: its kind and cell (owned until handed on), or a structure's script value by its key.
struct Read {
    QbValueKind kind = QbValueKind::kNone;
    Cell cell;
    std::string script;
};

std::optional<uint32_t> hex_digit(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return static_cast<uint32_t>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
        return static_cast<uint32_t>(digit - 'a') + kHexLetter;
    }
    if (digit >= 'A' && digit <= 'F') {
        return static_cast<uint32_t>(digit - 'A') + kHexLetter;
    }
    return std::nullopt;
}

// Hex digits' value (none: not all hex, or none at all).
std::optional<uint32_t> hex_value(std::string_view digits)
{
    uint32_t value = 0;
    for (const char digit : digits) {
        const std::optional<uint32_t> nibble = hex_digit(digit);
        if (!nibble) {
            return std::nullopt;
        }
        value = (value << kHexBits) | *nibble;
    }
    return digits.empty() ? std::nullopt : std::optional<uint32_t>(value);
}

// A name's checksum from its text: `_0x` and 8 hex digits is the checksum itself (as the TypeScript's N._0x...).
uint32_t name_checksum(const std::string& text)
{
    if (text.size() == kChecksumPrefix.size() + kChecksumDigits && text.starts_with(kChecksumPrefix)) {
        if (const std::optional<uint32_t> value = hex_value(std::string_view(text).substr(kChecksumPrefix.size()))) {
            return *value;
        }
    }
    return checksums::hash_from_text(text.c_str());
}

// The bytes a UTF-8 sequence's lead byte says follow it (none: not a lead byte).
std::optional<unsigned> utf8_following(uint32_t lead)
{
    if (lead >= kUtf8Four) {
        return 3U;
    }
    if (lead >= kUtf8Three) {
        return 2U;
    }
    if (lead >= kUtf8Two) {
        return 1U;
    }
    return std::nullopt;
}

class Reader {
public:
    Reader(std::string_view json, const DataHooks& hooks, std::string& error)
        : json_(json), hooks_(&hooks), error_(&error)
    {
    }

    bool file(std::string* from)
    {
        bool format = false;
        if (!expect('{', "a data file is an object")) {
            return false;
        }
        if (take('}')) {
            return fail("no format");
        }
        for (;;) {
            std::string key;
            if (!string(key) || !expect(':', "':' after a key")) {
                return false;
            }
            if (key == "format" || key == "source" || key == "from") {
                std::string text;
                if (!string(text)) {
                    return false;
                }
                if (key == "format" && text != kFormat) {
                    return fail("not a qb-data 1 file");
                }
                format = format || key == "format";
                if (key == "from" && from) {
                    *from = text;
                }
            } else if (key != "globals") {
                return fail("an unknown key");
            } else if (!format) {
                return fail("globals before the format");
            } else if (!globals()) {
                return false;
            }
            if (!take(',')) {
                return expect('}', "'}' at the file's end");
            }
        }
    }

private:
    std::string_view json_;
    size_t at_ = 0;
    const DataHooks* hooks_;
    std::string* error_;

    bool fail(const std::string& what)
    {
        if (error_->empty()) {
            *error_ = what + " (at byte " + std::to_string(at_) + ")";
        }
        return false;
    }
    [[nodiscard]] bool more() const { return at_ < json_.size(); }
    [[nodiscard]] char here() const { return json_.at(at_); }
    void space()
    {
        while (more() && (here() == ' ' || here() == '\t' || here() == '\n' || here() == '\r')) {
            ++at_;
        }
    }
    bool peek(char wanted)
    {
        space();
        return more() && here() == wanted;
    }
    bool take(char wanted)
    {
        if (!peek(wanted)) {
            return false;
        }
        ++at_;
        return true;
    }
    bool expect(char wanted, const char* what) { return take(wanted) || fail(what); }
    bool word(std::string_view text)
    {
        space();
        if (json_.substr(at_, text.size()) != text) {
            return false;
        }
        at_ += text.size();
        return true;
    }

    // An escape's character (after its backslash).
    bool escape(uint32_t& character)
    {
        if (!more()) {
            return fail("an unended string");
        }
        const char kind = json_.at(at_++);
        switch (kind) {
        case 'n':
            character = '\n';
            return true;
        case 't':
            character = '\t';
            return true;
        case 'r':
            character = '\r';
            return true;
        case 'b':
            character = '\b';
            return true;
        case 'f':
            character = '\f';
            return true;
        case 'u': {
            const std::optional<uint32_t> code = hex_value(json_.substr(at_, kEscapeDigits));
            if (!code || at_ + kEscapeDigits > json_.size()) {
                return fail("a bad \\u escape");
            }
            at_ += kEscapeDigits;
            character = *code;
            return true;
        }
        default:
            character = static_cast<uint8_t>(kind); // \" \\ \/
            return true;
        }
    }
    // A UTF-8 sequence's character (after its lead byte), as an editor saves one.
    bool utf8(uint32_t& character)
    {
        const std::optional<unsigned> following = utf8_following(character);
        if (!following || at_ + *following > json_.size()) {
            return fail("bad UTF-8 in a string");
        }
        character &= kUtf8Bits >> *following;
        for (unsigned i = 0; i < *following; ++i) {
            character = (character << kUtf8Shift) | (static_cast<uint8_t>(json_.at(at_++)) & kUtf8Bits);
        }
        return true;
    }
    // A JSON string's characters as bytes: each character one byte (past ÿ is an error - QB strings are bytes).
    bool string(std::string& out)
    {
        out.clear();
        if (!expect('"', "a string")) {
            return false;
        }
        while (more() && here() != '"') {
            uint32_t character = static_cast<uint8_t>(json_.at(at_++));
            if (character == '\\' ? !escape(character) : character >= kUtf8Lead && !utf8(character)) {
                return false;
            }
            if (character > kLastByte) {
                return fail("a string's character past \\u00ff");
            }
            out.push_back(static_cast<char>(character));
        }
        return expect('"', "an unended string");
    }

    // A number's text.
    bool number(std::string& out)
    {
        space();
        const size_t start = at_;
        while (more() && (here() == '-' || here() == '+' || here() == '.' || here() == 'e' || here() == 'E' ||
                          (here() >= '0' && here() <= '9'))) {
            ++at_;
        }
        out.assign(json_.substr(start, at_ - start));
        return !out.empty() || fail("a value");
    }
    // A float's 32 bits from a number (read through a double, as JavaScript's Math.fround(Number(text))) or a "0x..."
    // string.
    bool float_bits(uint32_t& bits)
    {
        std::string text;
        if (peek('"')) {
            const std::optional<uint32_t> value =
                string(text) && text.starts_with("0x") ? hex_value(std::string_view(text).substr(2)) : std::nullopt;
            if (!value || text.size() > 2 + kChecksumDigits) {
                return fail("a float's bits are \"0x...\"");
            }
            bits = *value;
            return true;
        }
        if (!number(text)) {
            return false;
        }
        bits = std::bit_cast<uint32_t>(static_cast<float>(std::strtod(text.c_str(), nullptr)));
        return true;
    }
    bool coordinates(std::span<float> xyz)
    {
        if (!expect('[', "coordinates are a list")) {
            return false;
        }
        bool first = true;
        for (float& coordinate : xyz) {
            uint32_t bits = 0;
            if ((!first && !expect(',', "a coordinate")) || !float_bits(bits)) {
                return false;
            }
            first = false;
            coordinate = std::bit_cast<float>(bits);
        }
        return expect(']', "too many coordinates");
    }
    bool text_value(QbValueKind kind, Read& out, bool translated)
    {
        std::string text;
        if (translated) {
            if (!string(text)) {
                return false;
            }
            if (const std::optional<std::string> found = hooks_->text ? hooks_->text(text) : std::nullopt) {
                text = *found;
            }
        } else if (peek('{')) {
            ++at_;
            std::string tag;
            if (!string(tag) || tag != "$tr" || !expect(':', "':' after $tr") || !text_value(kind, out, true)) {
                return fail("a string or {\"$tr\": token}");
            }
            return expect('}', "'}' after a tag");
        } else if (!string(text)) {
            return false;
        }
        out.kind = kind;
        out.cell.set_text(spawn_text(text.c_str()));
        return true;
    }

    bool value(Read& out, size_t depth)
    {
        if (depth > kMaxDepth) {
            return fail("a value too deep");
        }
        space();
        if (!more()) {
            return fail("a value");
        }
        if (here() == '"') {
            std::string name;
            if (!string(name)) {
                return false;
            }
            out.kind = QbValueKind::kName;
            out.cell.set_word(name_checksum(name));
            return true;
        }
        if (here() == '[') {
            return array(out, depth);
        }
        if (here() == '{') {
            return object(out, depth);
        }
        std::string text;
        if (!number(text)) {
            return false;
        }
        if (text.find_first_of(".eE") != std::string::npos) {
            out.kind = QbValueKind::kFloat;
            out.cell.set_word(std::bit_cast<uint32_t>(static_cast<float>(std::strtod(text.c_str(), nullptr))));
            return true;
        }
        const int64_t integer = std::strtoll(text.c_str(), nullptr, 10);
        if (integer < kLowestInteger || integer > kHighestInteger) {
            return fail("an integer out of range");
        }
        out.kind = QbValueKind::kInteger;
        out.cell.set_word(static_cast<uint32_t>(integer));
        return true;
    }

    // The kind of an array's elements: the first's - floats when there are floats among only numbers (values.js array).
    static QbValueKind element_kind(const std::vector<Read>& items)
    {
        if (items.empty()) {
            return QbValueKind::kNone;
        }
        bool floats = false;
        bool numbers = true;
        for (const Read& item : items) {
            floats = floats || item.kind == QbValueKind::kFloat;
            numbers = numbers && (item.kind == QbValueKind::kFloat || item.kind == QbValueKind::kInteger);
        }
        return floats && numbers ? QbValueKind::kFloat : items.front().kind;
    }
    // [values]: one kind of element; integers among floats are floats.
    bool array(Read& out, size_t depth)
    {
        ++at_;
        std::vector<Read> items;
        if (!take(']')) {
            for (;;) {
                Read& item = items.emplace_back();
                if (!value(item, depth + 1)) {
                    return false;
                }
                if (!item.script.empty()) {
                    return fail("a script value in an array");
                }
                if (!take(',')) {
                    break;
                }
            }
            if (!expect(']', "',' or ']' in an array")) {
                return false;
            }
        }
        const QbValueKind kind = element_kind(items);
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        const gsl::owner<Array*> made = script::new_array();
        if (!items.empty()) {
            made->configure_size_kind(static_cast<uint32_t>(items.size()), kind);
        }
        memory::leave_heap_scope();
        out.kind = QbValueKind::kArray;
        out.cell.set_array(made);
        uint32_t index = 0;
        for (Read& item : items) {
            if (kind == QbValueKind::kFloat && item.kind == QbValueKind::kInteger) {
                item.kind = QbValueKind::kFloat;
                item.cell.set_real(static_cast<float>(static_cast<int32_t>(item.cell.word())));
            }
            if (item.kind != kind) {
                return fail("an array's elements of different kinds");
            }
            made->set_cell(index++, item.cell);
        }
        return true;
    }

    // A member made and added to `structure` (a script value by its key; a flag: an unnamed name).
    bool member(Struct* structure, uint32_t name, Read& read)
    {
        const gsl::owner<Component*> component = new_component();
        component->name = name;
        if (!read.script.empty()) {
            if (!hooks_->script_value || !hooks_->script_value(read.script, *component, *error_)) {
                free_component(component);
                return fail("script value " + read.script);
            }
        } else {
            component->value = read.cell;
            set_value_kind(*component, read.kind);
        }
        structure->attach_module(component);
        return true;
    }

    // {...}: a tagged value ({"$tag": ...}) or a structure.
    bool object(Read& out, size_t depth)
    {
        ++at_;
        std::string key;
        const bool empty = take('}');
        if (!empty && (!string(key) || !expect(':', "':' after a key"))) {
            return false;
        }
        if (key.starts_with("$")) {
            return tagged(key, out, depth) && expect('}', "'}' after a tag");
        }
        const gsl::owner<Struct*> structure = script::new_struct();
        out.kind = QbValueKind::kStruct;
        out.cell.set_structure(structure);
        while (!empty) {
            Read read;
            if (word("true")) {
                read.kind = QbValueKind::kName;
                read.cell.set_word(name_checksum(key));
                if (!member(structure, 0, read)) {
                    return false;
                }
            } else if (!value(read, depth + 1) || !member(structure, name_checksum(key), read)) {
                return false;
            }
            if (!take(',')) {
                return expect('}', "',' or '}' in a structure");
            }
            if (!string(key) || !expect(':', "':' after a key")) {
                return false;
            }
        }
        return true;
    }

    bool pair(Read& out)
    {
        std::array<float, 2> xy{};
        if (!coordinates(xy)) {
            return false;
        }
        Pair* pair = new_pair();
        pair = pair ? new (pair) script::Pair : nullptr;
        if (!pair) {
            return fail("no memory for a pair");
        }
        pair->x() = xy.at(0);
        pair->y() = xy.at(1);
        out.kind = QbValueKind::kPair;
        out.cell.set_pair(pair);
        return true;
    }
    bool vector(Read& out)
    {
        std::array<float, 3> xyz{};
        if (!coordinates(xyz)) {
            return false;
        }
        Vector* vector = alloc_script_vector();
        vector = vector ? new (vector) script::Vector : nullptr;
        if (!vector) {
            return fail("no memory for a vector");
        }
        vector->x() = xyz.at(0);
        vector->y() = xyz.at(1);
        vector->z() = xyz.at(2);
        out.kind = QbValueKind::kVector;
        out.cell.set_vector(vector);
        return true;
    }
    // {"$struct": [[key | null, value], ...]}
    bool member_list(Read& out, size_t depth)
    {
        const gsl::owner<Struct*> structure = script::new_struct();
        out.kind = QbValueKind::kStruct;
        out.cell.set_structure(structure);
        if (!expect('[', "$struct takes a list")) {
            return false;
        }
        if (take(']')) {
            return true;
        }
        for (;;) {
            uint32_t name = 0;
            if (!expect('[', "a $struct member is [key, value]")) {
                return false;
            }
            if (!word("null")) {
                std::string key;
                if (!string(key)) {
                    return false;
                }
                name = name_checksum(key);
            }
            Read read;
            if (!expect(',', "a $struct member is [key, value]") || !value(read, depth + 1) ||
                !member(structure, name, read) || !expect(']', "a $struct member is [key, value]")) {
                return false;
            }
            if (!take(',')) {
                return expect(']', "',' or ']' in $struct");
            }
        }
    }

    bool tagged(const std::string& tag, Read& out, size_t depth)
    {
        if (tag == "$float") {
            uint32_t bits = 0;
            out.kind = QbValueKind::kFloat;
            if (!float_bits(bits)) {
                return false;
            }
            out.cell.set_word(bits);
            return true;
        }
        if (tag == "$string" || tag == "$tr") {
            return text_value(QbValueKind::kString, out, tag == "$tr");
        }
        if (tag == "$local") {
            return text_value(QbValueKind::kInlineText, out, false);
        }
        if (tag == "$pair") {
            return pair(out);
        }
        if (tag == "$vec") {
            return vector(out);
        }
        if (tag == "$script") {
            out.kind = QbValueKind::kScriptBody;
            return string(out.script) && (!out.script.empty() || fail("an empty script key"));
        }
        if (tag == "$struct") {
            return member_list(out, depth);
        }
        return fail("an unknown tag " + tag);
    }

    bool globals()
    {
        if (!expect('{', "globals are an object")) {
            return false;
        }
        if (take('}')) {
            return true;
        }
        for (;;) {
            std::string key;
            Read read;
            if (!string(key) || !expect(':', "':' after a global's name") || !value(read, 0)) {
                return false;
            }
            if (!read.script.empty()) {
                return fail("a global that is a script value");
            }
            hooks_->define(name_checksum(key), read.kind, read.cell);
            if (!take(',')) {
                return expect('}', "',' or '}' in the globals");
            }
        }
    }
};

} // namespace

std::optional<std::string> bytes_from_utf8(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();) {
        uint32_t character = static_cast<uint8_t>(text.at(at++));
        if (character >= kUtf8Lead) {
            const std::optional<unsigned> following = utf8_following(character);
            if (!following || text.size() - at < size_t{*following}) {
                return std::nullopt;
            }
            character &= kUtf8Bits >> *following;
            for (unsigned i = 0; i < *following; ++i) {
                character = (character << kUtf8Shift) | (static_cast<uint8_t>(text.at(at++)) & kUtf8Bits);
            }
        }
        if (character > kLastByte) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>(character));
    }
    return out;
}

bool define_data(std::string_view json, const DataHooks& hooks, std::string* from, std::string& error)
{
    error.clear();
    Reader reader(json, hooks, error);
    return reader.file(from);
}

} // namespace script::qb::ts
