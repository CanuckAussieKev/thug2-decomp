// NOT ORIGINAL - the level editor's full Inspector: every field of a node, typed (ours). See level_editor_fields.h.

#include "editor/level_editor_fields.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

#include "engine/core/checksums.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_objects.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"

namespace frontend::level_editor::fields {

// --- the pure parts ---------------------------------------------------------------------------------------------

const char* kind_name(Kind kind)
{
    switch (kind) {
    case Kind::kInteger:
        return "integer";
    case Kind::kFloat:
        return "float";
    case Kind::kName:
        return "name";
    case Kind::kChars:
        return "string";
    case Kind::kLocalisedText:
        return "local string";
    case Kind::kVec4:
        return "vector";
    case Kind::kPair:
        return "pair";
    case Kind::kBitMark:
        return "flag";
    case Kind::kNames:
        return "names";
    case Kind::kIntegers:
        return "integers";
    case Kind::kFloats:
        return "floats";
    default:
        return "other";
    }
}

bool same_value(const Value& a, const Value& b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case Kind::kInteger:
        return a.integer == b.integer;
    case Kind::kFloat:
        return a.numbers[0] == b.numbers[0];
    case Kind::kPair:
        return a.numbers[0] == b.numbers[0] && a.numbers[1] == b.numbers[1];
    case Kind::kVec4:
        return a.numbers[0] == b.numbers[0] && a.numbers[1] == b.numbers[1] && a.numbers[2] == b.numbers[2];
    case Kind::kName:
        return a.checksum == b.checksum;
    case Kind::kChars:
    case Kind::kLocalisedText:
    case Kind::kOther:
        return a.text == b.text;
    case Kind::kBitMark:
        return a.on == b.on;
    case Kind::kNames:
    case Kind::kIntegers:
        return a.list == b.list;
    case Kind::kFloats:
        return a.floats == b.floats;
    }
    return false;
}

namespace {

bool identifier_char(char c, bool first)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (!first && c >= '0' && c <= '9');
}

bool is_identifier(const std::string& text)
{
    if (text.empty() || text.size() > 120)
        return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (!identifier_char(text[i], i == 0))
            return false;
    }
    return true;
}

std::string hex8(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", value);
    return text;
}

std::string get_trim(const std::string& text)
{
    const size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    const size_t b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

// The parts of a list: split at commas and white space (empty parts dropped).
std::vector<std::string> get_parts(const std::string& text)
{
    std::vector<std::string> parts;
    std::string part;
    for (const char c : text) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!part.empty())
                parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.empty())
        parts.push_back(part);
    return parts;
}

bool parse_int(const std::string& text, int32_t* out)
{
    const std::string t = get_trim(text);
    if (t.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(t.c_str(), &end, 10);
    if (errno || !end || *end || value < INT32_MIN || value > INT32_MAX)
        return false;
    *out = static_cast<int32_t>(value);
    return true;
}

bool parse_float(const std::string& text, float* out)
{
    const std::string t = get_trim(text);
    if (t.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(t.c_str(), &end);
    if (errno || !end || *end || !std::isfinite(value) || std::fabs(value) > 3.0e38)
        return false;
    *out = static_cast<float>(value);
    return true;
}

// A float as short as it reads back exactly.
std::string float_text(float value)
{
    char text[32];
    for (int digits = 6; digits <= 9; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));
        if (std::strtof(text, nullptr) == value)
            break;
    }
    return text;
}

std::string get_join(const std::vector<std::string>& parts, const char* separator)
{
    std::string text;
    for (size_t i = 0; i < parts.size(); ++i)
        text += (i ? separator : "") + parts[i];
    return text;
}

} // namespace

uint32_t hash_of_name(const std::string& name) { return checksums::hash_from_text(name.c_str()); }

std::string get_name_text(uint32_t checksum, const NameLookup& names)
{
    const char* name = names ? names(checksum) : nullptr;
    return name && *name ? std::string(name) : "#" + hex8(checksum);
}

std::string record_name(uint32_t checksum, const NameLookup& names)
{
    const char* name = names ? names(checksum) : nullptr;
    if (name && is_identifier(name) && hash_of_name(name) == checksum)
        return name;
    return "0x" + hex8(checksum);
}

