// NOT ORIGINAL - thug2-decomp.ini as text (ini_text.h).

#include "engine/config/ini_text.h"

#include <cctype>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/config/settings_model.h"

namespace config {

namespace {

std::string get_trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool get_same(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// One line of the text: where it starts, ends (before its line break) and where the next one starts.
struct Seg {
    size_t start, end, next;
};
std::vector<Seg> get_lines(const std::string& text)
{
    std::vector<Seg> lines;
    size_t at = 0;
    while (at < text.size()) {
        const size_t nl = text.find('\n', at);
        const size_t end = nl == std::string::npos ? text.size() : nl;
        const size_t content = end > at && core::at(text, end - 1) == '\r' ? end - 1 : end;
        lines.push_back({.start = at, .end = content, .next = nl == std::string::npos ? text.size() : nl + 1});
        at = nl == std::string::npos ? text.size() : nl + 1;
    }
    return lines;
}

// The line's section name if it is a section header ("[name]"), else false.
bool section_of(const std::string& line, std::string* name)
{
    const std::string trimmed = get_trim(line);
    if (trimmed.size() < 2 || trimmed.front() != '[') {
        return false;
    }
    const size_t close = trimmed.find(']');
    if (close == std::string::npos) {
        return false;
    }
    *name = get_trim(trimmed.substr(1, close - 1));
    return true;
}

// The line's key if it is a key=value line (not a comment), and where its '=' is.
bool get_key_of(const std::string& line, std::string* key, size_t* equals)
{
    const std::string trimmed = get_trim(line);
    if (trimmed.empty() || core::at(trimmed, 0) == ';' || core::at(trimmed, 0) == '#' || core::at(trimmed, 0) == '[') {
        return false;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    *key = get_trim(line.substr(0, eq));
    *equals = eq;
    return !key->empty();
}

} // namespace

bool ini_get(const std::string& text, const IniKey& at, std::string* value)
{
    bool in_section = false;
    for (const Seg& segment : get_lines(text)) {
        const std::string line = text.substr(segment.start, segment.end - segment.start);
        std::string name;
        if (section_of(line, &name)) {
            if (in_section) {
                return false; // (only the first section of the name: Windows reads no other)
            }
            in_section = get_same(name, at.section);
            continue;
        }
        size_t eq = 0;
        if (in_section && get_key_of(line, &name, &eq) && get_same(name, at.key)) {
            if (value != nullptr) {
                *value = get_trim(line.substr(eq + 1));
            }
            return true;
        }
    }
    return false;
}

std::string ini_set(const std::string& text, const IniKey& at, const std::string& value)
{
    const std::string eol =
        text.find("\r\n") != std::string::npos || text.find('\n') == std::string::npos ? "\r\n" : "\n";
    const std::vector<Seg> lines = get_lines(text);
    bool in_section = false;
    bool found = false;
    size_t insert_at = std::string::npos; // after the section's last line that isn't blank
    for (const Seg& segment : lines) {
        const std::string line = text.substr(segment.start, segment.end - segment.start);
        std::string name;
        if (section_of(line, &name)) {
            if (in_section) {
                break; // (the section ended)
            }
            in_section = get_same(name, at.section);
            if (in_section) {
                found = true;
                insert_at = segment.next;
            }
            continue;
        }
        if (!in_section) {
            continue;
        }
        size_t eq = 0;
        if (get_key_of(line, &name, &eq) && get_same(name, at.key)) {
            // (the value replaced; whatever stood after the line - its break - kept)
            return text.substr(0, segment.start + eq + 1) + value + text.substr(segment.end);
        }
        if (!get_trim(line).empty()) {
            insert_at = segment.next;
        }
    }
    const std::string added = at.key + "=" + value;
    if (found) {
        std::string before = text.substr(0, insert_at);
        // (the line before has no break when it is the file's last)
        if (!before.empty() && before.back() != '\n') {
            before += eol;
        }
        return before + added + eol + text.substr(insert_at);
    }
    std::string out = text;
    if (!out.empty() && out.back() != '\n') {
        out += eol;
    }
    if (!out.empty()) {
        out += eol;
    }
    return out + "[" + at.section + "]" + eol + added + eol;
}

IniValues ini_values(const std::string& text)
{
    IniValues values;
    std::set<std::string> sections_seen;
    std::string section;
    bool counted = false; // (the first section of its name: its keys count)
    for (const Seg& segment : get_lines(text)) {
        const std::string line = text.substr(segment.start, segment.end - segment.start);
        std::string name;
        if (section_of(line, &name)) {
            section = name;
            std::string lowered = name;
            for (char& ch : lowered) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            counted = sections_seen.insert(lowered).second;
            continue;
        }
        size_t eq = 0;
        if (counted && get_key_of(line, &name, &eq)) {
            values.emplace(ini_slot(section.c_str(), name.c_str()), get_trim(line.substr(eq + 1))); // (the first kept)
        }
    }
    return values;
}

} // namespace config
