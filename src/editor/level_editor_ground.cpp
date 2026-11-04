// NOT ORIGINAL - the level editor's raised / lowered ground with ramps (ours). See level_editor_ground.h.

#include "editor/level_editor_ground.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <span>
#include <sstream>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "engine/render/coll_obj.h"
#include "editor/level_editor.h"
#include "engine/core/checksums.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/packed_colour.h"
#include "engine/gfx/render_state.h"

namespace frontend::level_editor::ground {

namespace {

constexpr float kKickerLengthFactor = 2.0F; // a kicker ramp is this many times as long as the ground is high
constexpr float kMinRampLength = 10.0F;
constexpr float kRadiusMargin = 0.01F;
constexpr float kTinyLength = 1e-3F;
constexpr uint32_t kLastBuiltInShader = 6;  // vertex format values up to this are the engine's built-in vertex shaders
constexpr uint32_t kShaderHandleMin = 0x10000; // vertex format values from here are shader handles
constexpr uint32_t kFvfUnsupportedMask = 0x400E; // position bits other than plain XYZ (blend weights, XYZRHW ...)
constexpr int kPositionBytes = 12;  // three floats
constexpr int kNormalBytes = 12;    // three floats
constexpr uint32_t kFvfPointSize = 0x020; // ... a point size
constexpr uint32_t kFvfTexCountShift = 8; // the texture coordinate set count sits in the bits from here
constexpr uint32_t kFvfTexCountMask = 0xF;
constexpr int kTexCoordBytes = 8;   // two floats a set
constexpr std::size_t kNumberTextSize = 32;
constexpr std::size_t kPrefixedHexLength = 10; // "0x" and eight digits
constexpr std::size_t kMaxNameLength = 64;
constexpr std::size_t kColourTextSize = 16;
constexpr std::size_t kMaterialTextSize = 16;
constexpr std::size_t kHexTextSize = 16;
constexpr std::size_t kChecksumDigits = 8;  // hex digits of a checksum
constexpr int kMaxTerrain = 0xFFFF;
constexpr float kFloorNormalY = 0.7F; // a face whose normal is this upright is floor

constexpr float kRoundHalf = 0.5F; // added before truncating: round to the nearest

} // namespace

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

bool normalise(Spec* spec)
{
    if (!finite(spec->x0) || !finite(spec->x1) || !finite(spec->z0) || !finite(spec->z1) || !finite(spec->base) ||
        !finite(spec->height) || !finite(spec->length) || !finite(spec->uv)) {
        return false;
    }
    if (spec->x1 < spec->x0) {
        std::swap(spec->x0, spec->x1);
    }
    if (spec->z1 < spec->z0) {
        std::swap(spec->z0, spec->z1);
    }
    // (too narrow / wide: grown / shrunk about the first corner)
    spec->x1 = spec->x0 + confine(spec->x1 - spec->x0, kMinSide, kMaxSide);
    spec->z1 = spec->z0 + confine(spec->z1 - spec->z0, kMinSide, kMaxSide);
    spec->height = confine(spec->height, kMinHeight, kCeilingY);
    spec->length = confine(spec->length, 0.0F, kMaxLength);
    spec->uv = confine(spec->uv, kMinUvScale, kMaxUvScale);
    return true;
}

float ramp_length(const Spec& spec, int side)
{
    const Ramp ramp = core::at(spec.ramps, side);
    if (ramp == Ramp::kNone) {
        return 0.0F;
    }
    float length = 0.0F;
    if (spec.length > 0.0F) {
        length = spec.length;
    } else {
        length = ramp == Ramp::kKicker ? kKickerLengthFactor * spec.height : spec.height;
    }
    length = std::max(kMinRampLength, length);
    if (spec.lower) {
        // (a pit: the ramp opposite takes its share of the width)
        const float across = side == kMinusX || side == kPlusX ? spec.x1 - spec.x0 : spec.z1 - spec.z0;
        const int opposite = side ^ 1;
        const float room = core::at(spec.ramps, opposite) != Ramp::kNone ? (across * 0.5F) - 10.0F : across - 10.0F;
        length = std::max(kMinRampLength, std::min(length, room));
    }
    return length;
}

std::vector<std::pair<float, float>> quarter_profile(QuarterShape shape)
{
    const auto radius = shape.radius;
    const auto shape_height = shape.height;
    auto segments = shape.segments;
    std::vector<std::pair<float, float>> points;
    segments = std::max(segments, 1);
    // An arc of `radius`: its foot (out `radius` sin(top), up 0) to the angle reaching min(height, radius) - vertical
    // when the full quarter; d = R (sin top - sin t), y = R (1 - cos t).
    const float rise = std::min(shape_height, radius);
    const double top = std::acos(std::max(-1.0, std::min(1.0, 1.0 - (static_cast<double>(rise) / radius))));
    for (int i = 0; i <= segments; ++i) {
        const double fraction = top * i / segments;
        auto drop = static_cast<float>(radius * (std::sin(top) - std::sin(fraction)));
        auto y = static_cast<float>(radius * (1.0 - std::cos(fraction)));
        if (i == segments) {
            drop = 0.0F;
            y = rise;
        }
        points.emplace_back(drop, y);
    }
    if (shape_height > radius + kRadiusMargin) {
        points.emplace_back(0.0F, shape_height);
    }
    return points;
}

namespace {

V3 get_up() { return {.x = 0.0F, .y = 1.0F, .z = 0.0F}; }

// The side's outward direction and its edge's two ends (at height 0) - along the side, the first at its lower x / z.
void side_frame(const Spec& spec, int side, V3* out, V3* e0, V3* e1)
{
    switch (side) {
    case kMinusX:
        *out = {.x = -1, .y = 0, .z = 0}, *e0 = {.x = spec.x0, .y = 0, .z = spec.z0},
        *e1 = {.x = spec.x0, .y = 0, .z = spec.z1};
        break;
    case kPlusX:
        *out = {.x = 1, .y = 0, .z = 0}, *e0 = {.x = spec.x1, .y = 0, .z = spec.z0},
        *e1 = {.x = spec.x1, .y = 0, .z = spec.z1};
        break;
    case kMinusZ:
        *out = {.x = 0, .y = 0, .z = -1}, *e0 = {.x = spec.x0, .y = 0, .z = spec.z0},
        *e1 = {.x = spec.x1, .y = 0, .z = spec.z0};
        break;
    default:
        *out = {.x = 0, .y = 0, .z = 1}, *e0 = {.x = spec.x0, .y = 0, .z = spec.z1},
        *e1 = {.x = spec.x1, .y = 0, .z = spec.z1};
        break;
    }
}

struct Builder {
    // The fields other code reads and sets.
    Mesh& mesh() { return mesh_; }
    [[nodiscard]] const Mesh& mesh() const { return mesh_; }
    void set_uv(float value) { uv = value; }

private:
    Mesh mesh_;
    float uv{};

public:
    void get_corner(V3 point, V3 normal)
    {
        const float ax = std::fabs(normal.x);
        const float ay = std::fabs(normal.y);
        const float az = std::fabs(normal.z);
        float u = 0.0F;
        float v = 0.0F;
        if (ay >= ax && ay >= az) {
            u = point.x, v = point.z;
        } else if (ax >= az) {
            u = point.z, v = point.y;
        } else {
            u = point.x, v = point.y;
        }
        mesh_.vertices.push_back({.pos = point, .normal = normal, .u = u / uv, .v = -v / uv});
    }
    // A triangle's corners.
    struct Triangle {
        V3 a{};
        V3 b{};
        V3 c{};
    };