bool parse_name(const std::string& typed, uint32_t* checksum)
{
    std::string text = get_trim(typed);
    std::string hex;
    if (text.size() == 9 && text[0] == '#')
        hex = text.substr(1);
    else if (text.size() == 10 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        hex = text.substr(2);
    if (!hex.empty()) {
        uint32_t value = 0;
        for (const char c : hex) {
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                     : -1;
            if (digit < 0)
                return false;
            value = value << 4 | static_cast<uint32_t>(digit);
        }
        *checksum = value;
        return true;
    }
    if (!is_identifier(text))
        return false;
    *checksum = hash_of_name(text);
    return true;
}

std::string get_format(const Value& value, const NameLookup& names)
{
    switch (value.kind) {
    case Kind::kInteger:
        return std::to_string(value.integer);
    case Kind::kFloat:
        return float_text(value.numbers[0]);
    case Kind::kPair:
        return float_text(value.numbers[0]) + ", " + float_text(value.numbers[1]);
    case Kind::kVec4:
        return float_text(value.numbers[0]) + ", " + float_text(value.numbers[1]) + ", " + float_text(value.numbers[2]);
    case Kind::kName:
        return get_name_text(value.checksum, names);
    case Kind::kChars:
    case Kind::kLocalisedText:
    case Kind::kOther:
        return value.text;
    case Kind::kBitMark:
        return value.on ? "on" : "off";
    case Kind::kNames: {
        std::vector<std::string> parts;
        parts.reserve(value.list.size());
        for (const uint32_t c : value.list)
            parts.push_back(get_name_text(c, names));
        return get_join(parts, ", ");
    }
    case Kind::kIntegers: {
        std::vector<std::string> parts;
        parts.reserve(value.list.size());
        for (const uint32_t i : value.list)
            parts.push_back(std::to_string(static_cast<int32_t>(i)));
        return get_join(parts, ", ");
    }
    case Kind::kFloats: {
        std::vector<std::string> parts;
        parts.reserve(value.floats.size());
        for (const float f : value.floats)
            parts.push_back(float_text(f));
        return get_join(parts, ", ");
    }
    }
    return "";
}

bool parse(Kind kind, const std::string& text, Value* value, std::string* error)
{
    Value v;
    v.kind = kind;
    std::vector<std::string> parts = get_parts(text);
    switch (kind) {
    case Kind::kInteger:
        if (!parse_int(text, &v.integer)) {
            *error = "not a whole number";
            return false;
        }
        break;
    case Kind::kFloat:
        if (!parse_float(text, &v.numbers[0])) {
            *error = "not a number";
            return false;
        }
        break;
    case Kind::kPair:
    case Kind::kVec4: {
        const size_t want = kind == Kind::kPair ? 2 : 3;
        if (parts.size() != want) {
            *error = kind == Kind::kPair ? "two numbers (x, y)" : "three numbers (x, y, z)";
            return false;
        }
        for (size_t i = 0; i < want; ++i) {
            if (!parse_float(parts[i], &v.numbers[i])) {
                *error = "'" + parts[i] + "' isn't a number";
                return false;
            }
        }
        break;
    }
    case Kind::kName:
        if (!parse_name(text, &v.checksum)) {
            *error = "a name (letters, digits, _) or #XXXXXXXX";
            return false;
        }
        break;
    case Kind::kChars:
    case Kind::kLocalisedText:
        if (text.size() > 255) {
            *error = "too long (255 characters at most)";
            return false;
        }
        v.text = text;
        break;
    case Kind::kBitMark: {
        std::string t = get_trim(text);
        for (char& c : t)
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (t == "1" || t == "on" || t == "true" || t == "yes")
            v.on = true;
        else if (t == "0" || t == "off" || t == "false" || t == "no")
            v.on = false;
        else {
            *error = "on or off";
            return false;
        }
        break;
    }
    case Kind::kNames:
        for (const std::string& part : parts) {
            uint32_t c = 0;
            if (!parse_name(part, &c)) {
                *error = "'" + part + "' isn't a name";
                return false;
            }
            v.list.push_back(c);
        }
        break;
    case Kind::kIntegers:
        for (const std::string& part : parts) {
            int32_t i = 0;
            if (!parse_int(part, &i)) {
                *error = "'" + part + "' isn't a whole number";
                return false;
            }
            v.list.push_back(static_cast<uint32_t>(i));
        }
        break;
    case Kind::kFloats:
        for (const std::string& part : parts) {
            float f = 0.0f;
            if (!parse_float(part, &f)) {
                *error = "'" + part + "' isn't a number";
                return false;
            }
            v.floats.push_back(f);
        }
        break;
    case Kind::kOther:
        *error = "not editable";
        return false;
    }
    if (kind == Kind::kNames || kind == Kind::kIntegers || kind == Kind::kFloats) {
        if (v.list.size() > 256 || v.floats.size() > 256) {
            *error = "too many (256 at most)";
            return false;
        }
    }
    *value = v;
    error->clear();
    return true;
}

std::string escape(const std::string& text)
{
    std::string out;
    for (const unsigned char c : text) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                           c == '.' || c == '-' || c == '/' || c == ':';
        if (plain) {
            out += static_cast<char>(c);
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

bool unescape(const std::string& text, std::string* out)
{
    out->clear();
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '%') {
            out->push_back(c);
            continue;
        }
        if (i + 2 >= text.size())
            return false;
        int value = 0;
        for (int k = 1; k <= 2; ++k) {
            const char h = text[i + k];
            const int digit = h >= '0' && h <= '9'   ? h - '0'
                              : h >= 'A' && h <= 'F' ? h - 'A' + 10
                              : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                                     : -1;
            if (digit < 0)
                return false;
            value = value * 16 + digit;
        }
        if (value == 0)
            return false;
        out->push_back(static_cast<char>(value));
        i += 2;
    }
    return true;
}

std::string encode_value(const Value& value, const NameLookup& names)
{
    char text[96];
    switch (value.kind) {
    case Kind::kInteger:
        return "i:" + std::to_string(value.integer);
    case Kind::kFloat:
        std::snprintf(text, sizeof(text), "f:%.9g", static_cast<double>(value.numbers[0]));
        return text;
    case Kind::kPair:
        std::snprintf(text, sizeof(text), "p:%.9g,%.9g", static_cast<double>(value.numbers[0]),
                      static_cast<double>(value.numbers[1]));
        return text;
    case Kind::kVec4:
        std::snprintf(text, sizeof(text), "v:%.9g,%.9g,%.9g", static_cast<double>(value.numbers[0]),
                      static_cast<double>(value.numbers[1]), static_cast<double>(value.numbers[2]));
        return text;
    case Kind::kName:
        return "n:" + record_name(value.checksum, names);
    case Kind::kChars:
        return "s:" + escape(value.text);
    case Kind::kLocalisedText:
        return "l:" + escape(value.text);
    case Kind::kBitMark:
        return value.on ? "flag:1" : "flag:0";
    case Kind::kNames: {
        std::vector<std::string> parts;
        parts.reserve(value.list.size());
        for (const uint32_t c : value.list)
            parts.push_back(record_name(c, names));
        return "an:" + get_join(parts, ",");
    }
    case Kind::kIntegers: {
        std::vector<std::string> parts;
        parts.reserve(value.list.size());
        for (const uint32_t i : value.list)
            parts.push_back(std::to_string(static_cast<int32_t>(i)));
        return "ai:" + get_join(parts, ",");
    }
    case Kind::kFloats: {
        std::vector<std::string> parts;
        for (const float f : value.floats) {
            std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(f));
            parts.emplace_back(text);
        }
        return "af:" + get_join(parts, ",");
    }
    case Kind::kOther:
        break;
    }
    return "";
}

