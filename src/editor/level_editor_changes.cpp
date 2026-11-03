// NOT ORIGINAL - the level editor's Changes panel (ours). See level_editor_changes.h.

#include "editor/level_editor_changes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor_fields.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor.h"

namespace frontend::level_editor::changes {

namespace {

constexpr std::size_t kChecksumDigits = 8; // hex digits of a checksum
constexpr std::size_t kNumberTextSize = 32; // a formatted number
constexpr std::size_t kPrefixedChecksumLength = 10; // "0x" and a checksum's digits
constexpr std::size_t kScaleKeyLength = 6; // "scale="
constexpr int kDegreesPerQuarter = 90;
constexpr double kHalfTurnDegrees = 180.0;
constexpr double kPiDouble = 3.14159265358979;
constexpr std::size_t kSummaryTextSize = 96; // " at (x, y, z), heading h"

struct GroupRow {
    const char* keyword;
    const char* group;
};
constexpr auto kGroups = std::to_array<GroupRow>({
    {.keyword = "level", .group = "Level properties"},
    {.keyword = "sector", .group = "Scenery"},
    {.keyword = "clone", .group = "Scenery copies"},
    {.keyword = "rail", .group = "Rail points"},
    {.keyword = "object", .group = "Objects"},
    {.keyword = "copy", .group = "Object copies"},
    {.keyword = "set", .group = "Nodes moved / fields"},
    {.keyword = "node", .group = "Nodes added"},
    {.keyword = "hide", .group = "Nodes hidden"},
    {.keyword = "order", .group = "Nodes added"},
    {.keyword = "trigger", .group = "Triggers"},
    {.keyword = "proxim", .group = "Triggers"},
    {.keyword = "point", .group = "Chains"},
    {.keyword = "links", .group = "Chains"},
    {.keyword = "drop", .group = "Chains"},
    {.keyword = "terrain", .group = "Chains"},
    {.keyword = "facing", .group = "Chains"},
    {.keyword = "grindflat", .group = "Chains"},
    {.keyword = "railspeed", .group = "Chains"},
    {.keyword = "speed", .group = "Peds, cars, sounds"},
    {.keyword = "emitter", .group = "Peds, cars, sounds"},
    {.keyword = "wall", .group = "Collision and walls"},
    {.keyword = "faces", .group = "Collision and walls"},
    {.keyword = "cluster", .group = "Trick objects"},
    {.keyword = "camera", .group = "Cameras"},
    {.keyword = "lock", .group = "Locks"},
});

std::vector<std::string> get_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

bool is_hex8(const std::string& w)
{
    if (w.size() != kChecksumDigits)
        return false;
    for (const char c : w) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

std::string get_num(float v)
{
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "%.0f", v);
    return text.data();
}

} // namespace

const char* group_of(const std::string& keyword)
{
    for (const GroupRow& g : kGroups) {
        if (keyword == g.keyword)
            return g.group;
    }
    return nullptr;
}

const std::vector<const char*>& bunches()
{
    static const std::vector<const char*> kGroups = {
        "Level properties",     "Scenery",       "Scenery copies", "Rail points", "Objects", "Object copies",
        "Nodes moved / fields", "Nodes added",   "Nodes hidden",   "Triggers",    "Chains",  "Peds, cars, sounds",
        "Collision and walls",  "Trick objects", "Cameras",        "Locks"};
    return kGroups;
}

uint32_t id_of(const std::string& word)
{
    std::string w = word;
    if (w.size() == kPrefixedChecksumLength && core::at(w, 0) == '0' && (core::at(w, 1) == 'x' || core::at(w, 1) == 'X'))
        w = w.substr(2);
    if (is_hex8(w))
        return static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, kHexBase));
    return w.empty() ? 0 : fields::hash_of_name(w);
}