    // A triangle wound so that its normal points along `hint` (not added when it has no area).
    void get_tri(Triangle triangle, V3 hint, render::FaceFlag flags)
    {
        auto corner_a = triangle.a;
        auto corner_b = triangle.b;
        auto corner_c = triangle.c;
        V3 normal = perpendicular(corner_b - corner_a, corner_c - corner_a);
        const float length = std::sqrt(get_dot(normal, normal));
        if (!(length > kTinyLength)) {
            return;
        }
        if (get_dot(normal, hint) < 0.0F) {
            std::swap(corner_b, corner_c);
            normal = normal * -1.0F;
        }
        normal = normal * (1.0F / length);
        auto first = static_cast<uint16_t>(mesh_.vertices.size());
        get_corner(corner_a, normal);
        get_corner(corner_b, normal);
        get_corner(corner_c, normal);
        mesh_.faces.push_back({.a = first, .b = static_cast<uint16_t>(first + 1), .c = static_cast<uint16_t>(first + 2), .flags = flags});
    }
    // A quad (corners in order round it) as two triangles.
    void get_quad(V3 a, V3 b, V3 corner_c, V3 corner_d, V3 hint, render::FaceFlag flags)
    {
        get_tri({.a = a, .b = b, .c = corner_c}, hint, flags);
        get_tri({.a = a, .b = corner_c, .c = corner_d}, hint, flags);
    }
};

V3 get_at(V3 point, float y) { return {.x = point.x, .y = y, .z = point.z}; }

// A ramp's foot and top heights and its length.
struct RampHeights {
    float foot{};
    float top{};
    float length{};
};

// A ramp along a side: from its foot (`foot` high, `length` out along `out` from the edge) up to `top` at the edge,
// facing `out` and up; a raise's gets its two end cheeks (facing along the side, away from the ramp).
void add_ramp(Builder* b, Ramp ramp, V3 out, V3 e0, V3 e1, RampHeights heights, bool cheeks)
{
    const auto foot = heights.foot;
    const auto top = heights.top;
    const auto length = heights.length;
    float rise = top - foot;
    std::vector<std::pair<float, float>> profile;
    if (ramp == Ramp::kKicker) {
        profile = {{length, 0.0F}, {0.0F, rise}};
    } else {
        profile = quarter_profile({.radius = length, .height = rise, .segments = kQuarterSegments});
    }
    for (size_t i = 0; i + 1 < profile.size(); ++i) {
        const float d0 = core::at(profile, i).first;
        const float y0 = foot + core::at(profile, i).second;
        const float d1 = core::at(profile, i + 1).first;
        const float y1 = foot + core::at(profile, i + 1).second;
        const float dd = d1 - d0;
        const float dy = y1 - y0;
        // (the surface's normal: out by the rise, up by the run)
        const V3 hint = out * dy + get_up() * -dd;
        const render::FaceFlag flags = ramp == Ramp::kQuarter && dy >= -dd ? kFlagVert : kFlagRamp;
        b->get_quad(get_at(e0 + out * d0, y0), get_at(e1 + out * d0, y0), get_at(e1 + out * d1, y1),
                    get_at(e0 + out * d1, y1), hint, flags);
    }
    if (!cheeks) {
        return;
    }
    const V3 along = make_unit(e1 - e0);
    for (int end = 0; end < 2; ++end) {
        const V3 end_point = (end != 0) ? e1 : e0;
        const V3 away = (end != 0) ? along : along * -1.0F;
        const V3 corner = get_at(end_point, foot);
        for (size_t i = 0; i + 1 < profile.size(); ++i) {
            const V3 point = get_at(end_point + out * core::at(profile, i).first, foot + core::at(profile, i).second);
            const V3 next_point =
                get_at(end_point + out * core::at(profile, i + 1).first, foot + core::at(profile, i + 1).second);
            b->get_tri({.a = corner, .b = point, .c = next_point}, away, kFlagWall);
        }
    }
}

} // namespace

Mesh get_build(const Spec& spec)
{
    Spec edited_spec = spec;
    Builder builder;
    builder.set_uv(edited_spec.uv > 0.0F ? edited_spec.uv : kDefaultUvScale);
    if (!normalise(&edited_spec)) {
        return builder.mesh();
    }
    if (!edited_spec.lower) {
        const float top = edited_spec.base + edited_spec.height;
        const float bottom = edited_spec.base - kSink;
        builder.get_quad({.x = edited_spec.x0, .y = top, .z = edited_spec.z0},
                         {.x = edited_spec.x1, .y = top, .z = edited_spec.z0},
                         {.x = edited_spec.x1, .y = top, .z = edited_spec.z1},
                         {.x = edited_spec.x0, .y = top, .z = edited_spec.z1}, get_up(), kFlagDeck);
        for (int side = 0; side < 4; ++side) {
            V3 out{};
            V3 e0{};
            V3 e1{};
            side_frame(edited_spec, side, &out, &e0, &e1);
            if (core::at(edited_spec.ramps, side) == Ramp::kNone) {
                builder.get_quad(get_at(e0, bottom), get_at(e1, bottom), get_at(e1, top), get_at(e0, top), out,
                                 kFlagWall);
            } else {
                add_ramp(&builder, core::at(edited_spec.ramps, side), out, e0, e1,
                         {.foot = edited_spec.base, .top = top, .length = ramp_length(edited_spec, side)}, true);
            }
        }
    } else {
        const float floor = edited_spec.base - edited_spec.height;
        builder.get_quad({.x = edited_spec.x0, .y = floor, .z = edited_spec.z0},
                         {.x = edited_spec.x1, .y = floor, .z = edited_spec.z0},
                         {.x = edited_spec.x1, .y = floor, .z = edited_spec.z1},
                         {.x = edited_spec.x0, .y = floor, .z = edited_spec.z1}, get_up(), kFlagDeck);
        for (int side = 0; side < 4; ++side) {
            V3 out{};
            V3 e0{};
            V3 e1{};
            side_frame(edited_spec, side, &out, &e0, &e1);
            const V3 in = out * -1.0F;
            if (core::at(edited_spec.ramps, side) == Ramp::kNone) {
                builder.get_quad(get_at(e0, floor), get_at(e1, floor), get_at(e1, edited_spec.base),
                                 get_at(e0, edited_spec.base), in, kFlagWall);
            } else {
                add_ramp(&builder, core::at(edited_spec.ramps, side), in, e0, e1,
                         {.foot = floor, .top = edited_spec.base, .length = ramp_length(edited_spec, side)}, false);
            }
        }
    }
    Mesh& mesh = builder.mesh();
    if (!mesh.vertices.empty()) {
        mesh.min = mesh.max = core::at(mesh.vertices, 0).pos;
        for (const Vertex& v : mesh.vertices) {
            mesh.min = {.x = std::min(mesh.min.x, v.pos.x),
                        .y = std::min(mesh.min.y, v.pos.y),
                        .z = std::min(mesh.min.z, v.pos.z)};
            mesh.max = {.x = std::max(mesh.max.x, v.pos.x),
                        .y = std::max(mesh.max.y, v.pos.y),
                        .z = std::max(mesh.max.z, v.pos.z)};
        }
    }
    return mesh;
}

std::vector<uint16_t> get_strip(const std::vector<Face>& faces, bool two_sided)
{
    std::vector<uint16_t> strip;
    auto add = [&](uint16_t a, uint16_t b, uint16_t vertex_c) {
        if (!strip.empty()) {
            // (a join: the last index and this face's first, repeated until the face starts at an even position)
            strip.push_back(strip.back());
            strip.push_back(a);
            strip.push_back(a);
            if ((strip.size() - 1) % 2 != 0) {
                strip.push_back(a);
            }
        } else {
            strip.push_back(a);
        }
        strip.push_back(b);
        strip.push_back(vertex_c);
    };
    for (const Face& face : faces) {
        add(face.a, face.b, face.c);
        if (two_sided) {
            add(face.a, face.c, face.b);
        }
    }
    return strip;
}

std::vector<std::array<uint16_t, 3>> strip_triangles(const std::vector<uint16_t>& strip)
{
    std::vector<std::array<uint16_t, 3>> triangles;
    for (size_t i = 0; i + 2 < strip.size(); ++i) {
        uint16_t vertex_a = core::at(strip, i);
        uint16_t vertex_b = core::at(strip, i + 1);
        uint16_t const vertex_c = core::at(strip, i + 2);
        if (vertex_a == vertex_b || vertex_b == vertex_c || vertex_a == vertex_c) {
            continue;
        }
        if ((i % 2) != 0U) {
            std::swap(vertex_a, vertex_b);
        }
        triangles.push_back({vertex_a, vertex_b, vertex_c});
    }
    return triangles;
}

bool layout_for(VertexFormat format, Layout* layout)
{
    const auto fvf = format.fvf;
    const auto stride = format.stride;
    *layout = Layout{};
    // (0..6: the engine's built-in vertex shaders; 0x10000 and above: a shader handle - texture coordinate sizes other
    // than two never appear in the scene's formats)
    if (fvf <= kLastBuiltInShader || fvf >= kShaderHandleMin || (fvf & kFvfUnsupportedMask) != 0x002) {
        return false;
    }
    int at = kPositionBytes;
    if ((fvf & render::kFvfNormal) != 0U) {
        layout->normal = at, at += kNormalBytes;
    }
    if ((fvf & kFvfPointSize) != 0U) {
        layout->size = at, at += 4;
    }
    if ((fvf & render::kFvfDiffuse) != 0U) {
        layout->diffuse = at, at += 4;
    }
    if ((fvf & render::kFvfSpecular) != 0U) {
        layout->specular = at, at += 4;
    }
    layout->tex_count_ = static_cast<int>((fvf >> kFvfTexCountShift) & kFvfTexCountMask);
    if (layout->tex_count_ != 0) {
        layout->tex = at;
    }
    at += kTexCoordBytes * layout->tex_count_;
    return ints::as_unsigned(at) == stride;
}

void write_vertices(const Mesh& mesh, const Layout& layout, uint32_t stride, colour::Packed colour_word, std::span<uint8_t> out)
{
    const uint32_t colour = colour_word.value();
    for (const Vertex& v : mesh.vertices) {
        const std::span<uint8_t> one = out.first(stride);
        std::memset(one.data(), 0, stride);
        std::memcpy(one.data(), &v.pos, kPositionBytes);
        if (layout.normal >= 0) {
            std::memcpy(one.subspan(static_cast<size_t>(layout.normal)).data(), &v.normal, kNormalBytes);
        }
        if (layout.size >= 0) {
            float unit = 1.0F;
            std::memcpy(one.subspan(static_cast<size_t>(layout.size)).data(), &unit, 4);
        }
        if (layout.diffuse >= 0) {
            std::memcpy(one.subspan(static_cast<size_t>(layout.diffuse)).data(), &colour, 4);
        }
        for (int texture_index = 0; texture_index < layout.tex_count_; ++texture_index) {
            std::memcpy(one.subspan(static_cast<size_t>(layout.tex + (kTexCoordBytes * texture_index))).data(), &v.u,
                        4);
            std::memcpy(one.subspan(static_cast<size_t>(layout.tex + (kTexCoordBytes * texture_index) + 4)).data(),
                        &v.v, 4);
        }
        out = out.subspan(stride);
    }
}

char ramp_letter(Ramp ramp)
{
    switch (ramp) {
    case Ramp::kKicker:
        return 'k';
    case Ramp::kQuarter:
        return 'q';
    default:
        return '-';
    }
}

namespace {

std::string get_number(float v)
{
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "%.9g", v);
    return text.data();
}

bool read_float(const std::string& text, float* out)
{
    char* end = nullptr;
    *out = std::strtof(text.c_str(), &end);
    return !text.empty() && (end != nullptr) && *end == '\0' && std::isfinite(*out);
}

std::vector<std::string> split_commas(const std::string& text)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return parts;
}

