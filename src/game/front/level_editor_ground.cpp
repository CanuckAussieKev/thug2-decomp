// NOT ORIGINAL - the level editor's raised / lowered ground with ramps (ours). See level_editor_ground.h.

#include "game/front/level_editor_ground.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "game/core/crc.h"

namespace Front::LevelEditor::Ground {

// --- the pure parts -----------------------------------------------------------------------------------------------

bool SameSpec(const Spec& a, const Spec& b)
{
    return a.name == b.name && a.lower == b.lower && a.x0 == b.x0 && a.z0 == b.z0 && a.x1 == b.x1 && a.z1 == b.z1 &&
           a.base == b.base && a.height == b.height && a.ramps == b.ramps && a.length == b.length &&
           a.material == b.material && a.uv == b.uv && a.colour == b.colour && a.terrain == b.terrain &&
           a.hides == b.hides;
}

namespace {

bool Finite(float v) { return std::isfinite(v); }
float Clamp(float v, float low, float high) { return std::max(low, std::min(high, v)); }

} // namespace

bool Normalise(Spec* s)
{
    if (!Finite(s->x0) || !Finite(s->x1) || !Finite(s->z0) || !Finite(s->z1) || !Finite(s->base) ||
        !Finite(s->height) || !Finite(s->length) || !Finite(s->uv))
        return false;
    if (s->x1 < s->x0)
        std::swap(s->x0, s->x1);
    if (s->z1 < s->z0)
        std::swap(s->z0, s->z1);
    // (too narrow / wide: grown / shrunk about the first corner)
    s->x1 = s->x0 + Clamp(s->x1 - s->x0, kMinSide, kMaxSide);
    s->z1 = s->z0 + Clamp(s->z1 - s->z0, kMinSide, kMaxSide);
    s->height = Clamp(s->height, kMinHeight, kMaxHeight);
    s->length = Clamp(s->length, 0.0f, kMaxLength);
    s->uv = Clamp(s->uv, 16.0f, 4096.0f);
    return true;
}

float RampLength(const Spec& s, int side)
{
    Ramp ramp = s.ramps[side];
    if (ramp == Ramp::None)
        return 0.0f;
    float length = s.length > 0.0f ? s.length : (ramp == Ramp::Kicker ? 2.0f * s.height : s.height);
    length = std::max(10.0f, length);
    if (s.lower) {
        // (a pit: the ramp opposite takes its share of the width)
        float across = side == kMinusX || side == kPlusX ? s.x1 - s.x0 : s.z1 - s.z0;
        int opposite = side ^ 1;
        float room = s.ramps[opposite] != Ramp::None ? across * 0.5f - 10.0f : across - 10.0f;
        length = std::max(10.0f, std::min(length, room));
    }
    return length;
}

std::vector<std::pair<float, float>> QuarterProfile(float radius, float height, int segments)
{
    std::vector<std::pair<float, float>> points;
    if (segments < 1)
        segments = 1;
    // An arc of `radius`: its foot (out `radius` sin(top), up 0) to the angle reaching min(height, radius) - vertical
    // when the full quarter; d = R (sin top - sin t), y = R (1 - cos t).
    float rise = std::min(height, radius);
    double top = std::acos(std::max(-1.0, std::min(1.0, 1.0 - static_cast<double>(rise) / radius)));
    for (int i = 0; i <= segments; ++i) {
        double t = top * i / segments;
        float d = static_cast<float>(radius * (std::sin(top) - std::sin(t)));
        float y = static_cast<float>(radius * (1.0 - std::cos(t)));
        if (i == segments) {
            d = 0.0f;
            y = rise;
        }
        points.push_back({d, y});
    }
    if (height > radius + 0.01f)
        points.push_back({0.0f, height});
    return points;
}

namespace {

V3 Up() { return {0.0f, 1.0f, 0.0f}; }

// The side's outward direction and its edge's two ends (at height 0) - along the side, the first at its lower x / z.
void SideFrame(const Spec& s, int side, V3* out, V3* e0, V3* e1)
{
    switch (side) {
    case kMinusX: *out = {-1, 0, 0}, *e0 = {s.x0, 0, s.z0}, *e1 = {s.x0, 0, s.z1}; break;
    case kPlusX: *out = {1, 0, 0}, *e0 = {s.x1, 0, s.z0}, *e1 = {s.x1, 0, s.z1}; break;
    case kMinusZ: *out = {0, 0, -1}, *e0 = {s.x0, 0, s.z0}, *e1 = {s.x1, 0, s.z0}; break;
    default: *out = {0, 0, 1}, *e0 = {s.x0, 0, s.z1}, *e1 = {s.x1, 0, s.z1}; break;
    }
}

struct Builder {
    Mesh mesh;
    float uv;

