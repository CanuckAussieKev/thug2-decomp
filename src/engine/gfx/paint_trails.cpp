// NOT ORIGINAL - paint trails: the strokes, the splatter, the session's fields (paint_trails.h).
#include "engine/gfx/paint_trails.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/game_hooks.h"
#include "engine/gfx/surface_trail.h"

namespace render::paint {

namespace {

using Vec3 = std::array<float, 3>;
using surface_trail::Point;
using surface_trail::smoothstep;

constexpr float kByteMax = 255.0F;
constexpr uint32_t kByte = 0xFFU;
constexpr uint32_t kGreenShift = 8;
constexpr uint32_t kBlueShift = 16;
constexpr uint32_t kAlphaShift = 24;
constexpr float kSoftEdge = 0.65F;     // (a line's edge softened over its outer 35%)
constexpr float kFringeTexels = 2.0F;  // (the place written this far past a coat's edge: the backend's nearest read)
constexpr float kStrokeCover = 0.92F;  // a line's coverage at its middle (a second pass deepens it)
constexpr float kLeastNormal = 1.0e-3F;
constexpr double kForgetSeconds = 2.0; // a skater not seen this long is forgotten (its line starts afresh)
constexpr uint64_t kSeed = 0x5041494E'54545241ULL; // (the session's: the same every run)

struct Memory {
    Vec3 last{};
    Contact contact = Contact::kNone;
    float drop_carry = 0.0F;
    double seen = 0.0;
};

struct State {
    State() { random.seed(kSeed); }
    std::mutex mutex;
    Settings settings;
    std::array<surface_trail::Field, kAxes> fields{surface_trail::Field(kTexel, kPlanes), surface_trail::Field(kTexel, kPlanes),
                                                  surface_trail::Field(kTexel, kPlanes)};
    surface_trail::Random random;
    std::map<uint64_t, Memory> memory; // (ordered: the same order every run)
    uint64_t level = 0;
    double clock = 0.0;
    Counts counts;
};

State& state()
{
    static State instance;
    return instance;
}

void clear_session(State& own)
{
    for (surface_trail::Field& field : own.fields) {
        field = surface_trail::Field(kTexel, kPlanes);
    }
    own.random.seed(kSeed);
    own.memory.clear();
    own.clock = 0.0;
    const uint64_t clears = own.counts.clears + 1;
    own.counts = {};
    own.counts.clears = clears;
}

float dot(const Vec3& a, const Vec3& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
}

Vec3 normalised(const Vec3& v, const Vec3& fallback)
{
    const float length = std::sqrt(dot(v, v));
    if (!(length > kLeastNormal) || !std::isfinite(length)) {
        return fallback;
    }
    return {core::at(v, 0) / length, core::at(v, 1) / length, core::at(v, 2) / length};
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(core::at(a, 1) * core::at(b, 2)) - (core::at(a, 2) * core::at(b, 1)),
            (core::at(a, 2) * core::at(b, 0)) - (core::at(a, 0) * core::at(b, 2)),
            (core::at(a, 0) * core::at(b, 1)) - (core::at(a, 1) * core::at(b, 0))};
}

Vec3 along(const Vec3& from, const Vec3& direction, float distance)
{
    return {core::at(from, 0) + (core::at(direction, 0) * distance), core::at(from, 1) + (core::at(direction, 1) * distance),
            core::at(from, 2) + (core::at(direction, 2) * distance)};
}

bool finite(const Vec3& v) { return std::isfinite(core::at(v, 0)) && std::isfinite(core::at(v, 1)) && std::isfinite(core::at(v, 2)); }

// The axes a point's (u, v) are along, for each field: x's - z, y; y's - x, z; z's - x, y.
constexpr std::array<std::array<std::size_t, 2>, kAxes> kPlaneAxes{{{2, 1}, {0, 2}, {0, 1}}};

// A coat: what a stroke or a drop puts down.
struct Coat {
    std::array<float, 3> colour{}; // 0 - 1
    float cover = 1.0F;            // at its middle
};

// 0xRRGGBB as red, green, blue (0 - 1)
std::array<float, 3> colour_of(uint32_t rgb)
{
    constexpr uint32_t kRedShift = 16;
    return {static_cast<float>((rgb >> kRedShift) & kByte) / kByteMax, static_cast<float>((rgb >> kGreenShift) & kByte) / kByteMax,
            static_cast<float>(rgb & kByte) / kByteMax};
}

