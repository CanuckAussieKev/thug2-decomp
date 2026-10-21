// NOT ORIGINAL - the level editor's Changes panel (ours). See level_editor_changes.h.

#include "game/front/level_editor_changes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "game/front/level_editor_fields.h"

namespace Front::LevelEditor::Changes {

namespace {

struct GroupRow {
    const char* keyword;
    const char* group;
};
const GroupRow kGroups[] = {
    {"level", "Level properties"}, {"sector", "Scenery"},       {"clone", "Scenery copies"}, {"rail", "Rail points"},
    {"object", "Objects"},         {"copy", "Object copies"},   {"set", "Nodes moved / fields"},
    {"node", "Nodes added"},       {"hide", "Nodes hidden"},    {"order", "Nodes added"},   {"trigger", "Triggers"},
    {"proxim", "Triggers"},        {"point", "Chains"},         {"links", "Chains"},        {"drop", "Chains"},
    {"terrain", "Chains"},         {"facing", "Chains"},        {"grindflat", "Chains"},    {"railspeed", "Chains"},
    {"speed", "Peds, cars, sounds"}, {"emitter", "Peds, cars, sounds"}, {"wall", "Collision and walls"},
    {"faces", "Collision and walls"}, {"cluster", "Trick objects"}, {"camera", "Cameras"},  {"lock", "Locks"},
};

std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

bool IsHex8(const std::string& w)
{
    if (w.size() != 8)
        return false;
    for (char c : w) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

std::string Num(float v)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f", v);
    return text;
}

} // namespace

const char* GroupOf(const std::string& keyword)
{
    for (const GroupRow& g : kGroups) {
        if (keyword == g.keyword)
            return g.group;
    }
    return nullptr;
}

const std::vector<const char*>& Groups()
{
    static const std::vector<const char*> groups = {
        "Level properties", "Scenery", "Scenery copies", "Rail points", "Objects", "Object copies", "Nodes moved / fields",
        "Nodes added", "Nodes hidden", "Triggers", "Chains", "Peds, cars, sounds", "Collision and walls", "Trick objects",
        "Cameras", "Locks"};
    return groups;
}

uint32_t IdOf(const std::string& word)
{
    std::string w = word;
    if (w.size() == 10 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X'))
        w = w.substr(2);
    if (IsHex8(w))
        return static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, 16));
    return w.empty() ? 0 : Fields::NameChecksum(w);
}