bool decode_value(const std::string& text, Value* value)
{
    const size_t colon = text.find(':');
    if (colon == std::string::npos)
        return false;
    const std::string type = text.substr(0, colon);
    const std::string body = text.substr(colon + 1);
    std::string error, plain;
    // (lists are comma-separated in records; Parse takes commas)
    if (type == "i")
        return parse(Kind::kInteger, body, value, &error);
    if (type == "f")
        return parse(Kind::kFloat, body, value, &error);
    if (type == "p")
        return parse(Kind::kPair, body, value, &error);
    if (type == "v")
        return parse(Kind::kVec4, body, value, &error);
    if (type == "n")
        return parse(Kind::kName, body, value, &error);
    if (type == "s" || type == "l")
        return unescape(body, &plain) && parse(type == "s" ? Kind::kChars : Kind::kLocalisedText, plain, value, &error);
    if (type == "flag") {
        if (body != "0" && body != "1")
            return false;
        return parse(Kind::kBitMark, body, value, &error);
    }
    if (type == "an")
        return parse(Kind::kNames, body, value, &error);
    if (type == "ai")
        return parse(Kind::kIntegers, body, value, &error);
    if (type == "af")
        return parse(Kind::kFloats, body, value, &error);
    return false;
}

std::string get_token(const Edit& edit, const NameLookup& names)
{
    return "f." + record_name(edit.key, names) + "=" +
           (edit.removed ? std::string("del") : encode_value(edit.value, names));
}