uint32_t to_byte(float value) { return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByteMax)); }

// A texel coated: `cover` of `coat` over what is there, on the surface `depth` along the axis. A coat of another surface
// (further than kSameSurface along the axis) is kept unless this one covers more; a fringe (cover 0) only notes the place
// where nothing is painted (the backend reads the place nearest a pixel).
bool coat_texel(surface_trail::TexelRef ref, const Coat& coat, float cover, float depth)
{
    uint32_t& word = ref.word(kCoatPlane);
    uint32_t& place = ref.word(kDepthPlane);
    const uint32_t depth_bits = std::bit_cast<uint32_t>(depth);
    const float old_cover = static_cast<float>((word >> kAlphaShift) & kByte) / kByteMax;
    if (!(cover > 0.0F)) {
        if (word == 0 && place != depth_bits) {
            place = depth_bits;
            return true;
        }
        return false;
    }
    std::array<float, 3> premultiplied{static_cast<float>(word & kByte) / kByteMax, static_cast<float>((word >> kGreenShift) & kByte) / kByteMax,
                                       static_cast<float>((word >> kBlueShift) & kByte) / kByteMax};
    float under = old_cover;
    const float old_depth = std::bit_cast<float>(place);
    if (old_cover > 0.0F && !(std::abs(old_depth - depth) <= kSameSurface)) {
        if (cover < old_cover) {
            return false; // (another surface's coat, more of it)
        }
        under = 0.0F;
        premultiplied = {};
    }
    const float total = cover + (under * (1.0F - cover));
    std::array<float, 3> made{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        core::at(made, channel) = (core::at(coat.colour, channel) * cover) + (core::at(premultiplied, channel) * (1.0F - cover));
    }
    const uint32_t new_word = to_byte(core::at(made, 0)) | (to_byte(core::at(made, 1)) << kGreenShift) |
                              (to_byte(core::at(made, 2)) << kBlueShift) | (to_byte(total) << kAlphaShift);
    const uint32_t new_place = under == 0.0F || cover >= 0.5F ? depth_bits : place;
    if (new_word == word && new_place == place) {
        return false;
    }
    word = new_word;
    place = new_place;
    return true;
}

// A capsule of coat from `from` to `to` (world) of half width `half` on the surface through `to` facing `normal`, into
// every field the surface faces enough.
void paint_capsule(State& own, const Vec3& from, const Vec3& to, const Vec3& normal, float half, const Coat& coat)
{
    if (!(half > 0.0F) || !finite(from) || !finite(to)) {
        return;
    }
    for (std::size_t axis = 0; axis < kAxes; ++axis) {
        const float share = core::at(normal, axis);
        if (!(std::abs(share) >= kStampShare)) {
            continue;
        }
        const std::size_t u_axis = core::at(core::at(kPlaneAxes, axis), 0);
        const std::size_t v_axis = core::at(core::at(kPlaneAxes, axis), 1);
        const float nu = core::at(normal, u_axis);
        const float nv = core::at(normal, v_axis);
        const Point origin = plane_of(axis, to);
        const float origin_depth = depth_of(axis, to);
        const float fringe = kFringeTexels * kTexel;
        surface_trail::Field& field = core::at(own.fields, axis);
        field.stroke(plane_of(axis, from), origin, half + fringe, [&](float d, const Point& centre, surface_trail::TexelRef ref) {
            // the surface's place along the axis at the texel's centre (its plane through `to`)
            const float depth = origin_depth - (((nu * (core::at(centre, 0) - core::at(origin, 0))) +
                                                 (nv * (core::at(centre, 1) - core::at(origin, 1)))) / share);
            const float cover = d < half ? coat.cover * (1.0F - smoothstep(kSoftEdge * half, half, d)) : 0.0F;
            return coat_texel(ref, coat, cover, depth);
        });
    }
}