bool is_hex8(const std::string& text, uint32_t* value)
{
    if (text.size() != kPrefixedHexLength || core::at(text, 0) != '0' ||
        (core::at(text, 1) != 'x' && core::at(text, 1) != 'X')) {
        return false;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(std::next(text.c_str(), 2), &end, 16);
    if (!end || (*end != '\0')) {
        return false;
    }
    *value = static_cast<uint32_t>(v);
    return true;
}

bool valid_word(const std::string& name)
{
    if (name.empty() || name.size() > kMaxNameLength) {
        return false;
    }
    return std::ranges::all_of(name, [](const char character) {
        return (std::isalnum(static_cast<unsigned char>(character)) != 0) || character == '_';
    });
}

} // namespace

namespace {
// A name's checksum (the game's CRC; "0x" + 8 hex digits: that checksum - as level_editor_triggers.cpp's checksum_of).
uint32_t name_checksum_for(const std::string& text)
{
    uint32_t value = 0;
    if (is_hex8(text, &value)) {
        return value;
    }
    return text.empty() ? 0U : checksums::hash_from_text(text.c_str());
}
} // namespace

std::string link_record(const Spec& spec, const char* material_name)
{
    std::ostringstream out;
    std::array<char, kColourTextSize> colour{};
    text::format_to(colour, "%08X", spec.colour);
    std::array<char, kMaterialTextSize> material{};
    text::format_to(material, "0x%08X", spec.material);
    const std::string material_text = material_name && (*material_name != '\0') && valid_word(material_name) &&
                                              name_checksum_for(material_name) == spec.material
                                          ? std::string(material_name)
                                          : std::string(material.data());
    out << spec.name << (spec.lower ? " lower" : " raise") << " rect=" << get_number(spec.x0) << ','
        << get_number(spec.z0) << ',' << get_number(spec.x1) << ',' << get_number(spec.z1)
        << " base=" << get_number(spec.base) << " height=" << get_number(spec.height)
        << " ramps=" << ramp_letter(core::at(spec.ramps, 0)) << ',' << ramp_letter(core::at(spec.ramps, 1)) << ','
        << ramp_letter(core::at(spec.ramps, 2)) << ',' << ramp_letter(core::at(spec.ramps, 3))
        << " length=" << get_number(spec.length) << " material=" << material_text << " uv=" << get_number(spec.uv)
        << " color=" << colour.data() << " terrain=" << spec.terrain;
    if (!spec.hides.empty()) {
        out << " hides=";
        for (size_t i = 0; i < spec.hides.size(); ++i) {
            std::array<char, kHexTextSize> hex{};
            text::format_to(hex, "0x%08X", core::at(spec.hides, i));
            out << ((i != 0U) ? "," : "") << hex.data();
        }
    }
    return out.str();
}

