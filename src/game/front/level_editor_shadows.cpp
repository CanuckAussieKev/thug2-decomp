// NOT ORIGINAL - the level editor's shadows and the selection's parts (ours; level_editor_shadows.h).

#include "game/front/level_editor_shadows.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Front::LevelEditor::Shadows {

namespace {

std::string Lower(const char* name)
{
    std::string text = name ? name : "";
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool Letter(char c) { return c >= 'a' && c <= 'z'; }
bool Digit(char c) { return c >= '0' && c <= '9'; }

// How far a point (x, z) is from a footprint (0: on it).
float FootprintDistance(float x, float z, V3 min, V3 max)
{
    float dx = x < min.x ? min.x - x : (x > max.x ? x - max.x : 0.0f);
    float dz = z < min.z ? min.z - z : (z > max.z ? z - max.z : 0.0f);
    return std::sqrt(dx * dx + dz * dz);
}

float CentreDistance(float x, float z, V3 min, V3 max)
{
    float dx = x - (min.x + max.x) * 0.5f, dz = z - (min.z + max.z) * 0.5f;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

bool ShadowName(const char* name) { return Lower(name).find("shad") != std::string::npos; }

std::vector<std::string> OwnerNames(const char* name)
{
    std::vector<std::string> owners;
    std::string text = Lower(name);
    // The last "shad", its tail a suffix: letters, then optionally "_" and a number, then optionally a letter (an earlier
    // "shad" has this one in its tail: it can't be the suffix if this isn't).
    size_t at = text.rfind("shad");
    if (at == std::string::npos || at == 0)
        return owners;
    size_t i = at + 4;
    while (i < text.size() && Letter(text[i]))
        ++i;
    size_t numberAt = i;
    if (i < text.size() && text[i] == '_' && i + 1 < text.size() && Digit(text[i + 1]))
        numberAt = ++i;
    while (i < text.size() && Digit(text[i]))
        ++i;
    std::string number = text.substr(numberAt, i - numberAt);
    if (!number.empty() && i < text.size() && Letter(text[i]))
        number += text[i++];
    if (i != text.size())
        return owners; // (not at the end: "Shadow" is part of the name)
    std::string prefix = text.substr(0, at);
    if (prefix.back() == '_')
        prefix.pop_back();
    if (prefix.empty())
        return owners;
    if (!number.empty()) {
        owners.push_back(prefix + "_" + number);
        owners.push_back(prefix + number);
    }
    owners.push_back(prefix);
    return owners;
}

bool FallsUnder(V3 shadowMin, V3 shadowMax, V3 groupMin, V3 groupMax)
{
    if (!(shadowMin.x <= shadowMax.x && shadowMin.y <= shadowMax.y && shadowMin.z <= shadowMax.z) ||
        !(groupMin.x <= groupMax.x && groupMin.y <= groupMax.y && groupMin.z <= groupMax.z))
        return false;
    if (shadowMax.y - shadowMin.y > kFlat || std::fabs(shadowMax.y - groupMin.y) > kGap ||
        groupMax.y - groupMin.y <= kFlat)
        return false;
    float width = shadowMax.x - shadowMin.x, depth = shadowMax.z - shadowMin.z;
    if (std::max(width, depth) > kMaxSpan)
        return false;
    float casterArea = std::max(groupMax.x - groupMin.x, 1.0f) * std::max(groupMax.z - groupMin.z, 1.0f);
    if (std::max(width, 1.0f) * std::max(depth, 1.0f) < kMinCover * casterArea)
        return false;
    float margin = kMarginBase + kMarginPerHeight * (groupMax.y - groupMin.y);
    float x0 = std::max(shadowMin.x, groupMin.x - margin), x1 = std::min(shadowMax.x, groupMax.x + margin);
    float z0 = std::max(shadowMin.z, groupMin.z - margin), z1 = std::min(shadowMax.z, groupMax.z + margin);
    if (x1 < x0 || z1 < z0)
        return false;
    // (a thin shadow - a rail's line - counted as at least a unit wide, so its share is measured along it)
    float area = std::max(width, 1.0f) * std::max(depth, 1.0f);
    float inside = (width < 1.0f ? 1.0f : x1 - x0) * (depth < 1.0f ? 1.0f : z1 - z0);
    return inside >= kInside * area;
}

bool NearestToGroup(V3 shadowMin, V3 shadowMax, V3 groupMin, V3 groupMax, const std::vector<std::pair<V3, V3>>& others)
{
    float x = (shadowMin.x + shadowMax.x) * 0.5f, z = (shadowMin.z + shadowMax.z) * 0.5f;
    float mine = FootprintDistance(x, z, groupMin, groupMax);
    float mineCentre = CentreDistance(x, z, groupMin, groupMax);
    for (const auto& other : others) {
        float theirs = FootprintDistance(x, z, other.first, other.second);
        if (theirs < mine)
            return false;
        if (theirs == mine && CentreDistance(x, z, other.first, other.second) < mineCentre)
            return false;
    }
    return true;
}

bool Beside(V3 shadowMin, V3 shadowMax, V3 casterMin, V3 casterMax)
{
    float margin = kMarginBase + kMarginPerHeight * (casterMax.y - casterMin.y);
    return shadowMin.x <= casterMax.x + margin && shadowMax.x >= casterMin.x - margin &&
           shadowMin.z <= casterMax.z + margin && shadowMax.z >= casterMin.z - margin &&
           shadowMin.y <= casterMax.y + margin && shadowMax.y >= casterMin.y - margin;
}

bool StandsBy(V3 pieceMin, V3 shadowMax)
{
    return std::fabs(pieceMin.y - shadowMax.y) <= kGap;
}

} // namespace Front::LevelEditor::Shadows

namespace Front::LevelEditor::Members {

int DropTarget(const std::vector<bool>& passed, const std::vector<bool>& selected)
{
    size_t count = std::min(passed.size(), selected.size());
    for (size_t i = 0; i < count; ++i) {
        if (selected[i])
            return static_cast<int>(i);
        if (!passed[i])
            break;
    }
    return -1;
}

} // namespace Front::LevelEditor::Members