bool is_field_key(const std::string& key) { return key.size() > 2 && key[0] == 'f' && key[1] == '.'; }

bool read_token(const std::string& key, const std::string& value, Edit* edit)
{
    if (!is_field_key(key))
        return false;
    Edit e;
    if (!parse_name(key.substr(2), &e.key))
        return false;
    if (value == "del") {
        e.removed = true;
    } else if (!decode_value(value, &e.value)) {
        return false;
    }
    *edit = e;
    return true;
}

uint32_t get_key_name()
{
    static const uint32_t kK = hash_of_name("name");
    return kK;
}
uint32_t key_class()
{
    static const uint32_t kK = hash_of_name("Class");
    return kK;
}
uint32_t get_key_type()
{
    static const uint32_t kK = hash_of_name("Type");
    return kK;
}
uint32_t key_pos()
{
    static const uint32_t kK = hash_of_name("pos");
    return kK;
}
uint32_t key_angles()
{
    static const uint32_t kK = hash_of_name("angles");
    return kK;
}
uint32_t key_links()
{
    static const uint32_t kK = hash_of_name("links");
    return kK;
}
uint32_t key_terrain()
{
    static const uint32_t kK = hash_of_name("TerrainType");
    return kK;
}
uint32_t key_restart_types()
{
    static const uint32_t kK = hash_of_name("restart_types");
    return kK;
}

When when_applied(nodes::Family family, uint32_t key, const char** note)
{
    const char* dummy = nullptr;
    const char** out = note ? note : &dummy;
    if (key == get_key_name() || key == key_class() || key == get_key_type()) {
        *out = "Read-only";
        return When::kReadOnly;
    }
    if (key == key_pos() || key == key_angles()) {
        *out = "Edit with Position / Heading";
        return When::kElsewhere;
    }
    if (key == key_links()) {
        *out = "Live";
        return When::kLive;
    }
    if (key == key_terrain() && family == nodes::Family::kRail) {
        *out = "Edit with Terrain";
        return When::kElsewhere;
    }
    if (key == key_restart_types() && family == nodes::Family::kSpawn) {
        *out = "Live";
        return When::kLive;
    }
    *out = "Applies on level reload";
    return When::kReload;
}

bool valid_links(const std::vector<uint32_t>& links, uint32_t self, const std::function<bool(uint32_t)>& exists,
                 const NameLookup& names, std::string* error)
{
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i] == self) {
            *error = "a node can't link to itself";
            return false;
        }
        if (!exists(links[i])) {
            *error = "no node named " + get_name_text(links[i], names) + " in this level";
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (links[j] == links[i]) {
                *error = get_name_text(links[i], names) + " is linked twice";
                return false;
            }
        }
    }
    error->clear();
    return true;
}