    void Corner(V3 p, V3 n)
    {
        float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        float u, v;
        if (ay >= ax && ay >= az)
            u = p.x, v = p.z;
        else if (ax >= az)
            u = p.z, v = p.y;
        else
            u = p.x, v = p.y;
        mesh.vertices.push_back({p, n, u / uv, -v / uv});
    }
    // A triangle wound so that its normal points along `hint` (not added when it has no area).
    void Tri(V3 a, V3 b, V3 c, V3 hint, uint16_t flags)
    {
        V3 n = Cross(b - a, c - a);
        float length = std::sqrt(Dot(n, n));
        if (!(length > 1e-3f))
            return;
        if (Dot(n, hint) < 0.0f) {
            std::swap(b, c);
            n = n * -1.0f;
        }
        n = n * (1.0f / length);
        uint16_t first = static_cast<uint16_t>(mesh.vertices.size());
        Corner(a, n);
        Corner(b, n);
        Corner(c, n);
        mesh.faces.push_back({first, static_cast<uint16_t>(first + 1), static_cast<uint16_t>(first + 2), flags});
    }
    // A quad (corners in order round it) as two triangles.
    void Quad(V3 a, V3 b, V3 c, V3 d, V3 hint, uint16_t flags)
    {
        Tri(a, b, c, hint, flags);
        Tri(a, c, d, hint, flags);
    }
};

V3 At(V3 p, float y) { return {p.x, y, p.z}; }

// A ramp along a side: from its foot (`foot` high, `length` out along `out` from the edge) up to `top` at the edge,
// facing `out` and up; a raise's gets its two end cheeks (facing along the side, away from the ramp).
void AddRamp(Builder* b, Ramp ramp, V3 out, V3 e0, V3 e1, float foot, float top, float length, bool cheeks)
{
    float rise = top - foot;
    std::vector<std::pair<float, float>> profile;
    if (ramp == Ramp::Kicker)
        profile = {{length, 0.0f}, {0.0f, rise}};
    else
        profile = QuarterProfile(length, rise, kQuarterSegments);
    for (size_t i = 0; i + 1 < profile.size(); ++i) {
        float d0 = profile[i].first, y0 = foot + profile[i].second;
        float d1 = profile[i + 1].first, y1 = foot + profile[i + 1].second;
        float dd = d1 - d0, dy = y1 - y0;
        // (the surface's normal: out by the rise, up by the run)
        V3 hint = out * dy + Up() * -dd;
        uint16_t flags = ramp == Ramp::Quarter && dy >= -dd ? kFlagVert : kFlagRamp;
        b->Quad(At(e0 + out * d0, y0), At(e1 + out * d0, y0), At(e1 + out * d1, y1), At(e0 + out * d1, y1), hint, flags);
    }
    if (!cheeks)
        return;
    V3 along = Normalize(e1 - e0);
    for (int end = 0; end < 2; ++end) {
        V3 e = end ? e1 : e0;
        V3 away = end ? along : along * -1.0f;
        V3 corner = At(e, foot);
        for (size_t i = 0; i + 1 < profile.size(); ++i) {
            V3 p = At(e + out * profile[i].first, foot + profile[i].second);
            V3 q = At(e + out * profile[i + 1].first, foot + profile[i + 1].second);
            b->Tri(corner, p, q, away, kFlagWall);
        }
    }
}

} // namespace

Mesh Build(const Spec& spec)
{
    Spec s = spec;
    Builder b;
    b.uv = s.uv > 0.0f ? s.uv : 256.0f;
    if (!Normalise(&s))
        return b.mesh;
    if (!s.lower) {
        float top = s.base + s.height, bottom = s.base - kSink;
        b.Quad({s.x0, top, s.z0}, {s.x1, top, s.z0}, {s.x1, top, s.z1}, {s.x0, top, s.z1}, Up(), kFlagDeck);
        for (int side = 0; side < 4; ++side) {
            V3 out, e0, e1;
            SideFrame(s, side, &out, &e0, &e1);
            if (s.ramps[side] == Ramp::None)
                b.Quad(At(e0, bottom), At(e1, bottom), At(e1, top), At(e0, top), out, kFlagWall);
            else
                AddRamp(&b, s.ramps[side], out, e0, e1, s.base, top, RampLength(s, side), true);
        }
    } else {
        float floor = s.base - s.height;
        b.Quad({s.x0, floor, s.z0}, {s.x1, floor, s.z0}, {s.x1, floor, s.z1}, {s.x0, floor, s.z1}, Up(), kFlagDeck);
        for (int side = 0; side < 4; ++side) {
            V3 out, e0, e1;
            SideFrame(s, side, &out, &e0, &e1);
            V3 in = out * -1.0f;
            if (s.ramps[side] == Ramp::None)
                b.Quad(At(e0, floor), At(e1, floor), At(e1, s.base), At(e0, s.base), in, kFlagWall);
            else
                AddRamp(&b, s.ramps[side], in, e0, e1, floor, s.base, RampLength(s, side), false);
        }
    }
    Mesh& m = b.mesh;
    if (!m.vertices.empty()) {
        m.min = m.max = m.vertices[0].pos;
        for (const Vertex& v : m.vertices) {
            m.min = {std::min(m.min.x, v.pos.x), std::min(m.min.y, v.pos.y), std::min(m.min.z, v.pos.z)};
            m.max = {std::max(m.max.x, v.pos.x), std::max(m.max.y, v.pos.y), std::max(m.max.z, v.pos.z)};
        }
    }
    return m;
}

std::vector<uint16_t> Strip(const std::vector<Face>& faces, bool twoSided)
{
    std::vector<uint16_t> strip;
    auto add = [&](uint16_t a, uint16_t b, uint16_t c) {
        if (!strip.empty()) {
            // (a join: the last index and this face's first, repeated until the face starts at an even position)
            strip.push_back(strip.back());
            strip.push_back(a);
            strip.push_back(a);
            if ((strip.size() - 1) % 2 != 0)
                strip.push_back(a);
        } else {
            strip.push_back(a);
        }
        strip.push_back(b);
        strip.push_back(c);
    };
    for (const Face& f : faces) {
        add(f.a, f.b, f.c);
        if (twoSided)
            add(f.a, f.c, f.b);
    }
    return strip;
}

std::vector<std::array<uint16_t, 3>> StripTriangles(const std::vector<uint16_t>& strip)
{
    std::vector<std::array<uint16_t, 3>> triangles;
    for (size_t i = 0; i + 2 < strip.size(); ++i) {
        uint16_t a = strip[i], b = strip[i + 1], c = strip[i + 2];
        if (a == b || b == c || a == c)
            continue;
        if (i % 2)
            std::swap(a, b);
        triangles.push_back({a, b, c});
    }
    return triangles;
}

bool LayoutFor(uint32_t fvf, uint32_t stride, Layout* layout)
{
    *layout = Layout{};
    // (0..6: the engine's built-in vertex shaders; 0x10000 and above: a shader handle - texture coordinate sizes other
    // than two never appear in the scene's formats)
    if (fvf <= 6 || fvf >= 0x10000 || (fvf & 0x400E) != 0x002)
        return false;
    int at = 12;
    if (fvf & 0x010)
        layout->normal = at, at += 12;
    if (fvf & 0x020)
        layout->size = at, at += 4;
    if (fvf & 0x040)
        layout->diffuse = at, at += 4;
    if (fvf & 0x080)
        layout->specular = at, at += 4;
    layout->texCount = static_cast<int>((fvf >> 8) & 0xF);
    if (layout->texCount)
        layout->tex = at;
    at += 8 * layout->texCount;
    return static_cast<uint32_t>(at) == stride;
}

void WriteVertices(const Mesh& mesh, const Layout& layout, uint32_t stride, uint32_t colour, uint8_t* out)
{
    for (const Vertex& v : mesh.vertices) {
        std::memset(out, 0, stride);
        std::memcpy(out, &v.pos, 12);
        if (layout.normal >= 0)
            std::memcpy(out + layout.normal, &v.normal, 12);
        if (layout.size >= 0) {
            float one = 1.0f;
            std::memcpy(out + layout.size, &one, 4);
        }
        if (layout.diffuse >= 0)
            std::memcpy(out + layout.diffuse, &colour, 4);
        for (int t = 0; t < layout.texCount; ++t) {
            std::memcpy(out + layout.tex + 8 * t, &v.u, 4);
            std::memcpy(out + layout.tex + 8 * t + 4, &v.v, 4);
        }
        out += stride;
    }
}

char RampLetter(Ramp ramp) { return ramp == Ramp::Kicker ? 'k' : ramp == Ramp::Quarter ? 'q' : '-'; }

namespace {

std::string Number(float v)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", v);
    return text;
}

bool ReadFloat(const std::string& text, float* out)
{
    char* end = nullptr;
    *out = std::strtof(text.c_str(), &end);
    return !text.empty() && end && *end == '\0' && std::isfinite(*out);
}

std::vector<std::string> SplitCommas(const std::string& text)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return parts;
}

