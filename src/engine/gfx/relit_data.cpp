// NOT ORIGINAL - a level's relit data: see relit_data.h.
#include "engine/gfx/relit_data.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"

namespace render::rt {

namespace {

constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kVertexHalves = 17; // sun_baked, sky, bounce 3, bounce_local 3, tint 3, residual 3, lamp 3, sun_missed
constexpr uint32_t kMostCells = 1U << 20U;   // an agreement grid at most this many cells
constexpr uint32_t kMostVertices = 1U << 24U; // a mesh at most this many vertices
// (half floats)
constexpr uint32_t kFloatSign = 0x80000000U;
constexpr uint32_t kFloatAbs = 0x7FFFFFFFU;
constexpr uint32_t kFloatExpShift = 23;
constexpr uint32_t kFloatMantissa = 0x007FFFFFU;
constexpr int kFloatBias = 127;
constexpr int kHalfBias = 15;
constexpr int kHalfMaxExp = 30;
constexpr uint32_t kHalfMantissaBits = 10;
constexpr uint32_t kHalfMantissa = 0x3FFU;
constexpr uint32_t kHalfSignShift = 16;
constexpr uint16_t kHalfLargest = 0x7BFFU;
constexpr uint16_t kHalfSign = 0x8000U;
constexpr uint32_t kHalfExpMask = 0x1FU;
constexpr uint32_t kMantissaDrop = kFloatExpShift - kHalfMantissaBits; // 13

uint64_t fnv_bytes(uint64_t hash, std::span<const uint8_t> bytes)
{
    for (const uint8_t byte : bytes) {
        hash = (hash ^ byte) * kFnvPrime;
    }
    return hash;
}

template <typename T>
uint64_t fnv_value(uint64_t hash, const T& value)
{
    std::array<uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    return fnv_bytes(hash, bytes);
}

class Writer {
public:
    template <typename T>
    void put(const T& value)
    {
        std::array<char, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(T));
        text_.append(bytes.data(), bytes.size());
    }
    void put3(const std::array<float, 3>& value)
    {
        for (const float part : value) {
            put(part);
        }
    }
    void half(float value) { put(to_half(value)); }
    void half3(const Rgbf& value)
    {
        for (const float part : value) {
            half(part);
        }
    }
    [[nodiscard]] std::string& text() { return text_; }

private:
    std::string text_;
};

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}
    template <typename T>
    bool get(T* value)
    {
        if (at_ + sizeof(T) > text_.size()) {
            return false;
        }
        std::memcpy(value, text_.substr(at_, sizeof(T)).data(), sizeof(T));
        at_ += sizeof(T);
        return true;
    }
    bool get3(std::array<float, 3>* value)
    {
        return get(&std::get<0>(*value)) && get(&std::get<1>(*value)) && get(&std::get<2>(*value));
    }
    bool half(float* value)
    {
        uint16_t bits = 0;
        if (!get(&bits)) {
            return false;
        }
        *value = from_half(bits);
        return true;
    }
    bool half3(Rgbf* value)
    {
        return half(&std::get<0>(*value)) && half(&std::get<1>(*value)) && half(&std::get<2>(*value));
    }
    [[nodiscard]] bool done() const { return at_ == text_.size(); }

private:
    std::string_view text_;
    std::size_t at_ = 0;
};

} // namespace

const char* relit_verdict_name(RelitVerdict verdict)
{
    switch (verdict) {
    case RelitVerdict::kRelit:
        return "relit";
    case RelitVerdict::kNoSun:
        return "no sun in the bake";
    case RelitVerdict::kNoModel:
        return "no sunlight found in the bake";
    }
    return "?";
}

uint16_t to_half(float value)
{
    const auto bits = std::bit_cast<uint32_t>(value);
    const auto sign = static_cast<uint16_t>((bits & kFloatSign) >> kHalfSignShift);
    const uint32_t magnitude = bits & kFloatAbs;
    if (!std::isfinite(value)) {
        return static_cast<uint16_t>(sign | kHalfLargest); // (no infinities or NaNs kept: the largest)
    }
    const int exponent = static_cast<int>(magnitude >> kFloatExpShift) - kFloatBias + kHalfBias;
    if (exponent > kHalfMaxExp) {
        return static_cast<uint16_t>(sign | kHalfLargest);
    }
    if (exponent <= 0) {
        // a subnormal half (or 0): the mantissa with its leading 1, shifted down, rounded to nearest even
        if (exponent < -static_cast<int>(kHalfMantissaBits)) {
            return sign;
        }
        const uint32_t mantissa = (magnitude & kFloatMantissa) | (kFloatMantissa + 1);
        const auto shift = static_cast<uint32_t>(kMantissaDrop + 1 - exponent);
        uint32_t half = mantissa >> shift;
        const uint32_t rest = mantissa & ((1U << shift) - 1);
        const uint32_t halfway = 1U << (shift - 1);
        if (rest > halfway || (rest == halfway && (half & 1U) != 0)) {
            ++half;
        }
        return static_cast<uint16_t>(sign | half);
    }
    uint32_t half = (static_cast<uint32_t>(exponent) << kHalfMantissaBits) | ((magnitude & kFloatMantissa) >> kMantissaDrop);
    const uint32_t rest = magnitude & ((1U << kMantissaDrop) - 1);
    const uint32_t halfway = 1U << (kMantissaDrop - 1);
    if (rest > halfway || (rest == halfway && (half & 1U) != 0)) {
        ++half; // (a carry into the exponent is right; past the largest it is held)
    }
    return static_cast<uint16_t>(sign | std::min<uint32_t>(half, kHalfLargest));
}