void move_last_to(script::Struct* s, int index)
{
    script::Component* previous = nullptr;
    script::Component* last = s->head_;
    while (last && last->next) {
        previous = last;
        last = last->next;
    }
    if (!last || !previous || index < 0)
        return; // (the only one, or none: in its place already)
    int others = 0;
    script::Component* before = nullptr;
    for (script::Component* c = s->head_; c != last && others < index; c = c->next) {
        before = c;
        ++others;
    }
    if (before == previous)
        return; // (already there)
    previous->next = nullptr;
    if (before) {
        last->next = before->next;
        before->next = last;
    } else {
        last->next = s->head_;
        s->head_ = last;
    }
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

std::unordered_map<uint32_t, std::string> g_names; // (names typed in this session)

script::Struct* get_included(const script::Component* c)
{
    if (c->name != 0 || c->kind_tag() != script::kQbvalName)
        return nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
    const script::SymbolTableEntry* symbol = script::resolve_symbol(c->checksum);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
    return symbol && symbol->type == script::kQbvalStruct ? symbol->structure : nullptr;
}

bool is_flag(const script::Component* c)
{
    return c->name == 0 && c->kind_tag() == script::kQbvalName && !get_included(c);
}

// The key a component goes by: its name, a bare flag's name.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
uint32_t get_key_of(const script::Component* c) { return is_flag(c) ? c->checksum : c->name; }

Value value_of(const script::Component* c)
{
    Value v;
    if (is_flag(c)) {
        v.kind = Kind::kBitMark;
        v.on = true;
        return v;
    }
    switch (c->kind_tag()) {
    case script::kQbvalInteger:
        v.kind = Kind::kInteger;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        v.integer = c->integer;
        break;
    case script::kQbvalFloat:
        v.kind = Kind::kFloat;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        v.numbers[0] = c->float_value_;
        break;
    case script::kQbvalName:
        v.kind = Kind::kName;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        v.checksum = c->checksum;
        break;
    case script::kQbvalString:
        v.kind = Kind::kChars;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        v.text = c->string ? c->string : "";
        break;
    case script::kQbvalInlineText:
        v.kind = Kind::kLocalisedText;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        v.text = c->string ? c->string : "";
        break;
    case script::kQbvalPair:
        v.kind = Kind::kPair;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        if (c->pair) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            v.numbers[0] = c->pair->x;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            v.numbers[1] = c->pair->y;
        }
        break;
    case script::kQbvalVector:
        v.kind = Kind::kVec4;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        if (c->vector) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            v.numbers[0] = c->vector->x;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            v.numbers[1] = c->vector->y;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            v.numbers[2] = c->vector->z;
        }
        break;
    case script::kQbvalArray:
    case script::kEsymboltypeArraypointer: {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        script::Array* array = c->array;
        const uint32_t size = array ? array->extent_ : 0;
        const uint8_t type = array ? static_cast<uint8_t>(array->kind_tag_) : 0;
        if (size == 0 || type == script::kQbvalName) {
            v.kind = Kind::kNames;
            for (uint32_t i = 0; i < size; ++i)
                v.list.push_back(array->fetch_part(i));
        } else if (type == script::kQbvalInteger) {
            v.kind = Kind::kIntegers;
            for (uint32_t i = 0; i < size; ++i)
                v.list.push_back(array->fetch_part(i));
        } else if (type == script::kQbvalFloat) {
            v.kind = Kind::kFloats;
            for (uint32_t i = 0; i < size; ++i)
                v.floats.push_back(array->fetch_float(i));
        } else {
            v.kind = Kind::kOther;
            v.text = "[" + std::to_string(size) + " items]";
        }
        break;
    }
    case script::kQbvalStruct:
    case script::kQbvalStructRef:
        v.kind = Kind::kOther;
        v.text = "{a structure}";
        break;
    case script::kQbvalScriptBody:
        v.kind = Kind::kOther;
        v.text = "(a script)";
        break;
    default:
        v.kind = Kind::kOther;
        v.text = "?";
        break;
    }
    return v;
}

// The value added to `s` as a new component at its end.
void add_value(script::Struct* s, uint32_t key, const Value& v)
{
    switch (v.kind) {
    case Kind::kInteger:
        s->put_int(key, v.integer);
        break;
    case Kind::kFloat:
        s->push_float(key, v.numbers[0]);
        break;
    case Kind::kName:
        s->put_name_key(key, v.checksum);
        break;
    case Kind::kChars:
        s->append_word(key, v.text.c_str());
        break;
    case Kind::kLocalisedText:
        s->append_inline_text(key, v.text.c_str());
        break;
    case Kind::kPair:
        s->insert_pair(key, v.numbers[0], v.numbers[1]);
        break;
    case Kind::kVec4:
        s->put_vec3(key, v.numbers[0], v.numbers[1], v.numbers[2]);
        break;
    case Kind::kBitMark:
        if (v.on)
            s->put_name_key(0u, key);
        break;
    case Kind::kNames:
    case Kind::kIntegers:
    case Kind::kFloats: {
        auto* array = new (script::alloc_script_array()) script::Array;
        auto size = static_cast<uint32_t>(v.kind == Kind::kFloats ? v.floats.size() : v.list.size());
        array->configure_size_kind(size, v.kind == Kind::kNames      ? script::kQbvalName
                                         : v.kind == Kind::kIntegers ? script::kQbvalInteger
                                                                     : script::kQbvalFloat);
        for (uint32_t i = 0; i < size; ++i) {
            if (v.kind == Kind::kFloats)
                array->change_float(i, v.floats[i]);
            else
                array->set_element(i, v.list[i]);
        }
        s->append_array_ref(key, array);
        break;
    }
    case Kind::kOther:
        break;
    }
}