bool IsHex8(const std::string& text, uint32_t* value)
{
    if (text.size() != 10 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
        return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(text.c_str() + 2, &end, 16);
    if (!end || *end)
        return false;
    *value = static_cast<uint32_t>(v);
    return true;
}

bool ValidWord(const std::string& name)
{
    if (name.empty() || name.size() > 64)
        return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    }
    return true;
}

} // namespace

namespace {
// A name's checksum (the game's CRC; "0x" + 8 hex digits: that checksum - as level_editor_triggers.cpp's ChecksumOf).
uint32_t NameChecksumFor(const std::string& text)
{
    uint32_t value = 0;
    if (IsHex8(text, &value))
        return value;
    return text.empty() ? 0u : Crc::GenerateCRCFromString(text.c_str());
}
} // namespace

std::string Record(const Spec& s, const char* materialName)
{
    std::ostringstream out;
    char colour[16];
    std::snprintf(colour, sizeof(colour), "%08X", s.colour);
    char material[16];
    std::snprintf(material, sizeof(material), "0x%08X", s.material);
    std::string materialText = materialName && *materialName && ValidWord(materialName) &&
                                       NameChecksumFor(materialName) == s.material
                                   ? std::string(materialName)
                                   : std::string(material);
    out << s.name << (s.lower ? " lower" : " raise") << " rect=" << Number(s.x0) << ',' << Number(s.z0) << ','
        << Number(s.x1) << ',' << Number(s.z1) << " base=" << Number(s.base) << " height=" << Number(s.height)
        << " ramps=" << RampLetter(s.ramps[0]) << ',' << RampLetter(s.ramps[1]) << ',' << RampLetter(s.ramps[2]) << ','
        << RampLetter(s.ramps[3]) << " length=" << Number(s.length) << " material=" << materialText
        << " uv=" << Number(s.uv) << " color=" << colour << " terrain=" << s.terrain;
    if (!s.hides.empty()) {
        out << " hides=";
        for (size_t i = 0; i < s.hides.size(); ++i) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "0x%08X", s.hides[i]);
            out << (i ? "," : "") << hex;
        }
    }
    return out.str();
}