std::vector<Entry> List(const std::string& layoutText, const Labeler& label)
{
    std::vector<Entry> out;
    std::vector<std::string> lines = Lines(layoutText);
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
        const char* group = GroupOf(keyword);
        if (!group)
            continue;
        Entry e;
        e.line = n;
        e.keyword = keyword;
        e.group = group;
        std::vector<std::string> words;
        for (std::string w; in >> w;)
            words.push_back(w);
        // (a name for the record's subject: the labeler's, else the comment's, else what the record says)
        auto nameOf = [&](uint32_t id, const std::string& fallback) {
            std::string name = label ? label(id) : "";
            if (name.empty())
                name = comment;
            return name.empty() ? fallback : name;
        };
        auto number = [&](size_t i) { return i < words.size() ? std::strtof(words[i].c_str(), nullptr) : 0.0f; };
        auto scaleOf = [&]() -> std::string {
            for (const std::string& w : words) {
                if (w.compare(0, 6, "scale=") == 0)
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
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string name = nameOf(e.id, words.empty() ? "?" : words[0]);
            int quarters = static_cast<int>(number(1));
            float x = number(2), y = number(3), z = number(4);
            bool hidden = words.size() > 5 && words[5] == "1" && keyword != "clone";
            std::string what;
            if (keyword == "clone")
                what = "a copy of " + name;
            else
                what = name;
            if (x != 0.0f || y != 0.0f || z != 0.0f)
                what += " moved (" + Num(x) + ", " + Num(y) + ", " + Num(z) + ")";
            if (quarters)
                what += std::string(" turned ") + std::to_string(quarters * 90) + "\xC2\xB0";
            what += scaleOf();
            if (keyword == "wall")
                what += hidden ? " - collision off" : "";
            else if (hidden)
                what += " - hidden";
            if (keyword == "rail")
                what = "Rail point " + what;
            e.summary = what;
        } else if (keyword == "object" || keyword == "copy") {
            bool copy = keyword == "copy";
            uint32_t source = copy && words.size() > 1 ? IdOf(words[1]) : 0;
            e.id = words.empty() ? 0 : IdOf(words[0]);
            size_t at = copy ? 2 : 1;
            float x = number(at), y = number(at + 1), z = number(at + 2);
            // (the at row: the heading)
            float ax = number(at + 9), az = number(at + 11);
            float heading = static_cast<float>(std::atan2(static_cast<double>(ax), static_cast<double>(az)) * 180.0 / 3.14159265358979);
            std::string name = copy ? "a copy of " + nameOf(source, words.size() > 1 ? words[1] : "?")
                                    : nameOf(e.id, words.empty() ? "?" : words[0]);
            char text[96];
            std::snprintf(text, sizeof(text), " at (%.0f, %.0f, %.0f), heading %.0f\xC2\xB0", x, y, z, heading);
            e.summary = name + text + scaleOf();
        } else if (keyword == "set") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string name = nameOf(e.id, words.empty() ? "?" : words[0]);
            bool pos = false, angles = false;
            int fields = 0;
            for (size_t i = 1; i < words.size(); ++i) {
                pos = pos || words[i].compare(0, 4, "pos=") == 0;
                angles = angles || words[i].compare(0, 7, "angles=") == 0;
                fields += words[i].compare(0, 2, "f.") == 0 ? 1 : 0;
            }
            std::string what = pos ? (angles ? "moved / turned" : "moved") : angles ? "turned" : "";
            if (fields)
                what += (what.empty() ? "" : ", ") + std::to_string(fields) + (fields == 1 ? " field" : " fields") + " edited";
            e.summary = name + ": " + (what.empty() ? std::string("edited") : what);
        } else if (keyword == "node" || keyword == "point" || keyword == "camera") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string cls = keyword == "camera" ? "camera" : words.size() > 1 ? words[1] : "?";
            e.summary = std::string(keyword == "point" ? "Point " : keyword == "camera" ? "Camera " : "Added ") +
                        (words.empty() ? "?" : words[0]) + " (" + cls + ")";
        } else if (keyword == "order") {
            e.summary = "The added crowns' order: " + std::to_string(words.size() > 0 ? words.size() - 1 : 0) + " crowns";
        } else if (keyword == "hide" || keyword == "drop") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) + (keyword == "drop" ? " deleted (a chain point)" : " hidden");
        } else if (keyword == "lock") {
            e.id = words.size() > 1 ? IdOf(words[1]) : 0;
            e.summary = nameOf(e.id, words.size() > 1 ? words[1] : "?") + " locked (" +
                        (words.empty() ? "?" : words[0]) + ")";
        } else if (keyword == "trigger") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            e.summary = "Trigger " + (words.empty() ? std::string("?") : words[0]) +
                        (words.size() > 1 ? " (" + words[1] + ")" : "");
        } else if (keyword == "proxim") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) + ": proximity scripts / shape";
        } else if (keyword == "links") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string to = words.size() > 1 ? words[1] : "-";
            int count = to == "-" ? 0 : 1 + static_cast<int>(std::count(to.begin(), to.end(), ','));
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) + " relinked (" + std::to_string(count) +
                        (count == 1 ? " link)" : " links)");
        } else if (keyword == "terrain" || keyword == "facing" || keyword == "railspeed" || keyword == "grindflat" ||
                   keyword == "speed") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string value = words.size() > 1 ? words[1] : "";
            const char* what = keyword == "terrain"     ? " terrain "
                               : keyword == "facing"    ? " facing "
                               : keyword == "railspeed" ? " speed-up "
                               : keyword == "speed"     ? " path speed "
                                                        : " grindable on flat ground";
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) + what + value;
        } else if (keyword == "emitter") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) + ": sound emitter's sound / volume / radius";
        } else if (keyword == "faces") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string list = words.size() > 1 ? words[1] : "";
            int count = list.empty() ? 0 : 1 + static_cast<int>(std::count(list.begin(), list.end(), ','));
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) + ": " + std::to_string(count) +
                        (count == 1 ? " face's flags" : " faces' flags");
        } else if (keyword == "cluster") {
            e.id = words.empty() ? 0 : IdOf(words[0]);
            std::string to = words.size() > 1 ? words[1] : "-";
            e.summary = nameOf(e.id, words.empty() ? "?" : words[0]) +
                        (to == "-" ? ": not a trick object" : ": trick object of cluster " + nameOf(IdOf(to), to));
        } else {
            e.summary = keyword;
        }
        out.push_back(e);
    }
    return out;
}

std::string Without(const std::string& layoutText, int line)
{
    std::vector<std::string> lines = Lines(layoutText);
    if (line < 0 || line >= static_cast<int>(lines.size()))
        return layoutText;
    std::string out;
    for (int n = 0; n < static_cast<int>(lines.size()); ++n) {
        if (n == line)
            continue;
        out += lines[n];
        out += '\n';
    }
    return out;
}

} // namespace Front::LevelEditor::Changes
