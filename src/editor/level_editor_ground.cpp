// NOT ORIGINAL - the level editor's raised / lowered ground with ramps (ours). See level_editor_ground.h.

#include "editor/level_editor_ground.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "engine/core/checksums.h"

namespace frontend::level_editor::ground {

// --- the pure parts -----------------------------------------------------------------------------------------------

bool same_spec(const Spec& a, const Spec& b)
{
    return a.name == b.name && a.lower == b.lower && a.x0 == b.x0 && a.z0 == b.z0 && a.x1 == b.x1 && a.z1 == b.z1 &&
           a.base == b.base && a.height == b.height && a.ramps == b.ramps && a.length == b.length &&
           a.material == b.material && a.uv == b.uv && a.colour == b.colour && a.terrain == b.terrain &&
           a.hides == b.hides;
}

namespace {

bool finite(float v) { return std::isfinite(v); }
float confine(float v, float low, float high) { return std::max(low, std::min(high, v)); }

} // namespace

bool normalise(Spec* s)
{
    if (!finite(s->x0) || !finite(s->x1) || !finite(s->z0) || !finite(s->z1) || !finite(s->base) ||
        !finite(s->height) || !finite(s->length) || !finite(s->uv))
        return false;
    if (s->x1 < s->x0)
        std::swap(s->x0, s->x1);
    if (s->z1 < s->z0)
        std::swap(s->z0, s->z1);
    // (too narrow / wide: grown / shrunk about the first corner)
    s->x1 = s->x0 + confine(s->x1 - s->x0, kMinSide, kMaxSide);
    s->z1 = s->z0 + confine(s->z1 - s->z0, kMinSide, kMaxSide);
    s->height = confine(s->height, kMinHeight, kCeilingY);
    s->length = confine(s->length, 0.0f, kMaxLength);
    s->uv = confine(s->uv, 16.0f, 4096.0f);
    return true;
}

float ramp_length(const Spec& s, int side)
{
    Ramp ramp = s.ramps[side];
    if (ramp == Ramp::kNone)
        return 0.0f;
    float length = s.length > 0.0f ? s.length : (ramp == Ramp::kKicker ? 2.0f * s.height : s.height);
    length = std::max(10.0f, length);
    if (s.lower) {
        // (a pit: the ramp opposite takes its share of the width)
        float across = side == kMinusX || side == kPlusX ? s.x1 - s.x0 : s.z1 - s.z0;
        int opposite = side ^ 1;
        float room = s.ramps[opposite] != Ramp::kNone ? across * 0.5f - 10.0f : across - 10.0f;
        length = std::max(10.0f, std::min(length, room));
    }
    return length;
}

std::vector<std::pair<float, float>> quarter_profile(float radius, float height, int segments)
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
        auto d = static_cast<float>(radius * (std::sin(top) - std::sin(t)));
        auto y = static_cast<float>(radius * (1.0 - std::cos(t)));
        if (i == segments) {
            d = 0.0f;
            y = rise;
        }
        points.emplace_back(d, y);
    }
    if (height > radius + 0.01f)
        points.emplace_back(0.0f, height);
    return points;
}

namespace {

V3 get_up() { return {0.0f, 1.0f, 0.0f}; }

// The side's outward direction and its edge's two ends (at height 0) - along the side, the first at its lower x / z.
void side_frame(const Spec& s, int side, V3* out, V3* e0, V3* e1)
{
    switch (side) {
    case kMinusX:
        *out = {-1, 0, 0}, *e0 = {s.x0, 0, s.z0}, *e1 = {s.x0, 0, s.z1};
        break;
    case kPlusX:
        *out = {1, 0, 0}, *e0 = {s.x1, 0, s.z0}, *e1 = {s.x1, 0, s.z1};
        break;
    case kMinusZ:
        *out = {0, 0, -1}, *e0 = {s.x0, 0, s.z0}, *e1 = {s.x1, 0, s.z0};
        break;
    default:
        *out = {0, 0, 1}, *e0 = {s.x0, 0, s.z1}, *e1 = {s.x1, 0, s.z1};
        break;
    }
}

struct Builder {
    Mesh mesh;
    float uv{};