// The splatter: drops thrown to the sides and ahead on the surface, more and further with the combo's size; on a rail
// along its stripe.
void splatter(State& own, const Brush& brush, const Vec3& normal, const Vec3& forward, float half, float size, float scale,
              const Coat& coat, int count)
{
    const Vec3 side = normalised(cross(forward, normal), {1, 0, 0});
    for (int k = 0; k < count; ++k) {
        surface_trail::Random& random = own.random;
        const float sign = random.unit() < 0.5F ? -1.0F : 1.0F;
        float out = sign * random.between(0.35F, 1.0F) * (half + (kSideReach * size * scale));
        float ahead = random.between(-0.15F, 1.0F) * kAheadReach * size * scale;
        float radius = random.between(1.0F, 1.6F + (5.5F * size)) * std::sqrt(scale);
        if (brush.contact == Contact::kRail) {
            out = 0.0F;
            ahead *= 0.6F;
            radius = std::min(radius, half);
        }
        const Vec3 at = along(along(brush.at, side, out), forward, ahead);
        paint_capsule(own, at, at, normal, radius, coat);
        ++own.counts.drops;
        // a few specks round a drop
        const int specks = brush.contact == Contact::kRail ? 0 : static_cast<int>(random.unit() * 4.0F);
        for (int speck = 0; speck < specks; ++speck) {
            constexpr float kTau = 6.28318530718F;
            const float angle = random.unit() * kTau;
            const float reach = radius * random.between(1.4F, 2.8F);
            const Vec3 speck_at = along(along(at, side, std::cos(angle) * reach), forward, std::sin(angle) * reach);
            paint_capsule(own, speck_at, speck_at, normal, radius * random.between(0.25F, 0.45F), coat);
        }
    }
}

void paint_brush(State& own, const Brush& brush, Memory& memory, double seconds)
{
    const Settings& settings = own.settings;
    const Vec3 normal = normalised(brush.normal, {0, 1, 0});
    Vec3 forward = brush.forward;
    const float into = dot(forward, normal); // (the facing kept along the surface)
    forward = normalised(along(forward, normal, -into), {0, 0, 1});
    const uint32_t rgb = brush.own && settings.colour != 0 ? settings.colour : slot_colour(brush.slot);
    const float size = combo_size(brush.combo);
    const float scale = settings.scale;
    const bool rail = brush.contact == Contact::kRail;
    const float half = (rail ? kRailHalfWidth + (kRailComboHalfWidth * size) : kBoardHalfWidth + (kComboHalfWidth * size)) * scale;
    const Coat coat{.colour = colour_of(rgb), .cover = kStrokeCover};
    const auto step = static_cast<float>(seconds);
    if (brush.contact != Contact::kNone) {
        const bool goes_on = memory.contact != Contact::kNone;
        paint_capsule(own, goes_on ? memory.last : brush.at, brush.at, normal, half, coat);
        ++own.counts.strokes;
        if (size >= kLeastCombo) {
            memory.drop_carry += kDropsPerSecond * size * step;
            const int count = static_cast<int>(memory.drop_carry);
            memory.drop_carry -= static_cast<float>(count);
            splatter(own, brush, normal, forward, half, size, scale, coat, count);
        }
    }
    if (brush.landed > 0.0F) { // (a burst all round where a combo landed - a remote player's, as the game was sent it)
        const float landed = combo_size(brush.landed);
        const auto count = static_cast<int>(std::min(static_cast<float>(kMostLandingDrops), 10.0F + (70.0F * landed)));
        const Brush round{.id = brush.id, .slot = brush.slot, .own = brush.own, .contact = Contact::kGround, .at = brush.at};
        splatter(own, round, {0, 1, 0}, forward, kBoardHalfWidth * scale, landed, scale, coat, count);
    }
}

} // namespace

// --- the settings -----------------------------------------------------------------------------------------------------

void set_settings(const Settings& settings)
{
    Settings clean = settings;
    clean.scale = std::isfinite(settings.scale) ? std::clamp(settings.scale, kLeastScale, kMostScale) : 1.0F;
    clean.colour = settings.colour & 0xFFFFFFU;
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    own.settings = clean;
    if (!clean.on) {
        clear_session(own);
    }
}

Settings settings()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.settings;
}

uint32_t slot_colour(int slot)
{
    constexpr std::array<uint32_t, kPaletteSize> kPalette{0xFF2E88U, 0x1EC8FFU, 0x7CFF2EU, 0xFF8A1EU,
                                                          0xFFE81EU, 0x9B4DFFU, 0xFF3030U, 0xF2F2F2U};
    const auto count = static_cast<int>(kPaletteSize);
    return core::at(kPalette, static_cast<std::size_t>(((slot % count) + count) % count));
}