bool get_matches(const script::Component* c, uint32_t key, bool flag)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
    return flag ? is_flag(c) && c->checksum == key : c->name == key && !is_flag(c);
}

// The node's own field: the last own component of that key (flags: the bare name), and its place - how many of the
// node's other components come before it.
Own own_of(script::Struct* s, uint32_t key, bool flag)
{
    Own own;
    int others = 0;
    for (const script::Component* c = s->head_; c; c = c->next) {
        if (get_matches(c, key, flag)) {
            own.present = true;
            own.value = value_of(c);
            own.index = others;
        } else {
            ++others;
        }
    }
    return own;
}

// Whether a shared structure of `s` after its own component's place (`index` others before it; -1: anywhere) has the
// key - which then counts over the node's own.
bool included_after(script::Struct* s, uint32_t key, bool flag, int index)
{
    int others = 0;
    for (const script::Component* c = s->head_; c; c = c->next) {
        if (get_matches(c, key, flag))
            continue;
        if (script::Struct* included = get_included(c)) {
            if (others >= index &&
                (flag ? included->has_bare_flag(key) : included->find_last_component(key) != nullptr))
                return true;
        }
        ++others;
    }
    return false;
}

// The node's own field made `own`: the old one(s) taken out, the new one put where `own` says (-1: at the end).
void put_own(script::Struct* s, uint32_t key, bool flag, const Own& own)
{
    if (flag)
        s->remove_flag(key);
    else
        s->erase_module(key);
    if (!own.present || (flag && !own.value.on))
        return;
    Value flag_on;
    flag_on.kind = Kind::kBitMark;
    flag_on.on = true;
    add_value(s, key, flag ? flag_on : own.value);
    if (own.index >= 0)
        move_last_to(s, own.index);
}

struct Touched {
    uint32_t node, key;
    bool flag;
    Own origin;
    script::Struct* structure;
};
std::vector<Touched> g_touched;

Touched* find_touched(uint32_t node, uint32_t key)
{
    for (Touched& t : g_touched) {
        if (t.node == node && t.key == key)
            return &t;
    }
    return nullptr;
}

script::Struct* level_node(uint32_t name)
{
    return name && level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
}

// Whether `key` is a flag of the node (its own or a shared structure's), or should be taken as one (`value` a flag).
bool key_is_flag(script::Struct* s, uint32_t key, const Value* value)
{
    if (value && value->kind == Kind::kBitMark)
        return true;
    for (const script::Component* c = s->head_; c; c = c->next) {
        if (get_matches(c, key, true))
            return true;
        if (get_matches(c, key, false))
            return false;
        if (script::Struct* included = get_included(c)) {
            if (included->has_bare_flag(key) && !included->find_last_component(key))
                return true;
        }
    }
    return false;
}

bool same_own(const Own& a, const Own& b, bool flag)
{
    if (flag)
        return a.present == b.present;
    return a.present == b.present && (!a.present || (same_value(a.value, b.value) && a.index == b.index));
}

const nodes::Elem* layer_node(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 ? &nodes::get_all()[index] : nullptr;
}

} // namespace

Own own_in(script::Struct* s, uint32_t key, bool flag) { return own_of(s, key, flag); }
void put_in(script::Struct* s, uint32_t key, bool flag, const Own& own) { put_own(s, key, flag, own); }

const char* get_name_of(uint32_t checksum)
{
    auto it = g_names.find(checksum);
    if (it != g_names.end())
        return it->second.c_str();
    return object_edit::get_name_of(checksum);
}