    void get_corner(V3 p, V3 n)
    {
        float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        float u = 0.0f, v = 0.0f;
        if (ay >= ax && ay >= az)
            u = p.x, v = p.z;
        else if (ax >= az)
            u = p.z, v = p.y;
        else
            u = p.x, v = p.y;
        mesh.vertices.push_back({p, n, u / uv, -v / uv});
    }
    // A triangle wound so that its normal points along `hint` (not added when it has no area).
    void get_tri(V3 a, V3 b, V3 c, V3 hint, uint16_t flags)
    {
        V3 n = perpendicular(b - a, c - a);
        float length = std::sqrt(get_dot(n, n));
        if (!(length > 1e-3f))
            return;
        if (get_dot(n, hint) < 0.0f) {
            std::swap(b, c);
            n = n * -1.0f;
        }
        n = n * (1.0f / length);
        auto first = static_cast<uint16_t>(mesh.vertices.size());
        get_corner(a, n);
        get_corner(b, n);
        get_corner(c, n);
        mesh.faces.push_back({first, static_cast<uint16_t>(first + 1), static_cast<uint16_t>(first + 2), flags});
    }
    // A quad (corners in order round it) as two triangles.
    void get_quad(V3 a, V3 b, V3 c, V3 d, V3 hint, uint16_t flags)
    {
        get_tri(a, b, c, hint, flags);
        get_tri(a, c, d, hint, flags);
    }
};

V3 get_at(V3 p, float y) { return {p.x, y, p.z}; }

// A ramp along a side: from its foot (`foot` high, `length` out along `out` from the edge) up to `top` at the edge,
// facing `out` and up; a raise's gets its two end cheeks (facing along the side, away from the ramp).
void add_ramp(Builder* b, Ramp ramp, V3 out, V3 e0, V3 e1, float foot, float top, float length, bool cheeks)
{
    float rise = top - foot;
    std::vector<std::pair<float, float>> profile;
    if (ramp == Ramp::kKicker)
        profile = {{length, 0.0f}, {0.0f, rise}};
    else
        profile = quarter_profile(length, rise, kQuarterSegments);
    for (size_t i = 0; i + 1 < profile.size(); ++i) {
        float d0 = profile[i].first, y0 = foot + profile[i].second;
        float d1 = profile[i + 1].first, y1 = foot + profile[i + 1].second;
        float dd = d1 - d0, dy = y1 - y0;
        // (the surface's normal: out by the rise, up by the run)
        V3 hint = out * dy + get_up() * -dd;
        uint16_t flags = ramp == Ramp::kQuarter && dy >= -dd ? kFlagVert : kFlagRamp;
        b->get_quad(get_at(e0 + out * d0, y0), get_at(e1 + out * d0, y0), get_at(e1 + out * d1, y1),
                    get_at(e0 + out * d1, y1), hint, flags);
    }
    if (!cheeks)
        return;
    V3 along = make_unit(e1 - e0);
    for (int end = 0; end < 2; ++end) {
        V3 e = end ? e1 : e0;
        V3 away = end ? along : along * -1.0f;
        V3 corner = get_at(e, foot);
        for (size_t i = 0; i + 1 < profile.size(); ++i) {
            V3 p = get_at(e + out * profile[i].first, foot + profile[i].second);
            V3 q = get_at(e + out * profile[i + 1].first, foot + profile[i + 1].second);
            b->get_tri(corner, p, q, away, kFlagWall);
        }
    }
}

} // namespace