float from_half(uint16_t half)
{
    const uint32_t sign = (static_cast<uint32_t>(half) & kHalfSign) << kHalfSignShift;
    const uint32_t exponent = (static_cast<uint32_t>(half) >> kHalfMantissaBits) & kHalfExpMask;
    const uint32_t mantissa = static_cast<uint32_t>(half) & kHalfMantissa;
    if (exponent == 0) {
        const float value = std::ldexp(static_cast<float>(mantissa), 1 - kHalfBias - static_cast<int>(kHalfMantissaBits));
        return sign != 0 ? -value : value;
    }
    const uint32_t bits = sign | ((exponent + static_cast<uint32_t>(kFloatBias - kHalfBias)) << kFloatExpShift) |
                          (mantissa << kMantissaDrop);
    return std::bit_cast<float>(bits);
}

uint64_t relit_level_content(std::span<const uint64_t> meshes)
{
    std::vector<uint64_t> sorted(meshes.begin(), meshes.end());
    std::ranges::sort(sorted);
    uint64_t hash = fnv_value(kFnvBasis, kRelitDataVersion);
    for (const uint64_t mesh : sorted) {
        hash = fnv_value(hash, mesh);
    }
    return hash;
}

std::string relit_data_name(uint32_t scene) { return text::format("%08x.relit", scene); }

uint64_t relit_mesh_content(const RelitMesh& mesh, std::span<const uint8_t> animated)
{
    uint64_t hash = fnv_value(kFnvBasis, static_cast<uint64_t>(mesh.positions.size()));
    for (std::size_t vi = 0; vi < mesh.positions.size(); ++vi) {
        hash = fnv_value(hash, core::at(mesh.positions, vi));
        const bool moves = vi < animated.size() && core::at(animated, vi) != 0;
        const uint32_t colour = vi < mesh.colours.size() && !moves ? core::at(mesh.colours, vi) : 0U;
        hash = fnv_value(hash, colour);
    }
    hash = fnv_value(hash, static_cast<uint64_t>(mesh.triangles.size()));
    for (const std::array<uint32_t, 3>& triangle : mesh.triangles) {
        hash = fnv_value(hash, triangle);
    }
    for (const RelitMesh::Source& source : mesh.sources) {
        hash = fnv_value(hash, source.count);
        for (std::size_t corner = 0; corner < source.count && corner < source.from.size(); ++corner) {
            hash = fnv_value(hash, core::at(source.from, corner));
            hash = fnv_value(hash, core::at(source.weight, corner));
        }
    }
    return hash;
}

std::string write_relit_data(const RelitLevelData& data)
{
    Writer out;
    out.put(kRelitDataMagic);
    out.put(kRelitDataVersion);
    out.put(data.scene);
    out.put(data.content);
    out.put(static_cast<uint32_t>(data.verdict));
    out.put(data.sky_rays);
    out.put(data.sun_rays);
    out.put(data.lamps);
    out.put(data.sun_margin);
    out.put(data.model_margin);
    out.put(data.facing);
    // the baked sun
    const BakedSun& sun = data.sun;
    out.put(static_cast<uint32_t>(sun.found ? 1 : 0));
    out.put3(sun.toward);
    out.put(sun.confidence);
    out.put(static_cast<uint32_t>(sun.ground_known ? 1 : 0));
    out.put(sun.ground);
    out.put(sun.columns);
    out.put(sun.rows);
    out.put(std::get<0>(sun.origin));
    out.put(std::get<1>(sun.origin));
    out.put(sun.cell);
    out.put(static_cast<uint32_t>(sun.agreement.size()));
    for (const float cell : sun.agreement) {
        out.put(cell);
    }
    // the model
    const RelitModel& model = data.model;
    out.put(static_cast<uint32_t>(model.valid ? 1 : 0));
    out.put3(model.toward);
    out.put3(model.sun);
    out.put3(model.sky);
    out.put3(model.bounce);
    out.put3(model.lamps);
    out.put(model.scale);
    out.put(model.error);
    out.put(static_cast<uint64_t>(model.vertices));
    out.put(model.sun_needed);
    // the meshes
    out.put(static_cast<uint32_t>(data.meshes.size()));
    for (const RelitDataMesh& mesh : data.meshes) {
        out.put(mesh.content);
        out.put(static_cast<uint32_t>(mesh.vertices.size()));
        for (const RelitVertex& vertex : mesh.vertices) {
            out.half(vertex.sun_baked);
            out.half(vertex.sky);
            out.half3(vertex.bounce);
            out.half3(vertex.bounce_local);
            out.half3(vertex.tint);
            out.half3(vertex.residual);
            out.half3(vertex.lamp);
            out.half(vertex.sun_missed);
        }
    }
    static_assert(kVertexHalves == 17);
    return std::move(out.text());
}

