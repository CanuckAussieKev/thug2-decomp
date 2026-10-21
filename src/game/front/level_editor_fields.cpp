// NOT ORIGINAL - the level editor's full Inspector: every field of a node, typed (ours). See level_editor_fields.h.

#include "game/front/level_editor_fields.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <utility>

#include "game/core/crc.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_objects.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"

namespace Front::LevelEditor::Fields {

// --- the pure parts ---------------------------------------------------------------------------------------------

const char* KindName(Kind kind)
{
    switch (kind) {
    case Kind::Integer: return "integer";
    case Kind::Float: return "float";
    case Kind::Name: return "name";
    case Kind::String: return "string";
    case Kind::LocalString: return "local string";
    case Kind::Vector: return "vector";
    case Kind::Pair: return "pair";
    case Kind::Flag: return "flag";
    case Kind::Names: return "names";
    case Kind::Integers: return "integers";
    case Kind::Floats: return "floats";
    default: return "other";
    }
}

bool SameValue(const Value& a, const Value& b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case Kind::Integer: return a.integer == b.integer;
    case Kind::Float: return a.numbers[0] == b.numbers[0];
    case Kind::Pair: return a.numbers[0] == b.numbers[0] && a.numbers[1] == b.numbers[1];
    case Kind::Vector:
        return a.numbers[0] == b.numbers[0] && a.numbers[1] == b.numbers[1] && a.numbers[2] == b.numbers[2];
    case Kind::Name: return a.checksum == b.checksum;
    case Kind::String:
    case Kind::LocalString:
    case Kind::Other: return a.text == b.text;
    case Kind::Flag: return a.on == b.on;
    case Kind::Names:
    case Kind::Integers: return a.list == b.list;
    case Kind::Floats: return a.floats == b.floats;
    }
    return false;
}

namespace {

bool IdentifierChar(char c, bool first)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (!first && c >= '0' && c <= '9');
}

bool IsIdentifier(const std::string& text)
{
    if (text.empty() || text.size() > 120)
        return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (!IdentifierChar(text[i], i == 0))
            return false;
    }
    return true;
}

std::string Hex8(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", value);
    return text;
}