Mesh get_build(const Spec& spec)
{
    Spec s = spec;
    Builder b;
    b.uv = s.uv > 0.0f ? s.uv : 256.0f;
    if (!normalise(&s))
        return b.mesh;
    if (!s.lower) {
        float top = s.base + s.height, bottom = s.base - kSink;
        b.get_quad({s.x0, top, s.z0}, {s.x1, top, s.z0}, {s.x1, top, s.z1}, {s.x0, top, s.z1}, get_up(), kFlagDeck);
        for (int side = 0; side < 4; ++side) {
            V3 out{}, e0{}, e1{};
            side_frame(s, side, &out, &e0, &e1);
            if (s.ramps[side] == Ramp::kNone)
                b.get_quad(get_at(e0, bottom), get_at(e1, bottom), get_at(e1, top), get_at(e0, top), out, kFlagWall);
            else
                add_ramp(&b, s.ramps[side], out, e0, e1, s.base, top, ramp_length(s, side), true);
        }
    } else {
        float floor = s.base - s.height;
        b.get_quad({s.x0, floor, s.z0}, {s.x1, floor, s.z0}, {s.x1, floor, s.z1}, {s.x0, floor, s.z1}, get_up(),
                   kFlagDeck);
        for (int side = 0; side < 4; ++side) {
            V3 out{}, e0{}, e1{};
            side_frame(s, side, &out, &e0, &e1);
            V3 in = out * -1.0f;
            if (s.ramps[side] == Ramp::kNone)
                b.get_quad(get_at(e0, floor), get_at(e1, floor), get_at(e1, s.base), get_at(e0, s.base), in, kFlagWall);
            else
                add_ramp(&b, s.ramps[side], in, e0, e1, floor, s.base, ramp_length(s, side), false);
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

std::vector<uint16_t> get_strip(const std::vector<Face>& faces, bool two_sided)
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
        if (two_sided)
            add(f.a, f.c, f.b);
    }
    return strip;
}

std::vector<std::array<uint16_t, 3>> strip_triangles(const std::vector<uint16_t>& strip)
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

bool layout_for(uint32_t fvf, uint32_t stride, Layout* layout)
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
    layout->tex_count_ = static_cast<int>((fvf >> 8) & 0xF);
    if (layout->tex_count_)
        layout->tex = at;
    at += 8 * layout->tex_count_;
    return static_cast<uint32_t>(at) == stride;
}

void write_vertices(const Mesh& mesh, const Layout& layout, uint32_t stride, uint32_t colour, uint8_t* out)
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
        for (int t = 0; t < layout.tex_count_; ++t) {
            std::memcpy(out + layout.tex + 8 * t, &v.u, 4);
            std::memcpy(out + layout.tex + 8 * t + 4, &v.v, 4);
        }
        out += stride;
    }
}

char ramp_letter(Ramp ramp) { return ramp == Ramp::kKicker ? 'k' : ramp == Ramp::kQuarter ? 'q' : '-'; }

namespace {

std::string get_number(float v)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", v);
    return text;
}

bool read_float(const std::string& text, float* out)
{
    char* end = nullptr;
    *out = std::strtof(text.c_str(), &end);
    return !text.empty() && end && *end == '\0' && std::isfinite(*out);
}

std::vector<std::string> split_commas(const std::string& text)
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

bool is_hex8(const std::string& text, uint32_t* value)
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

bool valid_word(const std::string& name)
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
// A name's checksum (the game's CRC; "0x" + 8 hex digits: that checksum - as level_editor_triggers.cpp's checksum_of).
uint32_t name_checksum_for(const std::string& text)
{
    uint32_t value = 0;
    if (is_hex8(text, &value))
        return value;
    return text.empty() ? 0u : checksums::hash_from_text(text.c_str());
}
} // namespace