bool read_relit_data(std::string_view bytes, RelitLevelData* out, std::string* why)
{
    const auto fail = [why](const char* text) {
        if (why != nullptr) {
            *why = text;
        }
        return false;
    };
    Reader in(bytes);
    RelitLevelData data;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t verdict = 0;
    if (!in.get(&magic) || magic != kRelitDataMagic) {
        return fail("not a relit data file");
    }
    if (!in.get(&version) || version != kRelitDataVersion) {
        return fail("made by another version of the relit bake");
    }
    if (!in.get(&data.scene) || !in.get(&data.content) || !in.get(&verdict) || !in.get(&data.sky_rays) ||
        !in.get(&data.sun_rays) || !in.get(&data.lamps) || !in.get(&data.sun_margin) || !in.get(&data.model_margin) ||
        !in.get(&data.facing)) {
        return fail("cut short");
    }
    if (verdict > static_cast<uint32_t>(RelitVerdict::kNoModel)) {
        return fail("an unknown verdict");
    }
    data.verdict = static_cast<RelitVerdict>(verdict);
    BakedSun& sun = data.sun;
    uint32_t found = 0;
    uint32_t ground_known = 0;
    uint32_t cells = 0;
    if (!in.get(&found) || !in.get3(&sun.toward) || !in.get(&sun.confidence) || !in.get(&ground_known) ||
        !in.get(&sun.ground) || !in.get(&sun.columns) || !in.get(&sun.rows) || !in.get(&std::get<0>(sun.origin)) ||
        !in.get(&std::get<1>(sun.origin)) || !in.get(&sun.cell) || !in.get(&cells)) {
        return fail("cut short (the baked sun)");
    }
    if (cells > kMostCells || (cells != 0 && static_cast<uint64_t>(sun.columns) * sun.rows != cells)) {
        return fail("a broken agreement grid");
    }
    sun.found = found != 0;
    sun.ground_known = ground_known != 0;
    sun.agreement.resize(cells);
    for (float& cell : sun.agreement) {
        if (!in.get(&cell)) {
            return fail("cut short (the agreement grid)");
        }
    }
    RelitModel& model = data.model;
    uint32_t valid = 0;
    uint64_t vertices = 0;
    if (!in.get(&valid) || !in.get3(&model.toward) || !in.get3(&model.sun) || !in.get3(&model.sky) ||
        !in.get3(&model.bounce) || !in.get3(&model.lamps) || !in.get(&model.scale) || !in.get(&model.error) ||
        !in.get(&vertices) || !in.get(&model.sun_needed)) {
        return fail("cut short (the model)");
    }
    model.valid = valid != 0;
    model.vertices = static_cast<std::size_t>(vertices);
    uint32_t meshes = 0;
    if (!in.get(&meshes)) {
        return fail("cut short (the meshes)");
    }
    data.meshes.resize(meshes);
    for (RelitDataMesh& mesh : data.meshes) {
        uint32_t count = 0;
        if (!in.get(&mesh.content) || !in.get(&count) || count > kMostVertices) {
            return fail("cut short (a mesh)");
        }
        mesh.vertices.resize(count);
        for (RelitVertex& vertex : mesh.vertices) {
            if (!in.half(&vertex.sun_baked) || !in.half(&vertex.sky) || !in.half3(&vertex.bounce) ||
                !in.half3(&vertex.bounce_local) || !in.half3(&vertex.tint) || !in.half3(&vertex.residual) ||
                !in.half3(&vertex.lamp) || !in.half(&vertex.sun_missed)) {
                return fail("cut short (a mesh's vertices)");
            }
            vertex.sun = vertex.sun_baked;
        }
    }
    if (!in.done()) {
        return fail("bytes after its end");
    }
    *out = std::move(data);
    return true;
}

} // namespace render::rt
