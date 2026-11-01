// NOT ORIGINAL - the level editor's Changes panel (ours). See level_editor_changes.h.

#include "editor/level_editor_changes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "editor/level_editor_fields.h"

namespace frontend::level_editor::changes {

namespace {

struct GroupRow {
    const char* keyword;
    const char* group;
};
const GroupRow kGroups[] = {
    {"level", "Level properties"},
    {"sector", "Scenery"},
    {"clone", "Scenery copies"},
    {"rail", "Rail points"},
    {"object", "Objects"},
    {"copy", "Object copies"},
    {"set", "Nodes moved / fields"},
    {"node", "Nodes added"},
    {"hide", "Nodes hidden"},
    {"order", "Nodes added"},
    {"trigger", "Triggers"},
    {"proxim", "Triggers"},
    {"point", "Chains"},
    {"links", "Chains"},
    {"drop", "Chains"},
    {"terrain", "Chains"},
    {"facing", "Chains"},
    {"grindflat", "Chains"},
    {"railspeed", "Chains"},
    {"speed", "Peds, cars, sounds"},
    {"emitter", "Peds, cars, sounds"},
    {"wall", "Collision and walls"},
    {"faces", "Collision and walls"},
    {"cluster", "Trick objects"},
    {"camera", "Cameras"},
    {"lock", "Locks"},
};

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
    if (w.size() != 8)
        return false;
    for (char c : w) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

std::string get_num(float v)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f", v);
    return text;
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
    if (w.size() == 10 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X'))
        w = w.substr(2);
    if (is_hex8(w))
        return static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, 16));
    return w.empty() ? 0 : fields::hash_of_name(w);
}