void remember(const std::string& name)
{
    if (is_identifier(name))
        g_names[hash_of_name(name)] = name;
}

std::vector<Row> rows_of(const nodes::Elem& node)
{
    std::vector<Row> rows;
    script::Struct* s = nodes::struct_of(node);
    if (!s)
        return rows;
    struct TableRow {
        uint32_t key;
        script::Component* c;
        bool included;
    };
    std::vector<TableRow> entries;
    for (script::Component* c = s->head_; c; c = c->next) {
        if (const script::Struct* included = get_included(c)) {
            for (script::Component* inner = included->head_; inner; inner = inner->next) {
                if (!get_included(inner)) // (one level of shared structures: the node arrays have no deeper ones)
                    entries.push_back({get_key_of(inner), inner, true});
            }
            continue;
        }
        entries.push_back({get_key_of(c), c, false});
    }
    std::vector<uint32_t> order;
    for (const TableRow& e : entries) {
        if (std::find(order.begin(), order.end(), e.key) == order.end())
            order.push_back(e.key);
    }
    const NameLookup names = get_name_of;
    for (const uint32_t key : order) {
        const TableRow* effective = nullptr;
        bool any_included = false, any_own = false;
        for (const TableRow& e : entries) {
            if (e.key != key)
                continue;
            effective = &e;
            any_included = any_included || e.included;
            any_own = any_own || !e.included;
        }
        Row row;
        row.key = key;
        row.value = value_of(effective->c);
        row.included = effective->included;
        row.overridden = !effective->included && any_included;
        row.when = when_applied(node.family, key, &row.note);
        if (const Touched* t = find_touched(node.name, key)) {
            const Own now = own_of(s, key, t->flag);
            row.edited = !same_own(now, t->origin, t->flag);
        }
        row.editable = true;
        if (node.added) {
            row.why = "Set by the node type";
        } else if (row.when == When::kReadOnly || row.when == When::kElsewhere) {
            row.why = row.note;
        } else if (row.value.kind == Kind::kOther) {
            row.why = "Not editable here";
        } else if (row.value.kind == Kind::kBitMark && effective->included && !any_own) {
            row.why = "Shared by the class";
        } else if (node.hidden && (key == nodes::hidden_class_key() || key == nodes::hidden_type_key())) {
            row.why = "Editor data for a hidden node";
        }
        row.editable = row.why.empty();
        rows.push_back(row);
    }
    return rows;
}

namespace {

// The field set on structure `s` of node `name` (of `family`: what may be set - see Set).
bool set_on(script::Struct* s, uint32_t name, nodes::Family family, uint32_t key, const Value& value, bool removed,
            std::string* error)
{
    const When when = when_applied(family, key, nullptr);
    if (when == When::kReadOnly || when == When::kElsewhere || key == key_links() || key == 0) {
        *error = "Field not editable";
        return false;
    }
    if (!removed && value.kind == Kind::kOther) {
        *error = "Not an editable value";
        return false;
    }
    const bool flag = key_is_flag(s, key, removed ? nullptr : &value);
    const Own now = own_of(s, key, flag);
    Own want;
    if (flag) {
        const bool on = !removed && value.on;
        if (!on && !now.present && included_after(s, key, true, -1)) {
            *error = "Shared by the class";
            return false;
        }
        want.present = on;
        want.value = value;
        want.index = now.present ? now.index : -1;
    } else {
        want.present = !removed;
        want.value = value;
        // (in its place, unless a shared structure after it would count over it: then at the end)
        want.index = now.present && !included_after(s, key, false, now.index) ? now.index : -1;
    }
    if (!find_touched(name, key))
        g_touched.push_back({name, key, flag, now, s});
    put_own(s, key, flag, want);
    error->clear();
    return true;
}

} // namespace

bool assign_value(uint32_t name, uint32_t key, const Value& value, bool removed, std::string* error)
{
    script::Struct* s = level_node(name);
    if (!s) {
        *error = "No such node in this level";
        return false;
    }
    if (node_create::is_added(name)) {
        *error = "Set by the node type";
        return false;
    }
    const nodes::Elem* layer = layer_node(name);
    if (!set_on(s, name, layer ? layer->family : nodes::Family::kOther, key, value, removed, error))
        return false;
    nodes::mark_stale(); // (the node layer's copies: restart types, radius, colour...)
    return true;
}