bool ReadRecord(const std::string& text, Spec* out)
{
    std::istringstream in(text);
    Spec s;
    std::string kind;
    if (!(in >> s.name >> kind) || !ValidWord(s.name))
        return false;
    if (kind == "raise")
        s.lower = false;
    else if (kind == "lower")
        s.lower = true;
    else
        return false;
    bool haveRect = false, haveBase = false, haveHeight = false, haveMaterial = false, haveRamps = false,
         haveLength = false, haveUv = false, haveColour = false, haveTerrain = false, haveHides = false;
    auto once = [](bool* seen) {
        if (*seen)
            return false;
        *seen = true;
        return true;
    };
    for (std::string word; in >> word;) {
        size_t eq = word.find('=');
        if (eq == std::string::npos || eq == 0)
            return false;
        std::string key = word.substr(0, eq), value = word.substr(eq + 1);
        bool ok = true;
        if (key == "rect") {
            std::vector<std::string> parts = SplitCommas(value);
            ok = once(&haveRect) && parts.size() == 4 && ReadFloat(parts[0], &s.x0) && ReadFloat(parts[1], &s.z0) &&
                 ReadFloat(parts[2], &s.x1) && ReadFloat(parts[3], &s.z1);
        } else if (key == "base") {
            ok = once(&haveBase) && ReadFloat(value, &s.base);
        } else if (key == "height") {
            ok = once(&haveHeight) && ReadFloat(value, &s.height) && s.height > 0.0f;
        } else if (key == "ramps") {
            std::vector<std::string> parts = SplitCommas(value);
            ok = once(&haveRamps) && parts.size() == 4;
            for (size_t i = 0; ok && i < 4; ++i) {
                const std::string& p = parts[i];
                if (p == "-")
                    s.ramps[i] = Ramp::None;
                else if (p == "k")
                    s.ramps[i] = Ramp::Kicker;
                else if (p == "q")
                    s.ramps[i] = Ramp::Quarter;
                else
                    ok = false;
            }
        } else if (key == "length") {
            ok = once(&haveLength) && ReadFloat(value, &s.length) && s.length >= 0.0f;
        } else if (key == "material") {
            ok = once(&haveMaterial) && (IsHex8(value, &s.material) || ValidWord(value));
            if (ok && !IsHex8(value, &s.material))
                s.material = NameChecksumFor(value);
        } else if (key == "uv") {
            ok = once(&haveUv) && ReadFloat(value, &s.uv) && s.uv > 0.0f;
        } else if (key == "color") {
            char* end = nullptr;
            unsigned long v = std::strtoul(value.c_str(), &end, 16);
            ok = once(&haveColour) && value.size() == 8 && end && !*end;
            s.colour = static_cast<uint32_t>(v);
        } else if (key == "terrain") {
            char* end = nullptr;
            long v = std::strtol(value.c_str(), &end, 10);
            ok = once(&haveTerrain) && !value.empty() && end && !*end && v >= 0 && v <= 0xFFFF;
            s.terrain = static_cast<uint16_t>(v);
        } else if (key == "hides") {
            ok = once(&haveHides);
            for (const std::string& p : SplitCommas(value)) {
                uint32_t c = 0;
                ok = ok && IsHex8(p, &c);
                if (ok)
                    s.hides.push_back(c);
            }
        }
        // (other keys: a later version's - skipped)
        if (!ok)
            return false;
    }
    if (!haveRect || !haveBase || !haveHeight || !haveMaterial || !s.material)
        return false;
    if (!Normalise(&s))
        return false;
    *out = s;
    return true;
}

