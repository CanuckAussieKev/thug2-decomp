// NOT ORIGINAL - the level editor's shadows and the selection's parts (ours; level_editor_shadows.h).

#include "editor/level_editor_shadows.h"
#include "editor/level_editor.h"
#include "engine/core/checked_index.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace frontend::level_editor::shadows {

namespace {

std::string get_lower(const char* name)
{
    std::string text = name ? name : "";
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

bool get_letter(char character) { return character >= 'a' && character <= 'z'; }
bool get_digit(char character) { return character >= '0' && character <= '9'; }

// A point on the floor: x and z.
struct FloorPoint {
    float x{};
    float z{};
};

// How far a point (x, z) is from a footprint (0: on it).
float footprint_distance(FloorPoint point, V3 min, V3 max)
{
    const auto x = point.x;
    const auto z = point.z;
    const float dx_beyond = x > max.x ? x - max.x : 0.0F;
    const float dx = x < min.x ? min.x - x : dx_beyond;
    const float dz_beyond = z > max.z ? z - max.z : 0.0F;
    const float dz = z < min.z ? min.z - z : dz_beyond;
    return std::sqrt((dx * dx) + (dz * dz));
}

float centre_distance(FloorPoint point, V3 min, V3 max)
{
    const auto x = point.x;
    const auto z = point.z;
    const float dx = x - ((min.x + max.x) * 0.5F);
    const float dz = z - ((min.z + max.z) * 0.5F);
    return std::sqrt((dx * dx) + (dz * dz));
}

} // namespace

bool shadow_name(const char* name) { return get_lower(name).find("shad") != std::string::npos; }

std::vector<std::string> owner_names(const char* name)
{
    std::vector<std::string> owners;
    std::string text = get_lower(name);
    // The last "shad", its tail a suffix: letters, then optionally "_" and a number, then optionally a letter (an earlier
    // "shad" has this one in its tail: it can't be the suffix if this isn't).
    const size_t at = text.rfind("shad");
    if (at == std::string::npos || at == 0) {
        return owners;
    }
    size_t index = at + 4;
    while (index < text.size() && get_letter(core::at(text, index))) {
        ++index;
    }
    size_t number_at = index;
    if (index < text.size() && core::at(text, index) == '_' && index + 1 < text.size() &&
        get_digit(core::at(text, index + 1))) {
        number_at = ++index;
    }
    while (index < text.size() && get_digit(core::at(text, index))) {
        ++index;
    }
    std::string number = text.substr(number_at, index - number_at);
    if (!number.empty() && index < text.size() && get_letter(core::at(text, index))) {
        number += core::at(text, index++);
    }
    if (index != text.size()) {
        return owners; // (not at the end: "Shadow" is part of the name)
    }
    std::string prefix = text.substr(0, at);
    if (prefix.back() == '_') {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        return owners;
    }
    if (!number.empty()) {
        owners.push_back(prefix + "_" + number);
        owners.push_back(prefix + number);
    }
    owners.push_back(prefix);
    return owners;
}

bool falls_under(V3 shadow_min, V3 shadow_max, V3 group_min, V3 group_max)
{
    if (!(shadow_min.x <= shadow_max.x && shadow_min.y <= shadow_max.y && shadow_min.z <= shadow_max.z) ||
        !(group_min.x <= group_max.x && group_min.y <= group_max.y && group_min.z <= group_max.z)) {
        return false;
    }
    if (shadow_max.y - shadow_min.y > kFlat || std::fabs(shadow_max.y - group_min.y) > kGap ||
        group_max.y - group_min.y <= kFlat) {
        return false;
    }
    const float width = shadow_max.x - shadow_min.x;
    const float depth = shadow_max.z - shadow_min.z;
    if (std::max(width, depth) > kMaxSpan) {
        return false;
    }
    const float caster_area = std::max(group_max.x - group_min.x, 1.0F) * std::max(group_max.z - group_min.z, 1.0F);
    if (std::max(width, 1.0F) * std::max(depth, 1.0F) < kMinCover * caster_area) {
        return false;
    }
    const float margin = kMarginBase + (kMarginPerHeight * (group_max.y - group_min.y));
    const float x0 = std::max(shadow_min.x, group_min.x - margin);
    const float x1 = std::min(shadow_max.x, group_max.x + margin);
    const float z0 = std::max(shadow_min.z, group_min.z - margin);
    const float z1 = std::min(shadow_max.z, group_max.z + margin);
    if (x1 < x0 || z1 < z0) {
        return false;
    }
    // (a thin shadow - a rail's line - counted as at least a unit wide, so its share is measured along it)
    const float area = std::max(width, 1.0F) * std::max(depth, 1.0F);
    const float inside = (width < 1.0F ? 1.0F : x1 - x0) * (depth < 1.0F ? 1.0F : z1 - z0);
    return inside >= kInside * area;
}

bool nearest_to_group(ShadowGroup shadow_group,
                      const std::vector<std::pair<V3, V3>>& others)
{
    const auto shadow_min = shadow_group.shadow_min_;
    const auto shadow_max = shadow_group.shadow_max_;
    const auto group_min = shadow_group.group_min_;
    const auto group_max = shadow_group.group_max_;
    const float x = (shadow_min.x + shadow_max.x) * 0.5F;
    const float z = (shadow_min.z + shadow_max.z) * 0.5F;
    const float mine = footprint_distance({.x = x, .z = z}, group_min, group_max);
    const float mine_centre = centre_distance({.x = x, .z = z}, group_min, group_max);
    // (no other group nearer, or as near with its centre nearer)
    return std::ranges::none_of(others, [&](const auto& other) {
        const float theirs = footprint_distance({.x = x, .z = z}, other.first, other.second);
        return theirs < mine ||
               (theirs == mine && centre_distance({.x = x, .z = z}, other.first, other.second) < mine_centre);
    });
}

bool get_beside(V3 shadow_min, V3 shadow_max, V3 caster_min, V3 caster_max)
{
    const float margin = kMarginBase + (kMarginPerHeight * (caster_max.y - caster_min.y));
    return shadow_min.x <= caster_max.x + margin && shadow_max.x >= caster_min.x - margin &&
           shadow_min.z <= caster_max.z + margin && shadow_max.z >= caster_min.z - margin &&
           shadow_min.y <= caster_max.y + margin && shadow_max.y >= caster_min.y - margin;
}

bool stands_by(V3 piece_min, V3 shadow_max) { return std::fabs(piece_min.y - shadow_max.y) <= kGap; }

} // namespace frontend::level_editor::shadows

namespace frontend::level_editor::members {

int drop_target(const std::vector<bool>& passed, const std::vector<bool>& selected)
{
    const size_t count = std::min(passed.size(), selected.size());
    for (size_t i = 0; i < count; ++i) {
        if (core::at(selected, i)) {
            return static_cast<int>(i);
        }
        if (!core::at(passed, i)) {
            break;
        }
    }
    return -1;
}

} // namespace frontend::level_editor::members