std::vector<TableRow> link_chain(const std::string& layout_text, const Labeler& label)
{
    std::vector<TableRow> out;
    std::vector<std::string> lines = get_lines(layout_text);
    for (int n = 0; std::cmp_less(n, lines.size()); ++n) {
        std::string line = core::at(lines, n);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string comment;
        if (const size_t hash = line.find('#'); hash != std::string::npos) {
            comment = line.substr(hash + 1);
            const size_t a = comment.find_first_not_of(' ');
            comment = a == std::string::npos ? "" : comment.substr(a);
            line.resize(hash);
        }
        std::istringstream in(line);
        std::string keyword;
        if (!(in >> keyword))
            continue;
        const char* group = group_of(keyword);
        if (!group)
            continue;
        TableRow e;
        e.line = n;
        e.keyword = keyword;
        e.group = group;
        std::vector<std::string> words;
        for (std::string w; in >> w;)
            words.push_back(w);
        // (a name for the record's subject: the labeler's, else the comment's, else what the record says)
        auto name_of = [&](uint32_t id, const std::string& fallback) {
            std::string name = label ? label(id) : "";
            if (name.empty())
                name = comment;
            return name.empty() ? fallback : name;
        };
        auto number = [&](size_t i) { return i < words.size() ? std::strtof(core::at(words, i).c_str(), nullptr) : 0.0f; };
        auto scale_of = [&]() -> std::string {
            for (const std::string& w : words) {
                if (w.starts_with("scale="))
                    return ", scaled " + w.substr(kScaleKeyLength);
            }
            return "";
        };
        if (keyword == "level") {
            std::string what;
            for (const std::string& w : words) {
                const std::string key = w.substr(0, w.find('='));
                what += (what.empty() ? "" : ", ") + key;
            }
            e.summary = "Level properties: " + (what.empty() ? std::string("edited") : what);
        } else if (keyword == "sector" || keyword == "rail" || keyword == "clone" || keyword == "wall") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            const std::string name = name_of(e.id, words.empty() ? "?" : core::at(words, 0));
            const int quarters = static_cast<int>(number(1));
            const float x = number(2);
            const float y = number(3);
            const float z = number(4);
            const bool hidden = words.size() > 5 && core::at(words, 5) == "1" && keyword != "clone";
            std::string what;
            if (keyword == "clone")
                what = "a copy of " + name;
            else
                what = name;
            if (x != 0.0f || y != 0.0f || z != 0.0f)
                what += " moved (" + get_num(x) + ", " + get_num(y) + ", " + get_num(z) + ")";
            if (quarters)
                what += std::string(" turned ") + std::to_string(quarters * kDegreesPerQuarter) + "\xC2\xB0";
            what += scale_of();
            if (keyword == "wall")
                what += hidden ? " - collision off" : "";
            else if (hidden)
                what += " - hidden";
            if (keyword == "rail")
                what.insert(0, "Rail point ");
            e.summary = what;
        } else if (keyword == "object" || keyword == "copy") {
            const bool copy = keyword == "copy";
            const uint32_t source = copy && words.size() > 1 ? id_of(core::at(words, 1)) : 0;
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            const size_t at = copy ? 2 : 1;
            const float x = number(at);
            const float y = number(at + 1);
            const float z = number(at + 2);
            // (the at row: the heading)
            const float ax = number(at + 9);
            const float az = number(at + 11);
            auto heading = static_cast<float>(std::atan2(static_cast<double>(ax), static_cast<double>(az)) * kHalfTurnDegrees /
                                              kPiDouble);
            const std::string name = copy ? "a copy of " + name_of(source, words.size() > 1 ? core::at(words, 1) : "?")
                                          : name_of(e.id, words.empty() ? "?" : core::at(words, 0));
            std::array<char, kSummaryTextSize> text{};
            text::format_to(text, " at (%.0f, %.0f, %.0f), heading %.0f\xC2\xB0", x, y, z, heading);
            e.summary = name + text.data() + scale_of();
        } else if (keyword == "set") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            const std::string name = name_of(e.id, words.empty() ? "?" : core::at(words, 0));
            bool pos = false;
            bool angles = false;
            int fields = 0;
            for (size_t i = 1; i < words.size(); ++i) {
                pos = pos || core::at(words, i).starts_with("pos=");
                angles = angles || core::at(words, i).starts_with("angles=");
                fields += core::at(words, i).starts_with("f.") ? 1 : 0;
            }
            std::string what = pos ? (angles ? "moved / turned" : "moved") : angles ? "turned" : "";
            if (fields)
                what += (what.empty() ? "" : ", ") + std::to_string(fields) + (fields == 1 ? " field" : " fields") +
                        " edited";
            e.summary = name + ": " + (what.empty() ? std::string("edited") : what);
        } else if (keyword == "node" || keyword == "point" || keyword == "camera") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            const std::string cls = keyword == "camera" ? "camera" : words.size() > 1 ? core::at(words, 1) : "?";
            e.summary = std::string(keyword == "point"    ? "Point "
                                    : keyword == "camera" ? "Camera "
                                                          : "Added ") +
                        (words.empty() ? "?" : core::at(words, 0)) + " (" + cls + ")";
        } else if (keyword == "order") {
            e.summary =
                "The added crowns' order: " + std::to_string(!words.empty() ? words.size() - 1 : 0) + " crowns";
        } else if (keyword == "hide" || keyword == "drop") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) +
                        (keyword == "drop" ? " deleted (a chain point)" : " hidden");
        } else if (keyword == "lock") {
            e.id = words.size() > 1 ? id_of(core::at(words, 1)) : 0;
            e.summary =
                name_of(e.id, words.size() > 1 ? core::at(words, 1) : "?") + " locked (" + (words.empty() ? "?" : core::at(words, 0)) + ")";
        } else if (keyword == "trigger") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            e.summary = "Trigger " + (words.empty() ? std::string("?") : core::at(words, 0)) +
                        (words.size() > 1 ? " (" + core::at(words, 1) + ")" : "");
        } else if (keyword == "proxim") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) + ": proximity scripts / shape";
        } else if (keyword == "links") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            std::string to = words.size() > 1 ? core::at(words, 1) : "-";
            const int count = to == "-" ? 0 : 1 + static_cast<int>(std::count(to.begin(), to.end(), ','));
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) + " relinked (" + std::to_string(count) +
                        (count == 1 ? " link)" : " links)");
        } else if (keyword == "terrain" || keyword == "facing" || keyword == "railspeed" || keyword == "grindflat" ||
                   keyword == "speed") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            const std::string value = words.size() > 1 ? core::at(words, 1) : "";
            const char* what = keyword == "terrain"     ? " terrain "
                               : keyword == "facing"    ? " facing "
                               : keyword == "railspeed" ? " speed-up "
                               : keyword == "speed"     ? " path speed "
                                                        : " grindable on flat ground";
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) + what + value;
        } else if (keyword == "emitter") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) + ": sound emitter's sound / volume / radius";
        } else if (keyword == "faces") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            std::string list = words.size() > 1 ? core::at(words, 1) : "";
            const int count = list.empty() ? 0 : 1 + static_cast<int>(std::count(list.begin(), list.end(), ','));
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) + ": " + std::to_string(count) +
                        (count == 1 ? " face's flags" : " faces' flags");
        } else if (keyword == "cluster") {
            e.id = words.empty() ? 0 : id_of(core::at(words, 0));
            const std::string to = words.size() > 1 ? core::at(words, 1) : "-";
            e.summary = name_of(e.id, words.empty() ? "?" : core::at(words, 0)) +
                        (to == "-" ? ": not a trick object" : ": trick object of cluster " + name_of(id_of(to), to));
        } else {
            e.summary = keyword;
        }
        out.push_back(e);
    }
    return out;
}

std::string without(const std::string& layout_text, int line)
{
    std::vector<std::string> lines = get_lines(layout_text);
    if (line < 0 || std::cmp_greater_equal(line, lines.size()))
        return layout_text;
    std::string out;
    for (int n = 0; std::cmp_less(n, lines.size()); ++n) {
        if (n == line)
            continue;
        out += core::at(lines, n);
        out += '\n';
    }
    return out;
}

} // namespace frontend::level_editor::changes