std::string FreshName(const std::vector<Spec>& specs)
{
    for (int n = 1;; ++n) {
        char name[32];
        std::snprintf(name, sizeof(name), "ed_ground%02d", n);
        bool taken = std::any_of(specs.begin(), specs.end(), [&](const Spec& s) { return s.name == name; });
        if (!taken)
            return name;
    }
}

bool TileInside(V3 min, V3 max, const Spec& s, float margin)
{
    return min.x >= s.x0 - margin && max.x <= s.x1 + margin && min.z >= s.z0 - margin && max.z <= s.z1 + margin;
}

bool GroundFaceUnder(const V3 c[3], const Spec& s, float tolerance)
{
    V3 n = Cross(c[1] - c[0], c[2] - c[0]);
    float length = std::sqrt(Dot(n, n));
    if (!(length > 0.0f) || std::fabs(n.y) / length < 0.7f)
        return false;
    float minX = std::min({c[0].x, c[1].x, c[2].x}), maxX = std::max({c[0].x, c[1].x, c[2].x});
    float minZ = std::min({c[0].z, c[1].z, c[2].z}), maxZ = std::max({c[0].z, c[1].z, c[2].z});
    float minY = std::min({c[0].y, c[1].y, c[2].y}), maxY = std::max({c[0].y, c[1].y, c[2].y});
    if (maxY < s.base - tolerance || minY > s.base + tolerance)
        return false;
    return maxX > s.x0 + 1.0f && minX < s.x1 - 1.0f && maxZ > s.z0 + 1.0f && minZ < s.z1 - 1.0f;
}

float SnapTo(float value, float grid)
{
    if (!(grid > 0.0f))
        return value;
    return std::floor(value / grid + 0.5f) * grid;
}

} // namespace Front::LevelEditor::Ground