bool get_capture(uint32_t name, uint32_t key, Own* own)
{
    script::Struct* s = level_node(name);
    if (!s)
        return false;
    const Touched* t = find_touched(name, key);
    *own = own_of(s, key, t ? t->flag : key_is_flag(s, key, nullptr));
    return true;
}

bool place_in(uint32_t name, uint32_t key, const Own& own)
{
    script::Struct* s = level_node(name);
    if (!s)
        return false;
    const Touched* t = find_touched(name, key);
    const bool flag = t ? t->flag : own.value.kind == Kind::kBitMark;
    put_own(s, key, flag, own);
    nodes::mark_stale();
    return true;
}

bool get_origin(uint32_t name, uint32_t key, Own* own)
{
    const Touched* t = find_touched(name, key);
    if (!t)
        return false;
    *own = t->origin;
    return true;
}

bool reset_node(uint32_t name)
{
    bool any = false;
    for (const Touched& t : g_touched) {
        if (t.node == name && level_node(name) == t.structure) {
            put_own(t.structure, t.key, t.flag, t.origin);
            any = true;
        }
    }
    if (any)
        nodes::mark_stale();
    return any;
}

bool node_edited(uint32_t name)
{
    for (const Touched& t : g_touched) {
        if (t.node == name && level_node(name) == t.structure &&
            !same_own(own_of(t.structure, t.key, t.flag), t.origin, t.flag))
            return true;
    }
    return false;
}

std::vector<uint32_t> touched_keys(uint32_t name)
{
    std::vector<uint32_t> keys;
    for (const Touched& t : g_touched) {
        if (t.node == name)
            keys.push_back(t.key);
    }
    return keys;
}

std::vector<NodeEdits> get_changed()
{
    std::vector<NodeEdits> out;
    for (const Touched& t : g_touched) {
        script::Struct* s = level_node(t.node);
        if (!s || s != t.structure)
            continue;
        const Own now = own_of(s, t.key, t.flag);
        if (same_own(now, t.origin, t.flag))
            continue;
        Edit edit;
        edit.key = t.key;
        if (t.flag) {
            edit.value.kind = Kind::kBitMark;
            edit.value.on = now.present;
        } else if (now.present) {
            edit.value = now.value;
        } else {
            edit.removed = true;
        }
        auto it = std::find_if(out.begin(), out.end(), [&](const NodeEdits& n) { return n.name == t.node; });
        if (it == out.end()) {
            out.push_back({t.node, {}});
            it = out.end() - 1;
        }
        it->edits.push_back(edit);
    }
    return out;
}

int get_apply(const std::vector<NodeEdits>& edits)
{
    int applied = 0;
    for (const NodeEdits& node : edits) {
        for (const Edit& edit : node.edits) {
            std::string error;
            if (edit.key == key_links())
                continue; // (links: the chains' records)
            if (assign_value(node.name, edit.key, edit.value, edit.removed, &error))
                ++applied;
        }
    }
    return applied;
}

void reset_all()
{
    for (const Touched& t : g_touched) {
        if (level_node(t.node) == t.structure)
            put_own(t.structure, t.key, t.flag, t.origin);
    }
    if (!g_touched.empty())
        nodes::mark_stale();
}

void forget_level()
{
    reset_all(); // (only where the structure is still the one changed: the same level's array kept loaded)
    g_touched.clear();
}

int apply_early(uint32_t array, const std::vector<NodeEdits>& edits)
{
    // (the level's objects aren't made yet: the node layer isn't asked - only the node manager's structures)
    if (!array || !level_nodes::node_manager_instance())
        return 0;
    int applied = 0;
    for (const NodeEdits& node : edits) {
        script::Struct* s = level_node(node.name);
        if (!s)
            continue;
        for (const Edit& edit : node.edits) {
            std::string error;
            if (edit.key != key_links() &&
                set_on(s, node.name, nodes::Family::kOther, edit.key, edit.value, edit.removed, &error))
                ++applied;
        }
    }
    return applied;
}

} // namespace frontend::level_editor::fields