std::string link_record(const Spec& s, const char* material_name)
{
    std::ostringstream out;
    char colour[16];
    std::snprintf(colour, sizeof(colour), "%08X", s.colour);
    char material[16];
    std::snprintf(material, sizeof(material), "0x%08X", s.material);
    std::string material_text =
        material_name && *material_name && valid_word(material_name) && name_checksum_for(material_name) == s.material
            ? std::string(material_name)
            : std::string(material);
    out << s.name << (s.lower ? " lower" : " raise") << " rect=" << get_number(s.x0) << ',' << get_number(s.z0) << ','
        << get_number(s.x1) << ',' << get_number(s.z1) << " base=" << get_number(s.base)
        << " height=" << get_number(s.height) << " ramps=" << ramp_letter(s.ramps[0]) << ',' << ramp_letter(s.ramps[1])
        << ',' << ramp_letter(s.ramps[2]) << ',' << ramp_letter(s.ramps[3]) << " length=" << get_number(s.length)
        << " material=" << material_text << " uv=" << get_number(s.uv) << " color=" << colour
        << " terrain=" << s.terrain;
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

bool read_record(const std::string& text, Spec* out)
{
    std::istringstream in(text);
    Spec s;
    std::string kind;
    if (!(in >> s.name >> kind) || !valid_word(s.name))
        return false;
    if (kind == "raise")
        s.lower = false;
    else if (kind == "lower")
        s.lower = true;
    else
        return false;
    bool have_rect = false, have_base = false, have_height = false, have_material = false, have_ramps = false,
         have_length = false, have_uv = false, have_colour = false, have_terrain = false, have_hides = false;
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
            std::vector<std::string> parts = split_commas(value);
            ok = once(&have_rect) && parts.size() == 4 && read_float(parts[0], &s.x0) && read_float(parts[1], &s.z0) &&
                 read_float(parts[2], &s.x1) && read_float(parts[3], &s.z1);
        } else if (key == "base") {
            ok = once(&have_base) && read_float(value, &s.base);
        } else if (key == "height") {
            ok = once(&have_height) && read_float(value, &s.height) && s.height > 0.0f;
        } else if (key == "ramps") {
            std::vector<std::string> parts = split_commas(value);
            ok = once(&have_ramps) && parts.size() == 4;
            for (size_t i = 0; ok && i < 4; ++i) {
                const std::string& p = parts[i];
                if (p == "-")
                    s.ramps[i] = Ramp::kNone;
                else if (p == "k")
                    s.ramps[i] = Ramp::kKicker;
                else if (p == "q")
                    s.ramps[i] = Ramp::kQuarter;
                else
                    ok = false;
            }
        } else if (key == "length") {
            ok = once(&have_length) && read_float(value, &s.length) && s.length >= 0.0f;
        } else if (key == "material") {
            ok = once(&have_material) && (is_hex8(value, &s.material) || valid_word(value));
            if (ok && !is_hex8(value, &s.material))
                s.material = name_checksum_for(value);
        } else if (key == "uv") {
            ok = once(&have_uv) && read_float(value, &s.uv) && s.uv > 0.0f;
        } else if (key == "color") {
            char* end = nullptr;
            unsigned long v = std::strtoul(value.c_str(), &end, 16);
            ok = once(&have_colour) && value.size() == 8 && end && !*end;
            s.colour = static_cast<uint32_t>(v);
        } else if (key == "terrain") {
            char* end = nullptr;
            long v = std::strtol(value.c_str(), &end, 10);
            ok = once(&have_terrain) && !value.empty() && end && !*end && v >= 0 && v <= 0xFFFF;
            s.terrain = static_cast<uint16_t>(v);
        } else if (key == "hides") {
            ok = once(&have_hides);
            for (const std::string& p : split_commas(value)) {
                uint32_t c = 0;
                ok = ok && is_hex8(p, &c);
                if (ok)
                    s.hides.push_back(c);
            }
        }
        // (other keys: a later version's - skipped)
        if (!ok)
            return false;
    }
    if (!have_rect || !have_base || !have_height || !have_material || !s.material)
        return false;
    if (!normalise(&s))
        return false;
    *out = s;
    return true;
}

std::string fresh_name(const std::vector<Spec>& specs)
{
    for (int n = 1;; ++n) {
        char name[32];
        std::snprintf(name, sizeof(name), "ed_ground%02d", n);
        bool taken = std::any_of(specs.begin(), specs.end(), [&](const Spec& s) { return s.name == name; });
        if (!taken)
            return name;
    }
}

bool tile_inside(V3 min, V3 max, const Spec& s, float margin)
{
    return min.x >= s.x0 - margin && max.x <= s.x1 + margin && min.z >= s.z0 - margin && max.z <= s.z1 + margin;
}

bool ground_face_under(const V3 c[3], const Spec& s, float tolerance)
{
    V3 n = perpendicular(c[1] - c[0], c[2] - c[0]);
    float length = std::sqrt(get_dot(n, n));
    if (!(length > 0.0f) || std::fabs(n.y) / length < 0.7f)
        return false;
    float min_x = std::min({c[0].x, c[1].x, c[2].x}), max_x = std::max({c[0].x, c[1].x, c[2].x});
    float min_z = std::min({c[0].z, c[1].z, c[2].z}), max_z = std::max({c[0].z, c[1].z, c[2].z});
    float min_y = std::min({c[0].y, c[1].y, c[2].y}), max_y = std::max({c[0].y, c[1].y, c[2].y});
    if (max_y < s.base - tolerance || min_y > s.base + tolerance)
        return false;
    return max_x > s.x0 + 1.0f && min_x < s.x1 - 1.0f && max_z > s.z0 + 1.0f && min_z < s.z1 - 1.0f;
}

float snap_to(float value, float grid)
{
    if (!(grid > 0.0f))
        return value;
    return std::floor(value / grid + 0.5f) * grid;
}

} // namespace frontend::level_editor::ground