bool read_record(const std::string& text, Spec* out)
{
    std::istringstream in(text);
    Spec spec;
    std::string kind;
    if (!(in >> spec.name >> kind) || !valid_word(spec.name)) {
        return false;
    }
    if (kind == "raise") {
        spec.lower = false;
    } else if (kind == "lower") {
        spec.lower = true;
    } else {
        return false;
    }
    bool have_rect = false;
    bool have_base = false;
    bool have_height = false;
    bool have_material = false;
    bool have_ramps = false;
    bool have_length = false;
    bool have_uv = false;
    bool have_colour = false;
    bool have_terrain = false;
    bool have_hides = false;
    auto once = [](bool* seen) {
        if (*seen) {
            return false;
        }
        *seen = true;
        return true;
    };
    for (std::string word; in >> word;) {
        const size_t eq = word.find('=');
        if (eq == std::string::npos || eq == 0) {
            return false;
        }
        const std::string key = word.substr(0, eq);
        const std::string value = word.substr(eq + 1);
        bool ok = true;
        if (key == "rect") {
            std::vector<std::string> parts = split_commas(value);
            ok = once(&have_rect) && parts.size() == 4 && read_float(core::at(parts, 0), &spec.x0) &&
                 read_float(core::at(parts, 1), &spec.z0) && read_float(core::at(parts, 2), &spec.x1) &&
                 read_float(core::at(parts, 3), &spec.z1);
        } else if (key == "base") {
            ok = once(&have_base) && read_float(value, &spec.base);
        } else if (key == "height") {
            ok = once(&have_height) && read_float(value, &spec.height) && spec.height > 0.0F;
        } else if (key == "ramps") {
            std::vector<std::string> parts = split_commas(value);
            ok = once(&have_ramps) && parts.size() == 4;
            for (size_t i = 0; ok && i < 4; ++i) {
                const std::string& part = core::at(parts, i);
                if (part == "-") {
                    core::at(spec.ramps, i) = Ramp::kNone;
                } else if (part == "k") {
                    core::at(spec.ramps, i) = Ramp::kKicker;
                } else if (part == "q") {
                    core::at(spec.ramps, i) = Ramp::kQuarter;
                } else {
                    ok = false;
                }
            }
        } else if (key == "length") {
            ok = once(&have_length) && read_float(value, &spec.length) && spec.length >= 0.0F;
        } else if (key == "material") {
            ok = once(&have_material) && (is_hex8(value, &spec.material) || valid_word(value));
            if (ok && !is_hex8(value, &spec.material)) {
                spec.material = name_checksum_for(value);
            }
        } else if (key == "uv") {
            ok = once(&have_uv) && read_float(value, &spec.uv) && spec.uv > 0.0F;
        } else if (key == "color") {
            char* end = nullptr;
            const unsigned long v = std::strtoul(value.c_str(), &end, 16);
            ok = once(&have_colour) && value.size() == kChecksumDigits && (end != nullptr) && (*end == '\0');
            spec.colour = static_cast<uint32_t>(v);
        } else if (key == "terrain") {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            ok = once(&have_terrain) && !value.empty() && (end != nullptr) && (*end == '\0') && v >= 0 &&
                 v <= kMaxTerrain;
            spec.terrain = static_cast<uint16_t>(v);
        } else if (key == "hides") {
            ok = once(&have_hides);
            for (const std::string& part : split_commas(value)) {
                uint32_t colour = 0;
                ok = ok && is_hex8(part, &colour);
                if (ok) {
                    spec.hides.push_back(colour);
                }
            }
        }
        // (other keys: a later version's - skipped)
        if (!ok) {
            return false;
        }
    }
    if (!have_rect || !have_base || !have_height || !have_material || (spec.material == 0U)) {
        return false;
    }
    if (!normalise(&spec)) {
        return false;
    }
    *out = spec;
    return true;
}