uint32_t colour_from_text(const char* text)
{
    if (text == nullptr) {
        return 0;
    }
    std::string_view value(text);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    constexpr std::array<std::string_view, kPaletteSize> kNames{"pink", "cyan", "lime", "orange", "yellow", "violet", "red", "white"};
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (value == core::at(kNames, i)) {
            return slot_colour(static_cast<int>(i));
        }
    }
    if (!value.empty() && value.front() == '#') {
        value.remove_prefix(1);
    }
    constexpr std::size_t kHexDigits = 6;
    if (value.size() != kHexDigits) {
        return 0;
    }
    uint32_t rgb = 0;
    for (const char digit : value) {
        constexpr uint32_t kDigitBits = 4;
        constexpr uint32_t kTen = 10;
        uint32_t nibble = 0;
        if (digit >= '0' && digit <= '9') {
            nibble = static_cast<uint32_t>(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = static_cast<uint32_t>(digit - 'a') + kTen;
        } else if (digit >= 'A' && digit <= 'F') {
            nibble = static_cast<uint32_t>(digit - 'A') + kTen;
        } else {
            return 0;
        }
        rgb = (rgb << kDigitBits) | nibble;
    }
    return rgb == 0 ? 1U : rgb; // (pure black: as near as the setting holds)
}

float combo_size(float score)
{
    if (!(score > 0.0F)) {
        return 0.0F;
    }
    return std::clamp(std::log10(1.0F + (score / kComboUnit)) / std::log10(1.0F + (kFullCombo / kComboUnit)), 0.0F, 1.0F);
}

Point plane_of(std::size_t axis, const std::array<float, 3>& point)
{
    const std::array<std::size_t, 2>& axes = core::at(kPlaneAxes, axis);
    return {core::at(point, core::at(axes, 0)), core::at(point, core::at(axes, 1))};
}

float depth_of(std::size_t axis, const std::array<float, 3>& point) { return core::at(point, axis); }

// --- the state --------------------------------------------------------------------------------------------------------

void step(std::span<const Brush> brushes, double seconds, uint64_t level)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    if (!own.settings.on) {
        return;
    }
    if (level != own.level) {
        own.level = level;
        clear_session(own);
    }
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return;
    }
    own.clock += seconds;
    ++own.counts.steps;
    // (in the skaters' order, whatever order the game listed them in: the splatter's random numbers taken the same way)
    std::vector<const Brush*> ordered;
    ordered.reserve(brushes.size());
    for (const Brush& brush : brushes) {
        ordered.push_back(&brush);
    }
    std::ranges::stable_sort(ordered, {}, [](const Brush* brush) { return brush->id; });
    for (const Brush* brush : ordered) {
        if (!finite(brush->at)) {
            continue;
        }
        auto [found, made] = own.memory.try_emplace(brush->id);
        Memory& memory = found->second;
        const Vec3 moved{core::at(brush->at, 0) - core::at(memory.last, 0), core::at(brush->at, 1) - core::at(memory.last, 1),
                         core::at(brush->at, 2) - core::at(memory.last, 2)};
        if (made || dot(moved, moved) > kTeleport * kTeleport) {
            memory = Memory{}; // (a new line)
        }
        paint_brush(own, *brush, memory, seconds);
        memory.last = brush->at;
        memory.contact = brush->contact;
        memory.seen = own.clock;
    }
    std::erase_if(own.memory, [&own](const auto& entry) { return own.clock - entry.second.seen > kForgetSeconds; });
}

void step_from_game(double seconds, uint64_t level)
{
    if (!settings().on) {
        return;
    }
    std::vector<Brush> brushes;
    if (const auto gather = game_hooks().paint_brushes; gather != nullptr) {
        gather(&brushes);
    }
    step(brushes, seconds, level);
}

void read_fields(const std::function<void(std::span<const surface_trail::Field* const, kAxes> fields)>& reader)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    const std::array<const surface_trail::Field*, kAxes> fields{&core::at(own.fields, 0), &core::at(own.fields, 1),
                                                                &core::at(own.fields, 2)};
    reader(fields);
}

Counts counts()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.counts;
}

void clear()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    clear_session(own);
}

} // namespace render::paint