std::vector<TableRow> link_chain(const std::string& layout_text, const Labeler& label)
{
    std::vector<TableRow> out;
    std::vector<std::string> lines = get_lines(layout_text);
    for (int n = 0; n < static_cast<int>(lines.size()); ++n) {
        std::string line = lines[n];
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string comment;
        if (size_t hash = line.find('#'); hash != std::string::npos) {
            comment = line.substr(hash + 1);
            size_t a = comment.find_first_not_of(' ');
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
        auto number = [&](size_t i) { return i < words.size() ? std::strtof(words[i].c_str(), nullptr) : 0.0f; };
        auto scale_of = [&]() -> std::string {
            for (const std::string& w : words) {
                if (w.starts_with("scale="))
                    return ", scaled " + w.substr(6);
            }
            return "";
        };
        if (keyword == "level") {
            std::string what;
            for (const std::string& w : words) {
                std::string key = w.substr(0, w.find('='));
                what += (what.empty() ? "" : ", ") + key;
            }
            e.summary = "Level properties: " + (what.empty() ? std::string("edited") : what);
        } else if (keyword == "sector" || keyword == "rail" || keyword == "clone" || keyword == "wall") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string name = name_of(e.id, words.empty() ? "?" : words[0]);
            int quarters = static_cast<int>(number(1));
            float x = number(2), y = number(3), z = number(4);
            bool hidden = words.size() > 5 && words[5] == "1" && keyword != "clone";
            std::string what;
            if (keyword == "clone")
                what = "a copy of " + name;
            else
                what = name;
            if (x != 0.0f || y != 0.0f || z != 0.0f)
                what += " moved (" + get_num(x) + ", " + get_num(y) + ", " + get_num(z) + ")";
            if (quarters)
                what += std::string(" turned ") + std::to_string(quarters * 90) + "\xC2\xB0";
            what += scale_of();
            if (keyword == "wall")
                what += hidden ? " - collision off" : "";
            else if (hidden)
                what += " - hidden";
            if (keyword == "rail")
                what = "Rail point " + what;
            e.summary = what;
        } else if (keyword == "object" || keyword == "copy") {
            bool copy = keyword == "copy";
            uint32_t source = copy && words.size() > 1 ? id_of(words[1]) : 0;
            e.id = words.empty() ? 0 : id_of(words[0]);
            size_t at = copy ? 2 : 1;
            float x = number(at), y = number(at + 1), z = number(at + 2);
            // (the at row: the heading)
            float ax = number(at + 9), az = number(at + 11);
            auto heading = static_cast<float>(std::atan2(static_cast<double>(ax), static_cast<double>(az)) * 180.0 /
                                              3.14159265358979);
            std::string name = copy ? "a copy of " + name_of(source, words.size() > 1 ? words[1] : "?")
                                    : name_of(e.id, words.empty() ? "?" : words[0]);
            char text[96];
            std::snprintf(text, sizeof(text), " at (%.0f, %.0f, %.0f), heading %.0f\xC2\xB0", x, y, z, heading);
            e.summary = name + text + scale_of();
        } else if (keyword == "set") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string name = name_of(e.id, words.empty() ? "?" : words[0]);
            bool pos = false, angles = false;
            int fields = 0;
            for (size_t i = 1; i < words.size(); ++i) {
                pos = pos || words[i].starts_with("pos=");
                angles = angles || words[i].starts_with("angles=");
                fields += words[i].starts_with("f.") ? 1 : 0;
            }
            std::string what = pos ? (angles ? "moved / turned" : "moved") : angles ? "turned" : "";
            if (fields)
                what += (what.empty() ? "" : ", ") + std::to_string(fields) + (fields == 1 ? " field" : " fields") +
                        " edited";
            e.summary = name + ": " + (what.empty() ? std::string("edited") : what);
        } else if (keyword == "node" || keyword == "point" || keyword == "camera") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string cls = keyword == "camera" ? "camera" : words.size() > 1 ? words[1] : "?";
            e.summary = std::string(keyword == "point"    ? "Point "
                                    : keyword == "camera" ? "Camera "
                                                          : "Added ") +
                        (words.empty() ? "?" : words[0]) + " (" + cls + ")";
        } else if (keyword == "order") {
            e.summary =
                "The added crowns' order: " + std::to_string(words.size() > 0 ? words.size() - 1 : 0) + " crowns";
        } else if (keyword == "hide" || keyword == "drop") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) +
                        (keyword == "drop" ? " deleted (a chain point)" : " hidden");
        } else if (keyword == "lock") {
            e.id = words.size() > 1 ? id_of(words[1]) : 0;
            e.summary =
                name_of(e.id, words.size() > 1 ? words[1] : "?") + " locked (" + (words.empty() ? "?" : words[0]) + ")";
        } else if (keyword == "trigger") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            e.summary = "Trigger " + (words.empty() ? std::string("?") : words[0]) +
                        (words.size() > 1 ? " (" + words[1] + ")" : "");
        } else if (keyword == "proxim") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) + ": proximity scripts / shape";
        } else if (keyword == "links") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string to = words.size() > 1 ? words[1] : "-";
            int count = to == "-" ? 0 : 1 + static_cast<int>(std::count(to.begin(), to.end(), ','));
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) + " relinked (" + std::to_string(count) +
                        (count == 1 ? " link)" : " links)");
        } else if (keyword == "terrain" || keyword == "facing" || keyword == "railspeed" || keyword == "grindflat" ||
                   keyword == "speed") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string value = words.size() > 1 ? words[1] : "";
            const char* what = keyword == "terrain"     ? " terrain "
                               : keyword == "facing"    ? " facing "
                               : keyword == "railspeed" ? " speed-up "
                               : keyword == "speed"     ? " path speed "
                                                        : " grindable on flat ground";
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) + what + value;
        } else if (keyword == "emitter") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) + ": sound emitter's sound / volume / radius";
        } else if (keyword == "faces") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string list = words.size() > 1 ? words[1] : "";
            int count = list.empty() ? 0 : 1 + static_cast<int>(std::count(list.begin(), list.end(), ','));
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) + ": " + std::to_string(count) +
                        (count == 1 ? " face's flags" : " faces' flags");
        } else if (keyword == "cluster") {
            e.id = words.empty() ? 0 : id_of(words[0]);
            std::string to = words.size() > 1 ? words[1] : "-";
            e.summary = name_of(e.id, words.empty() ? "?" : words[0]) +
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
    if (line < 0 || line >= static_cast<int>(lines.size()))
        return layout_text;
    std::string out;
    for (int n = 0; n < static_cast<int>(lines.size()); ++n) {
        if (n == line)
            continue;
        out += lines[n];
        out += '\n';
    }
    return out;
}

} // namespace frontend::level_editor::changes
