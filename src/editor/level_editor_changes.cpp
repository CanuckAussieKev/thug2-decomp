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
#include <iterator>
#include <vector>

#include "editor/level_editor_fields.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor.h"

namespace frontend::level_editor::changes {

namespace {

constexpr std::size_t kChecksumDigits = 8; // hex digits of a checksum
constexpr std::size_t kNumberTextSize = 32; // a formatted number
constexpr std::size_t kPrefixedChecksumLength = 10; // "0x" and a checksum's digits
constexpr std::size_t kScaleKeyLength = 6; // "scale="
constexpr int kDegreesPerQuarter = 90;
constexpr std::size_t kTurnKeyLength = 5; // "turn="
constexpr std::size_t kTurnTextSize = 32;
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
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool is_hex8(const std::string& w)
{
    if (w.size() != kChecksumDigits) {
        return false;
    }
    return std::ranges::all_of(
        w, [](const char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
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
    for (const GroupRow& group_row : kGroups) {
        if (keyword == group_row.keyword) {
            return group_row.group;
        }
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
    if (w.size() == kPrefixedChecksumLength && core::at(w, 0) == '0' &&
        (core::at(w, 1) == 'x' || core::at(w, 1) == 'X')) {
        w = w.substr(2);
    }
    if (is_hex8(w)) {
        return static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, kHexBase));
    }
    return w.empty() ? 0 : fields::hash_of_name(w);
}

namespace {

// One record line of a layout: its keyword, its words, the comment after its '#', and the labeler for its subjects.
struct RecordLine {
    std::string keyword;
    std::vector<std::string> words;
    std::string comment;
    const Labeler* label{};
};

// A name for the record's subject: the labeler's, else the comment's, else what the record says.
std::string name_of(const RecordLine& record, uint32_t id, const std::string& fallback)
{
    std::string name = *record.label ? (*record.label)(id) : "";
    if (name.empty()) {
        name = record.comment;
    }
    return name.empty() ? fallback : name;
}

float number(const RecordLine& record, size_t index)
{
    return index < record.words.size() ? std::strtof(core::at(record.words, index).c_str(), nullptr) : 0.0F;
}

std::string scale_of(const RecordLine& record)
{
    for (const std::string& w : record.words) {
        if (w.starts_with("scale=")) {
            return ", scaled " + w.substr(kScaleKeyLength);
        }
    }
    return "";
}

// The record's first word as a checksum (0 without one), and the first word ("?" without one).
uint32_t first_id(const RecordLine& record) { return record.words.empty() ? 0 : id_of(core::at(record.words, 0)); }

std::string first_word(const RecordLine& record) { return record.words.empty() ? "?" : core::at(record.words, 0); }

// level: the properties edited.
void summarise_level(const RecordLine& record, TableRow& table_row)
{
    std::string what;
    for (const std::string& w : record.words) {
        const std::string key = w.substr(0, w.find('='));
        what += (what.empty() ? "" : ", ") + key;
    }
    table_row.summary = "Level properties: " + (what.empty() ? std::string("edited") : what);
}

// sector, rail, clone, wall: moved, turned, scaled, hidden.
void summarise_placed(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    const std::string& keyword = record.keyword;
    table_row.id = first_id(record);
    const std::string name = name_of(record, table_row.id, first_word(record));
    const int quarters = static_cast<int>(number(record, 1));
    const float x = number(record, 2);
    const float y = number(record, 3);
    const float z = number(record, 4);
    const bool hidden = words.size() > 5 && core::at(words, 5) == "1" && keyword != "clone";
    std::string what;
    if (keyword == "clone") {
        what = "a copy of " + name;
    } else {
        what = name;
    }
    if (x != 0.0F || y != 0.0F || z != 0.0F) {
        what += " moved (" + get_num(x) + ", " + get_num(y) + ", " + get_num(z) + ")";
    }
    double degrees = quarters * kDegreesPerQuarter;
    for (const std::string& w : words) {
        if (w.starts_with("turn=")) {
            degrees += std::strtod(std::next(w.c_str(), kTurnKeyLength), nullptr);
        }
    }
    if (degrees != 0.0) {
        std::array<char, kTurnTextSize> text{};
        text::format_to(text, " turned %.4g", degrees);
        what += std::string(text.data()) + "\xC2\xB0";
    }
    what += scale_of(record);
    if (keyword == "wall") {
        what += hidden ? " - collision off" : "";
    } else if (hidden) {
        what += " - hidden";
    }
    if (keyword == "rail") {
        what.insert(0, "Rail point ");
    }
    table_row.summary = what;
}

// object, copy: where it stands and its heading.
void summarise_object(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    const bool copy = record.keyword == "copy";
    const uint32_t source = copy && words.size() > 1 ? id_of(core::at(words, 1)) : 0;
    table_row.id = first_id(record);
    const size_t at = copy ? 2 : 1;
    const float x = number(record, at);
    const float y = number(record, at + 1);
    const float z = number(record, at + 2);
    // (the at row: the heading)
    const float ax = number(record, at + 9);
    const float az = number(record, at + 11);
    auto heading =
        static_cast<float>(std::atan2(static_cast<double>(ax), static_cast<double>(az)) * kHalfTurnDegrees / kPiDouble);
    const std::string name = copy ? "a copy of " + name_of(record, source, words.size() > 1 ? core::at(words, 1) : "?")
                                  : name_of(record, table_row.id, first_word(record));
    std::array<char, kSummaryTextSize> text{};
    text::format_to(text, " at (%.0f, %.0f, %.0f), heading %.0f\xC2\xB0", x, y, z, heading);
    table_row.summary = name + text.data() + scale_of(record);
}

// set: moved, turned, fields edited.
void summarise_set(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.id = first_id(record);
    const std::string name = name_of(record, table_row.id, first_word(record));
    bool pos = false;
    bool angles = false;
    int fields = 0;
    for (size_t i = 1; i < words.size(); ++i) {
        pos = pos || core::at(words, i).starts_with("pos=");
        angles = angles || core::at(words, i).starts_with("angles=");
        fields += core::at(words, i).starts_with("f.") ? 1 : 0;
    }
    std::string what;
    if (pos) {
        what = angles ? "moved / turned" : "moved";
    } else if (angles) {
        what = "turned";
    }
    if (fields != 0) {
        what += (what.empty() ? "" : ", ") + std::to_string(fields) + (fields == 1 ? " field" : " fields") + " edited";
    }
    table_row.summary = name + ": " + (what.empty() ? std::string("edited") : what);
}

// node, point, camera: what was added, and its class.
void summarise_added(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    const std::string& keyword = record.keyword;
    table_row.id = first_id(record);
    std::string cls = "?";
    if (keyword == "camera") {
        cls = "camera";
    } else if (words.size() > 1) {
        cls = core::at(words, 1);
    }
    const char* added = "Added ";
    if (keyword == "point") {
        added = "Point ";
    } else if (keyword == "camera") {
        added = "Camera ";
    }
    table_row.summary = std::string(added) + first_word(record) + " (" + cls + ")";
}

void summarise_order(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.summary = "The added crowns' order: " + std::to_string(!words.empty() ? words.size() - 1 : 0) + " crowns";
}

void summarise_hidden(const RecordLine& record, TableRow& table_row)
{
    table_row.id = first_id(record);
    table_row.summary = name_of(record, table_row.id, first_word(record)) +
                        (record.keyword == "drop" ? " deleted (a chain point)" : " hidden");
}

void summarise_lock(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.id = words.size() > 1 ? id_of(core::at(words, 1)) : 0;
    table_row.summary = name_of(record, table_row.id, words.size() > 1 ? core::at(words, 1) : "?") + " locked (" +
                        first_word(record) + ")";
}

void summarise_trigger(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.id = first_id(record);
    table_row.summary = "Trigger " + first_word(record) + (words.size() > 1 ? " (" + core::at(words, 1) + ")" : "");
}

void summarise_proximity(const RecordLine& record, TableRow& table_row)
{
    table_row.id = first_id(record);
    table_row.summary = name_of(record, table_row.id, first_word(record)) + ": proximity scripts / shape";
}

void summarise_links(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.id = first_id(record);
    std::string to = words.size() > 1 ? core::at(words, 1) : "-";
    const int count = to == "-" ? 0 : 1 + static_cast<int>(std::count(to.begin(), to.end(), ','));
    table_row.summary = name_of(record, table_row.id, first_word(record)) + " relinked (" + std::to_string(count) +
                        (count == 1 ? " link)" : " links)");
}

// terrain, facing, railspeed, grindflat, speed: the chain's setting and its value.
void summarise_chain_setting(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    const std::string& keyword = record.keyword;
    table_row.id = first_id(record);
    const std::string value = words.size() > 1 ? core::at(words, 1) : "";
    const char* what = " grindable on flat ground";
    if (keyword == "terrain") {
        what = " terrain ";
    } else if (keyword == "facing") {
        what = " facing ";
    } else if (keyword == "railspeed") {
        what = " speed-up ";
    } else if (keyword == "speed") {
        what = " path speed ";
    }
    table_row.summary = name_of(record, table_row.id, first_word(record)) + what + value;
}

void summarise_emitter(const RecordLine& record, TableRow& table_row)
{
    table_row.id = first_id(record);
    table_row.summary = name_of(record, table_row.id, first_word(record)) + ": sound emitter's sound / volume / radius";
}

void summarise_faces(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.id = first_id(record);
    std::string list = words.size() > 1 ? core::at(words, 1) : "";
    const int count = list.empty() ? 0 : 1 + static_cast<int>(std::count(list.begin(), list.end(), ','));
    table_row.summary = name_of(record, table_row.id, first_word(record)) + ": " + std::to_string(count) +
                        (count == 1 ? " face's flags" : " faces' flags");
}

void summarise_cluster(const RecordLine& record, TableRow& table_row)
{
    const auto& words = record.words;
    table_row.id = first_id(record);
    const std::string to = words.size() > 1 ? core::at(words, 1) : "-";
    table_row.summary =
        name_of(record, table_row.id, first_word(record)) +
        (to == "-" ? ": not a trick object" : ": trick object of cluster " + name_of(record, id_of(to), to));
}

using Summariser = void (*)(const RecordLine&, TableRow&);
struct SummaryRow {
    const char* keyword;
    Summariser summarise;
};
// The record keywords' summaries (a keyword not here: the keyword itself).
constexpr auto kSummaries = std::to_array<SummaryRow>({
    {.keyword = "level", .summarise = summarise_level},
    {.keyword = "sector", .summarise = summarise_placed},
    {.keyword = "rail", .summarise = summarise_placed},
    {.keyword = "clone", .summarise = summarise_placed},
    {.keyword = "wall", .summarise = summarise_placed},
    {.keyword = "object", .summarise = summarise_object},
    {.keyword = "copy", .summarise = summarise_object},
    {.keyword = "set", .summarise = summarise_set},
    {.keyword = "node", .summarise = summarise_added},
    {.keyword = "point", .summarise = summarise_added},
    {.keyword = "camera", .summarise = summarise_added},
    {.keyword = "order", .summarise = summarise_order},
    {.keyword = "hide", .summarise = summarise_hidden},
    {.keyword = "drop", .summarise = summarise_hidden},
    {.keyword = "lock", .summarise = summarise_lock},
    {.keyword = "trigger", .summarise = summarise_trigger},
    {.keyword = "proxim", .summarise = summarise_proximity},
    {.keyword = "links", .summarise = summarise_links},
    {.keyword = "terrain", .summarise = summarise_chain_setting},
    {.keyword = "facing", .summarise = summarise_chain_setting},
    {.keyword = "railspeed", .summarise = summarise_chain_setting},
    {.keyword = "grindflat", .summarise = summarise_chain_setting},
    {.keyword = "speed", .summarise = summarise_chain_setting},
    {.keyword = "emitter", .summarise = summarise_emitter},
    {.keyword = "faces", .summarise = summarise_faces},
    {.keyword = "cluster", .summarise = summarise_cluster},
});

// A line's text after its '#' (leading spaces dropped), the line cut there.
std::string take_comment(std::string& line)
{
    std::string comment;
    if (const size_t hash = line.find('#'); hash != std::string::npos) {
        comment = line.substr(hash + 1);
        const size_t first = comment.find_first_not_of(' ');
        comment = first == std::string::npos ? "" : comment.substr(first);
        line.resize(hash);
    }
    return comment;
}

} // namespace

std::vector<TableRow> link_chain(const std::string& layout_text, const Labeler& label)
{
    std::vector<TableRow> out;
    std::vector<std::string> lines = get_lines(layout_text);
    for (int line_number = 0; std::cmp_less(line_number, lines.size()); ++line_number) {
        std::string line = core::at(lines, line_number);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        RecordLine record{.label = &label};
        record.comment = take_comment(line);
        std::istringstream in(line);
        if (!(in >> record.keyword)) {
            continue;
        }
        const char* group = group_of(record.keyword);
        if (!group) {
            continue;
        }
        TableRow table_row;
        table_row.line = line_number;
        table_row.keyword = record.keyword;
        table_row.group = group;
        for (std::string word; in >> word;) {
            record.words.push_back(word);
        }
        const auto summary = std::ranges::find_if(
            kSummaries, [&](const SummaryRow& summary_row) { return record.keyword == summary_row.keyword; });
        if (summary != kSummaries.end()) {
            summary->summarise(record, table_row);
        } else {
            table_row.summary = record.keyword;
        }
        out.push_back(table_row);
    }
    return out;
}

std::string without(const std::string& layout_text, int line)
{
    std::vector<std::string> lines = get_lines(layout_text);
    if (line < 0 || std::cmp_greater_equal(line, lines.size())) {
        return layout_text;
    }
    std::string out;
    for (int line_index = 0; std::cmp_less(line_index, lines.size()); ++line_index) {
        if (line_index == line) {
            continue;
        }
        out += core::at(lines, line_index);
        out += '\n';
    }
    return out;
}

} // namespace frontend::level_editor::changes