std::string fresh_name(const std::vector<Spec>& specs)
{
    for (int suffix = 1;; ++suffix) {
        std::array<char, 32> name{};
        text::format_to(name, "ed_ground%02d", suffix);
        const bool taken = std::ranges::any_of(specs, [&](const Spec& spec) { return spec.name == name.data(); });
        if (!taken) {
            return name.data();
        }
    }
}

bool tile_inside(V3 min, V3 max, const Spec& spec, float margin)
{
    return min.x >= spec.x0 - margin && max.x <= spec.x1 + margin && min.z >= spec.z0 - margin &&
           max.z <= spec.z1 + margin;
}

bool ground_face_under(std::span<const V3, 3> corners, const Spec& spec, float tolerance)
{
    const V3 normal =
        perpendicular(core::at(corners, 1) - core::at(corners, 0), core::at(corners, 2) - core::at(corners, 0));
    const float length = std::sqrt(get_dot(normal, normal));
    if (!(length > 0.0F) || std::fabs(normal.y) / length < kFloorNormalY) {
        return false;
    }
    const float min_x = std::min({core::at(corners, 0).x, core::at(corners, 1).x, core::at(corners, 2).x});
    const float max_x = std::max({core::at(corners, 0).x, core::at(corners, 1).x, core::at(corners, 2).x});
    const float min_z = std::min({core::at(corners, 0).z, core::at(corners, 1).z, core::at(corners, 2).z});
    const float max_z = std::max({core::at(corners, 0).z, core::at(corners, 1).z, core::at(corners, 2).z});
    const float min_y = std::min({core::at(corners, 0).y, core::at(corners, 1).y, core::at(corners, 2).y});
    const float max_y = std::max({core::at(corners, 0).y, core::at(corners, 1).y, core::at(corners, 2).y});
    if (max_y < spec.base - tolerance || min_y > spec.base + tolerance) {
        return false;
    }
    return max_x > spec.x0 + 1.0F && min_x < spec.x1 - 1.0F && max_z > spec.z0 + 1.0F && min_z < spec.z1 - 1.0F;
}

float snap_to(float value, float grid)
{
    if (!(grid > 0.0F)) {
        return value;
    }
    return std::floor((value / grid) + kRoundHalf) * grid;
}

} // namespace frontend::level_editor::ground