std::string Trim(const std::string& text)
{
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

// The parts of a list: split at commas and white space (empty parts dropped).
std::vector<std::string> Parts(const std::string& text)
{
    std::vector<std::string> parts;
    std::string part;
    for (char c : text) {
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

bool ParseInt(const std::string& text, int32_t* out)
{
    std::string t = Trim(text);
    if (t.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    long long value = std::strtoll(t.c_str(), &end, 10);
    if (errno || !end || *end || value < INT32_MIN || value > INT32_MAX)
        return false;
    *out = static_cast<int32_t>(value);
    return true;
}

bool ParseFloat(const std::string& text, float* out)
{
    std::string t = Trim(text);
    if (t.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    double value = std::strtod(t.c_str(), &end);
    if (errno || !end || *end || !std::isfinite(value) || std::fabs(value) > 3.0e38)
        return false;
    *out = static_cast<float>(value);
    return true;
}

// A float as short as it reads back exactly.
std::string FloatText(float value)
{
    char text[32];
    for (int digits = 6; digits <= 9; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));
        if (std::strtof(text, nullptr) == value)
            break;
    }
    return text;
}

std::string Join(const std::vector<std::string>& parts, const char* separator)
{
    std::string text;
    for (size_t i = 0; i < parts.size(); ++i)
        text += (i ? separator : "") + parts[i];
    return text;
}

} // namespace

uint32_t NameChecksum(const std::string& name) { return Crc::GenerateCRCFromString(name.c_str()); }

std::string NameText(uint32_t checksum, const NameLookup& names)
{
    const char* name = names ? names(checksum) : nullptr;
    return name && *name ? std::string(name) : "#" + Hex8(checksum);
}

std::string RecordName(uint32_t checksum, const NameLookup& names)
{
    const char* name = names ? names(checksum) : nullptr;
    if (name && IsIdentifier(name) && NameChecksum(name) == checksum)
        return name;
    return "0x" + Hex8(checksum);
}

bool ParseName(const std::string& typed, uint32_t* checksum)
{
    std::string text = Trim(typed);
    std::string hex;
    if (text.size() == 9 && text[0] == '#')
        hex = text.substr(1);
    else if (text.size() == 10 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        hex = text.substr(2);
    if (!hex.empty()) {
        uint32_t value = 0;
        for (char c : hex) {
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0)
                return false;
            value = value << 4 | static_cast<uint32_t>(digit);
        }
        *checksum = value;
        return true;
    }
    if (!IsIdentifier(text))
        return false;
    *checksum = NameChecksum(text);
    return true;
}

std::string Format(const Value& value, const NameLookup& names)
{
    switch (value.kind) {
    case Kind::Integer: return std::to_string(value.integer);
    case Kind::Float: return FloatText(value.numbers[0]);
    case Kind::Pair: return FloatText(value.numbers[0]) + ", " + FloatText(value.numbers[1]);
    case Kind::Vector:
        return FloatText(value.numbers[0]) + ", " + FloatText(value.numbers[1]) + ", " + FloatText(value.numbers[2]);
    case Kind::Name: return NameText(value.checksum, names);
    case Kind::String:
    case Kind::LocalString:
    case Kind::Other: return value.text;
    case Kind::Flag: return value.on ? "on" : "off";
    case Kind::Names: {
        std::vector<std::string> parts;
        for (uint32_t c : value.list)
            parts.push_back(NameText(c, names));
        return Join(parts, ", ");
    }
    case Kind::Integers: {
        std::vector<std::string> parts;
        for (uint32_t i : value.list)
            parts.push_back(std::to_string(static_cast<int32_t>(i)));
        return Join(parts, ", ");
    }
    case Kind::Floats: {
        std::vector<std::string> parts;
        for (float f : value.floats)
            parts.push_back(FloatText(f));
        return Join(parts, ", ");
    }
    }
    return "";
}

bool Parse(Kind kind, const std::string& text, Value* value, std::string* error)
{
    Value v;
    v.kind = kind;
    std::vector<std::string> parts = Parts(text);
    switch (kind) {
    case Kind::Integer:
        if (!ParseInt(text, &v.integer)) {
            *error = "not a whole number";
            return false;
        }
        break;
    case Kind::Float:
        if (!ParseFloat(text, &v.numbers[0])) {
            *error = "not a number";
            return false;
        }
        break;
    case Kind::Pair:
    case Kind::Vector: {
        size_t want = kind == Kind::Pair ? 2 : 3;
        if (parts.size() != want) {
            *error = kind == Kind::Pair ? "two numbers (x, y)" : "three numbers (x, y, z)";
            return false;
        }
        for (size_t i = 0; i < want; ++i) {
            if (!ParseFloat(parts[i], &v.numbers[i])) {
                *error = "'" + parts[i] + "' isn't a number";
                return false;
            }
        }
        break;
    }
    case Kind::Name:
        if (!ParseName(text, &v.checksum)) {
            *error = "a name (letters, digits, _) or #XXXXXXXX";
            return false;
        }
        break;
    case Kind::String:
    case Kind::LocalString:
        if (text.size() > 255) {
            *error = "too long (255 characters at most)";
            return false;
        }
        v.text = text;
        break;
    case Kind::Flag: {
        std::string t = Trim(text);
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
    case Kind::Names:
        for (const std::string& part : parts) {
            uint32_t c = 0;
            if (!ParseName(part, &c)) {
                *error = "'" + part + "' isn't a name";
                return false;
            }
            v.list.push_back(c);
        }
        break;
    case Kind::Integers:
        for (const std::string& part : parts) {
            int32_t i = 0;
            if (!ParseInt(part, &i)) {
                *error = "'" + part + "' isn't a whole number";
                return false;
            }
            v.list.push_back(static_cast<uint32_t>(i));
        }
        break;
    case Kind::Floats:
        for (const std::string& part : parts) {
            float f = 0.0f;
            if (!ParseFloat(part, &f)) {
                *error = "'" + part + "' isn't a number";
                return false;
            }
            v.floats.push_back(f);
        }
        break;
    case Kind::Other:
        *error = "not editable";
        return false;
    }
    if (kind == Kind::Names || kind == Kind::Integers || kind == Kind::Floats) {
        if (v.list.size() > 256 || v.floats.size() > 256) {
            *error = "too many (256 at most)";
            return false;
        }
    }
    *value = v;
    error->clear();
    return true;
}

std::string Escape(const std::string& text)
{
    std::string out;
    for (unsigned char c : text) {
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                     c == '-' || c == '/' || c == ':';
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

bool Unescape(const std::string& text, std::string* out)
{
    out->clear();
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c != '%') {
            out->push_back(c);
            continue;
        }
        if (i + 2 >= text.size())
            return false;
        int value = 0;
        for (int k = 1; k <= 2; ++k) {
            char h = text[i + k];
            int digit = h >= '0' && h <= '9' ? h - '0' : h >= 'A' && h <= 'F' ? h - 'A' + 10 : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1;
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

std::string EncodeValue(const Value& value, const NameLookup& names)
{
    char text[96];
    switch (value.kind) {
    case Kind::Integer: return "i:" + std::to_string(value.integer);
    case Kind::Float:
        std::snprintf(text, sizeof(text), "f:%.9g", static_cast<double>(value.numbers[0]));
        return text;
    case Kind::Pair:
        std::snprintf(text, sizeof(text), "p:%.9g,%.9g", static_cast<double>(value.numbers[0]),
                      static_cast<double>(value.numbers[1]));
        return text;
    case Kind::Vector:
        std::snprintf(text, sizeof(text), "v:%.9g,%.9g,%.9g", static_cast<double>(value.numbers[0]),
                      static_cast<double>(value.numbers[1]), static_cast<double>(value.numbers[2]));
        return text;
    case Kind::Name: return "n:" + RecordName(value.checksum, names);
    case Kind::String: return "s:" + Escape(value.text);
    case Kind::LocalString: return "l:" + Escape(value.text);
    case Kind::Flag: return value.on ? "flag:1" : "flag:0";
    case Kind::Names: {
        std::vector<std::string> parts;
        for (uint32_t c : value.list)
            parts.push_back(RecordName(c, names));
        return "an:" + Join(parts, ",");
    }
    case Kind::Integers: {
        std::vector<std::string> parts;
        for (uint32_t i : value.list)
            parts.push_back(std::to_string(static_cast<int32_t>(i)));
        return "ai:" + Join(parts, ",");
    }
    case Kind::Floats: {
        std::vector<std::string> parts;
        for (float f : value.floats) {
            std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(f));
            parts.push_back(text);
        }
        return "af:" + Join(parts, ",");
    }
    case Kind::Other: break;
    }
    return "";
}

bool DecodeValue(const std::string& text, Value* value)
{
    size_t colon = text.find(':');
    if (colon == std::string::npos)
        return false;
    std::string type = text.substr(0, colon), body = text.substr(colon + 1);
    std::string error, plain;
    // (lists are comma-separated in records; Parse takes commas)
    if (type == "i")
        return Parse(Kind::Integer, body, value, &error);
    if (type == "f")
        return Parse(Kind::Float, body, value, &error);
    if (type == "p")
        return Parse(Kind::Pair, body, value, &error);
    if (type == "v")
        return Parse(Kind::Vector, body, value, &error);
    if (type == "n")
        return Parse(Kind::Name, body, value, &error);
    if (type == "s" || type == "l")
        return Unescape(body, &plain) && Parse(type == "s" ? Kind::String : Kind::LocalString, plain, value, &error);
    if (type == "flag") {
        if (body != "0" && body != "1")
            return false;
        return Parse(Kind::Flag, body, value, &error);
    }
    if (type == "an")
        return Parse(Kind::Names, body, value, &error);
    if (type == "ai")
        return Parse(Kind::Integers, body, value, &error);
    if (type == "af")
        return Parse(Kind::Floats, body, value, &error);
    return false;
}

std::string Token(const Edit& edit, const NameLookup& names)
{
    return "f." + RecordName(edit.key, names) + "=" + (edit.removed ? std::string("del") : EncodeValue(edit.value, names));
}

bool IsFieldKey(const std::string& key) { return key.size() > 2 && key[0] == 'f' && key[1] == '.'; }

bool ReadToken(const std::string& key, const std::string& value, Edit* edit)
{
    if (!IsFieldKey(key))
        return false;
    Edit e;
    if (!ParseName(key.substr(2), &e.key))
        return false;
    if (value == "del") {
        e.removed = true;
    } else if (!DecodeValue(value, &e.value)) {
        return false;
    }
    *edit = e;
    return true;
}

uint32_t KeyName() { static const uint32_t k = NameChecksum("name"); return k; }
uint32_t KeyClass() { static const uint32_t k = NameChecksum("Class"); return k; }
uint32_t KeyType() { static const uint32_t k = NameChecksum("Type"); return k; }
uint32_t KeyPos() { static const uint32_t k = NameChecksum("pos"); return k; }
uint32_t KeyAngles() { static const uint32_t k = NameChecksum("angles"); return k; }
uint32_t KeyLinks() { static const uint32_t k = NameChecksum("links"); return k; }
uint32_t KeyTerrain() { static const uint32_t k = NameChecksum("TerrainType"); return k; }
uint32_t KeyRestartTypes() { static const uint32_t k = NameChecksum("restart_types"); return k; }

When WhenApplied(Nodes::Family family, uint32_t key, const char** note)
{
    const char* dummy = nullptr;
    const char** out = note ? note : &dummy;
    if (key == KeyName() || key == KeyClass() || key == KeyType()) {
        *out = "Read-only";
        return When::ReadOnly;
    }
    if (key == KeyPos() || key == KeyAngles()) {
        *out = "Edit with Position / Heading";
        return When::Elsewhere;
    }
    if (key == KeyLinks()) {
        *out = "Live";
        return When::Live;
    }
    if (key == KeyTerrain() && family == Nodes::Family::Rail) {
        *out = "Edit with Terrain";
        return When::Elsewhere;
    }
    if (key == KeyRestartTypes() && family == Nodes::Family::Spawn) {
        *out = "Live";
        return When::Live;
    }
    *out = "Applies on level reload";
    return When::Reload;
}

bool ValidLinks(const std::vector<uint32_t>& links, uint32_t self, const std::function<bool(uint32_t)>& exists,
                const NameLookup& names, std::string* error)
{
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i] == self) {
            *error = "a node can't link to itself";
            return false;
        }
        if (!exists(links[i])) {
            *error = "no node named " + NameText(links[i], names) + " in this level";
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (links[j] == links[i]) {
                *error = NameText(links[i], names) + " is linked twice";
                return false;
            }
        }
    }
    error->clear();
    return true;
}

void MoveLastTo(Script::CStruct* s, int index)
{
    Script::CComponent* previous = nullptr;
    Script::CComponent* last = s->m_head;
    while (last && last->next) {
        previous = last;
        last = last->next;
    }
    if (!last || !previous || index < 0)
        return; // (the only one, or none: in its place already)
    int others = 0;
    Script::CComponent* before = nullptr;
    for (Script::CComponent* c = s->m_head; c != last && others < index; c = c->next) {
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
        last->next = s->m_head;
        s->m_head = last;
    }
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

std::unordered_map<uint32_t, std::string> g_names; // (names typed in this session)

Script::CStruct* Included(const Script::CComponent* c)
{
    if (c->name != 0 || c->Type() != Script::ESYMBOLTYPE_NAME)
        return nullptr;
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(c->checksum);
    return symbol && symbol->type == Script::ESYMBOLTYPE_STRUCTURE ? symbol->structure : nullptr;
}

bool IsFlag(const Script::CComponent* c)
{
    return c->name == 0 && c->Type() == Script::ESYMBOLTYPE_NAME && !Included(c);
}

// The key a component goes by: its name, a bare flag's name.
uint32_t KeyOf(const Script::CComponent* c) { return IsFlag(c) ? c->checksum : c->name; }

Value ValueOf(const Script::CComponent* c)
{
    Value v;
    if (IsFlag(c)) {
        v.kind = Kind::Flag;
        v.on = true;
        return v;
    }
    switch (c->Type()) {
    case Script::ESYMBOLTYPE_INTEGER:
        v.kind = Kind::Integer;
        v.integer = c->integer;
        break;
    case Script::ESYMBOLTYPE_FLOAT:
        v.kind = Kind::Float;
        v.numbers[0] = c->floatValue;
        break;
    case Script::ESYMBOLTYPE_NAME:
        v.kind = Kind::Name;
        v.checksum = c->checksum;
        break;
    case Script::ESYMBOLTYPE_STRING:
        v.kind = Kind::String;
        v.text = c->string ? c->string : "";
        break;
    case Script::ESYMBOLTYPE_LOCALSTRING:
        v.kind = Kind::LocalString;
        v.text = c->string ? c->string : "";
        break;
    case Script::ESYMBOLTYPE_PAIR:
        v.kind = Kind::Pair;
        if (c->pair) {
            v.numbers[0] = c->pair->x;
            v.numbers[1] = c->pair->y;
        }
        break;
    case Script::ESYMBOLTYPE_VECTOR:
        v.kind = Kind::Vector;
        if (c->vector) {
            v.numbers[0] = c->vector->x;
            v.numbers[1] = c->vector->y;
            v.numbers[2] = c->vector->z;
        }
        break;
    case Script::ESYMBOLTYPE_ARRAY:
    case Script::ESYMBOLTYPE_ARRAYPOINTER: {
        Script::CArray* array = c->array;
        uint32_t size = array ? array->m_size : 0;
        uint8_t type = array ? static_cast<uint8_t>(array->m_type) : 0;
        if (size == 0 || type == Script::ESYMBOLTYPE_NAME) {
            v.kind = Kind::Names;
            for (uint32_t i = 0; i < size; ++i)
                v.list.push_back(array->GetElement(i));
        } else if (type == Script::ESYMBOLTYPE_INTEGER) {
            v.kind = Kind::Integers;
            for (uint32_t i = 0; i < size; ++i)
                v.list.push_back(array->GetElement(i));
        } else if (type == Script::ESYMBOLTYPE_FLOAT) {
            v.kind = Kind::Floats;
            for (uint32_t i = 0; i < size; ++i)
                v.floats.push_back(array->GetFloat(i));
        } else {
            v.kind = Kind::Other;
            v.text = "[" + std::to_string(size) + " items]";
        }
        break;
    }
    case Script::ESYMBOLTYPE_STRUCTURE:
    case Script::ESYMBOLTYPE_STRUCTUREPOINTER:
        v.kind = Kind::Other;
        v.text = "{a structure}";
        break;
    case Script::ESYMBOLTYPE_QSCRIPT:
        v.kind = Kind::Other;
        v.text = "(a script)";
        break;
    default:
        v.kind = Kind::Other;
        v.text = "?";
        break;
    }
    return v;
}

// The value added to `s` as a new component at its end.
void AddValue(Script::CStruct* s, uint32_t key, const Value& v)
{
    switch (v.kind) {
    case Kind::Integer: s->AddInteger(key, v.integer); break;
    case Kind::Float: s->AddFloat(key, v.numbers[0]); break;
    case Kind::Name: s->AddChecksum(key, v.checksum); break;
    case Kind::String: s->AddString(key, v.text.c_str()); break;
    case Kind::LocalString: s->AddLocalString(key, v.text.c_str()); break;
    case Kind::Pair: s->AddPair(key, v.numbers[0], v.numbers[1]); break;
    case Kind::Vector: s->AddVector(key, v.numbers[0], v.numbers[1], v.numbers[2]); break;
    case Kind::Flag:
        if (v.on)
            s->AddChecksum(0u, key);
        break;
    case Kind::Names:
    case Kind::Integers:
    case Kind::Floats: {
        Script::CArray* array = Script::NewArray()->Init();
        uint32_t size = static_cast<uint32_t>(v.kind == Kind::Floats ? v.floats.size() : v.list.size());
        array->SetSizeAndType(size, v.kind == Kind::Names      ? Script::ESYMBOLTYPE_NAME
                                    : v.kind == Kind::Integers ? Script::ESYMBOLTYPE_INTEGER
                                                               : Script::ESYMBOLTYPE_FLOAT);
        for (uint32_t i = 0; i < size; ++i) {
            if (v.kind == Kind::Floats)
                array->SetFloat(i, v.floats[i]);
            else
                array->SetElement(i, v.list[i]);
        }
        s->AddArrayPointer(key, array);
        break;
    }
    case Kind::Other: break;
    }
}

bool Matches(const Script::CComponent* c, uint32_t key, bool flag)
{
    return flag ? IsFlag(c) && c->checksum == key : c->name == key && !IsFlag(c);
}

// The node's own field: the last own component of that key (flags: the bare name), and its place - how many of the
// node's other components come before it.
Own OwnOf(Script::CStruct* s, uint32_t key, bool flag)
{
    Own own;
    int others = 0;
    for (Script::CComponent* c = s->m_head; c; c = c->next) {
        if (Matches(c, key, flag)) {
            own.present = true;
            own.value = ValueOf(c);
            own.index = others;
        } else {
            ++others;
        }
    }
    return own;
}

// Whether a shared structure of `s` after its own component's place (`index` others before it; -1: anywhere) has the
// key - which then counts over the node's own.
bool IncludedAfter(Script::CStruct* s, uint32_t key, bool flag, int index)
{
    int others = 0;
    for (Script::CComponent* c = s->m_head; c; c = c->next) {
        if (Matches(c, key, flag))
            continue;
        if (Script::CStruct* included = Included(c)) {
            if (others >= index &&
                (flag ? included->ContainsFlag(key) : included->FindLastComponent(key) != nullptr))
                return true;
        }
        ++others;
    }
    return false;
}

// The node's own field made `own`: the old one(s) taken out, the new one put where `own` says (-1: at the end).
void PutOwn(Script::CStruct* s, uint32_t key, bool flag, const Own& own)
{
    if (flag)
        s->RemoveFlag(key);
    else
        s->RemoveComponent(key);
    if (!own.present || (flag && !own.value.on))
        return;
    Value flagOn;
    flagOn.kind = Kind::Flag;
    flagOn.on = true;
    AddValue(s, key, flag ? flagOn : own.value);
    if (own.index >= 0)
        MoveLastTo(s, own.index);
}

struct Touched {
    uint32_t node, key;
    bool flag;
    Own origin;
    Script::CStruct* structure;
};
std::vector<Touched> g_touched;

Touched* FindTouched(uint32_t node, uint32_t key)
{
    for (Touched& t : g_touched) {
        if (t.node == node && t.key == key)
            return &t;
    }
    return nullptr;
}

Script::CStruct* LevelNode(uint32_t name)
{
    return name && SkateScript::NodeManagerInstance() ? SkateScript::GetNode(name, false) : nullptr;
}

// Whether `key` is a flag of the node (its own or a shared structure's), or should be taken as one (`value` a flag).
bool KeyIsFlag(Script::CStruct* s, uint32_t key, const Value* value)
{
    if (value && value->kind == Kind::Flag)
        return true;
    for (Script::CComponent* c = s->m_head; c; c = c->next) {
        if (Matches(c, key, true))
            return true;
        if (Matches(c, key, false))
            return false;
        if (Script::CStruct* included = Included(c)) {
            if (included->ContainsFlag(key) && !included->FindLastComponent(key))
                return true;
        }
    }
    return false;
}

bool SameOwn(const Own& a, const Own& b, bool flag)
{
    if (flag)
        return a.present == b.present;
    return a.present == b.present && (!a.present || (SameValue(a.value, b.value) && a.index == b.index));
}

const Nodes::Node* LayerNode(uint32_t name)
{
    int index = Nodes::Find(name);
    return index >= 0 ? &Nodes::All()[index] : nullptr;
}

} // namespace

Own OwnIn(Script::CStruct* s, uint32_t key, bool flag) { return OwnOf(s, key, flag); }
void PutIn(Script::CStruct* s, uint32_t key, bool flag, const Own& own) { PutOwn(s, key, flag, own); }

const char* NameOf(uint32_t checksum)
{
    auto it = g_names.find(checksum);
    if (it != g_names.end())
        return it->second.c_str();
    return Objects::NameOf(checksum);
}

void Remember(const std::string& name)
{
    if (IsIdentifier(name))
        g_names[NameChecksum(name)] = name;
}

std::vector<Row> RowsOf(const Nodes::Node& node)
{
    std::vector<Row> rows;
    Script::CStruct* s = Nodes::StructOf(node);
    if (!s)
        return rows;
    struct Entry {
        uint32_t key;
        Script::CComponent* c;
        bool included;
    };
    std::vector<Entry> entries;
    for (Script::CComponent* c = s->m_head; c; c = c->next) {
        if (Script::CStruct* included = Included(c)) {
            for (Script::CComponent* inner = included->m_head; inner; inner = inner->next) {
                if (!Included(inner)) // (one level of shared structures: the node arrays have no deeper ones)
                    entries.push_back({KeyOf(inner), inner, true});
            }
            continue;
        }
        entries.push_back({KeyOf(c), c, false});
    }
    std::vector<uint32_t> order;
    for (const Entry& e : entries) {
        if (std::find(order.begin(), order.end(), e.key) == order.end())
            order.push_back(e.key);
    }
    NameLookup names = NameOf;
    for (uint32_t key : order) {
        const Entry* effective = nullptr;
        bool anyIncluded = false, anyOwn = false;
        for (const Entry& e : entries) {
            if (e.key != key)
                continue;
            effective = &e;
            anyIncluded = anyIncluded || e.included;
            anyOwn = anyOwn || !e.included;
        }
        Row row;
        row.key = key;
        row.value = ValueOf(effective->c);
        row.included = effective->included;
        row.overridden = !effective->included && anyIncluded;
        row.when = WhenApplied(node.family, key, &row.note);
        if (Touched* t = FindTouched(node.name, key)) {
            Own now = OwnOf(s, key, t->flag);
            row.edited = !SameOwn(now, t->origin, t->flag);
        }
        row.editable = true;
        if (node.added) {
            row.why = "Set by the node type";
        } else if (row.when == When::ReadOnly || row.when == When::Elsewhere) {
            row.why = row.note;
        } else if (row.value.kind == Kind::Other) {
            row.why = "Not editable here";
        } else if (row.value.kind == Kind::Flag && effective->included && !anyOwn) {
            row.why = "Shared by the class";
        } else if (node.hidden && (key == Nodes::HiddenClassKey() || key == Nodes::HiddenTypeKey())) {
            row.why = "Editor data for a hidden node";
        }
        row.editable = row.why.empty();
        rows.push_back(row);
    }
    return rows;
}

namespace {

// The field set on structure `s` of node `name` (of `family`: what may be set - see Set).
bool SetOn(Script::CStruct* s, uint32_t name, Nodes::Family family, uint32_t key, const Value& value, bool removed,
           std::string* error)
{
    When when = WhenApplied(family, key, nullptr);
    if (when == When::ReadOnly || when == When::Elsewhere || key == KeyLinks() || key == 0) {
        *error = "Field not editable";
        return false;
    }
    if (!removed && value.kind == Kind::Other) {
        *error = "Not an editable value";
        return false;
    }
    bool flag = KeyIsFlag(s, key, removed ? nullptr : &value);
    Own now = OwnOf(s, key, flag);
    Own want;
    if (flag) {
        bool on = !removed && value.on;
        if (!on && !now.present && IncludedAfter(s, key, true, -1)) {
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
        want.index = now.present && !IncludedAfter(s, key, false, now.index) ? now.index : -1;
    }
    if (!FindTouched(name, key))
        g_touched.push_back({name, key, flag, now, s});
    PutOwn(s, key, flag, want);
    error->clear();
    return true;
}

} // namespace

bool Set(uint32_t name, uint32_t key, const Value& value, bool removed, std::string* error)
{
    Script::CStruct* s = LevelNode(name);
    if (!s) {
        *error = "No such node in this level";
        return false;
    }
    if (NodeCreate::IsAdded(name)) {
        *error = "Set by the node type";
        return false;
    }
    const Nodes::Node* layer = LayerNode(name);
    if (!SetOn(s, name, layer ? layer->family : Nodes::Family::Other, key, value, removed, error))
        return false;
    Nodes::Invalidate(); // (the node layer's copies: restart types, radius, colour...)
    return true;
}

bool Capture(uint32_t name, uint32_t key, Own* own)
{
    Script::CStruct* s = LevelNode(name);
    if (!s)
        return false;
    Touched* t = FindTouched(name, key);
    *own = OwnOf(s, key, t ? t->flag : KeyIsFlag(s, key, nullptr));
    return true;
}

bool Put(uint32_t name, uint32_t key, const Own& own)
{
    Script::CStruct* s = LevelNode(name);
    if (!s)
        return false;
    Touched* t = FindTouched(name, key);
    bool flag = t ? t->flag : own.value.kind == Kind::Flag;
    PutOwn(s, key, flag, own);
    Nodes::Invalidate();
    return true;
}

bool Origin(uint32_t name, uint32_t key, Own* own)
{
    Touched* t = FindTouched(name, key);
    if (!t)
        return false;
    *own = t->origin;
    return true;
}

bool ResetNode(uint32_t name)
{
    bool any = false;
    for (const Touched& t : g_touched) {
        if (t.node == name && LevelNode(name) == t.structure) {
            PutOwn(t.structure, t.key, t.flag, t.origin);
            any = true;
        }
    }
    if (any)
        Nodes::Invalidate();
    return any;
}

bool NodeEdited(uint32_t name)
{
    for (const Touched& t : g_touched) {
        if (t.node == name && LevelNode(name) == t.structure && !SameOwn(OwnOf(t.structure, t.key, t.flag), t.origin, t.flag))
            return true;
    }
    return false;
}

std::vector<uint32_t> TouchedKeys(uint32_t name)
{
    std::vector<uint32_t> keys;
    for (const Touched& t : g_touched) {
        if (t.node == name)
            keys.push_back(t.key);
    }
    return keys;
}

std::vector<NodeEdits> Changed()
{
    std::vector<NodeEdits> out;
    for (const Touched& t : g_touched) {
        Script::CStruct* s = LevelNode(t.node);
        if (!s || s != t.structure)
            continue;
        Own now = OwnOf(s, t.key, t.flag);
        if (SameOwn(now, t.origin, t.flag))
            continue;
        Edit edit;
        edit.key = t.key;
        if (t.flag) {
            edit.value.kind = Kind::Flag;
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

int Apply(const std::vector<NodeEdits>& edits)
{
    int applied = 0;
    for (const NodeEdits& node : edits) {
        for (const Edit& edit : node.edits) {
            std::string error;
            if (edit.key == KeyLinks())
                continue; // (links: the chains' records)
            if (Set(node.name, edit.key, edit.value, edit.removed, &error))
                ++applied;
        }
    }
    return applied;
}

void ResetAll()
{
    for (const Touched& t : g_touched) {
        if (LevelNode(t.node) == t.structure)
            PutOwn(t.structure, t.key, t.flag, t.origin);
    }
    if (!g_touched.empty())
        Nodes::Invalidate();
}

void ForgetLevel()
{
    ResetAll(); // (only where the structure is still the one changed: the same level's array kept loaded)
    g_touched.clear();
}

int ApplyEarly(uint32_t array, const std::vector<NodeEdits>& edits)
{
    // (the level's objects aren't made yet: the node layer isn't asked - only the node manager's structures)
    if (!array || !SkateScript::NodeManagerInstance())
        return 0;
    int applied = 0;
    for (const NodeEdits& node : edits) {
        Script::CStruct* s = LevelNode(node.name);
        if (!s)
            continue;
        for (const Edit& edit : node.edits) {
            std::string error;
            if (edit.key != KeyLinks() &&
                SetOn(s, node.name, Nodes::Family::Other, edit.key, edit.value, edit.removed, &error))
                ++applied;
        }
    }
    return applied;
}

} // namespace Front::LevelEditor::Fields
