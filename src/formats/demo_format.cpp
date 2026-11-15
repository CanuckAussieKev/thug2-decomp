// The demo files (formats/demo_format.h). NOT ORIGINAL.

#include "formats/demo_format.h"

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
#include "base/core/raw_view.h"
#include "formats/byte_stream.h"

namespace demo_format {

namespace {

constexpr std::size_t kTagBytes = 4;
constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kWordBits = 32;
constexpr std::size_t kChunkHeaderBytes = 8;
constexpr uint32_t kMaxFrames = 60U * 60U * 60U; // an hour at 60 frames a second
constexpr uint32_t kMaxStrings = 4096;

// A chunk's tag as the u32 it is read as.
constexpr uint32_t tag(std::string_view text)
{
    uint32_t value = 0;
    for (std::size_t i = 0; i < kTagBytes; ++i) {
        value |= static_cast<uint32_t>(static_cast<uint8_t>(text.at(i))) << (kBitsPerByte * i);
    }
    return value;
}
constexpr uint32_t kInfo = tag("INFO");
constexpr uint32_t kSettings = tag("SETS");
constexpr uint32_t kProfile = tag("PROF");
constexpr uint32_t kDetails = tag("DETL");
constexpr uint32_t kInput = tag("INPT");
constexpr uint32_t kChecks = tag("CHEK");
constexpr uint32_t kClocks = tag("CLCK");
constexpr uint32_t kRandom = tag("RAND");
constexpr uint32_t kGlobals = tag("GLOB");
constexpr uint32_t kRate = tag("RATE");
constexpr uint32_t kOptions = tag("OPTS");
constexpr uint32_t kSeal = tag("SEAL");
constexpr std::size_t kGlobalBytes = 9; // name, kind, word
constexpr std::string_view kSealLabel = "THUG2DEM seal 1";
constexpr uint32_t kMaxLinks = (kMaxFrames / kSealStride) + 1;

// A growing little-endian writer.
class Out {
public:
    void u32(uint32_t value)
    {
        for (unsigned shift = 0; shift < kWordBits; shift += kBitsPerByte) {
            bytes_.push_back(static_cast<uint8_t>(value >> shift));
        }
    }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void u8(uint8_t value) { bytes_.push_back(value); }
    void f32(float value) { u32(std::bit_cast<uint32_t>(value)); }
    void put(std::span<const uint8_t> data) { bytes_.insert(bytes_.end(), data.begin(), data.end()); }
    void text(const std::string& value)
    {
        u32(static_cast<uint32_t>(value.size()));
        put(std::span(raw::bytes(value.data()), value.size()));
    }
    void vec(const std::array<float, 3>& value)
    {
        for (const float v : value) {
            f32(v);
        }
    }
    // A chunk: its tag, its size, its body.
    void chunk(uint32_t id, const Out& body)
    {
        u32(id);
        u32(static_cast<uint32_t>(body.bytes_.size()));
        put(body.bytes_);
    }
    std::vector<uint8_t>& bytes() { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

std::string read_text(util::ByteReader& in)
{
    const uint32_t length = in.u32();
    if (!in.ok() || length > in.remaining()) {
        in.skip(in.remaining() + 1); // (fails the reader)
        return {};
    }
    std::string value(length, '\0');
    in.bytes(value.data(), length);
    return value;
}

std::array<float, 3> read_vec(util::ByteReader& in)
{
    std::array<float, 3> value{};
    for (float& v : value) {
        v = in.f32();
    }
    return value;
}

void write_point(Out& out, const CheckPoint& point)
{
    out.u32(point.frame);
    out.u32(point.flags);
    out.vec(point.position);
    out.vec(point.velocity);
    out.vec(point.at);
    out.vec(point.up);
    out.i32(point.state);
    out.vec(point.camera);
    out.vec(point.camera_forward);
}

CheckPoint read_point(util::ByteReader& in)
{
    CheckPoint point;
    point.frame = in.u32();
    point.flags = in.u32();
    point.position = read_vec(in);
    point.velocity = read_vec(in);
    point.at = read_vec(in);
    point.up = read_vec(in);
    point.state = in.i32();
    point.camera = read_vec(in);
    point.camera_forward = read_vec(in);
    return point;
}

bool read_info(util::ByteReader& in, Demo* demo)
{
    demo->level = read_text(in);
    demo->skater = read_text(in);
    demo->created = read_text(in);
    demo->build = read_text(in);
    demo->level_load = in.u32();
    demo->skater_name = in.u32();
    demo->seed = in.u32();
    demo->crt_seed = in.u32();
    demo->up_frame = in.u32();
    in.u32(); // (frames: the input chunk's count is the one used)
    demo->ports = in.u32();
    demo->check_stride = in.u32();
    for (uint32_t& word : demo->random_at_up.streams) {
        word = in.u32();
    }
    demo->random_at_up.crt = in.u32();
    return in.ok();
}

bool read_input(util::ByteReader& in, Demo* demo, std::string* error)
{
    const uint32_t ports = in.u32();
    const uint32_t frames = in.u32();
    if (!in.ok() || ports == 0 || ports > kMaxPorts || frames > kMaxFrames) {
        *error = "bad input chunk (ports / frames)";
        return false;
    }
    demo->ports = ports;
    demo->inputs.assign(static_cast<std::size_t>(frames) * ports, PortData{});
    for (uint32_t port = 0; port < ports; ++port) {
        uint32_t frame = 0;
        while (frame < frames) {
            const uint32_t count = in.u32();
            PortData data{};
            in.bytes(data.data(), data.size());
            if (!in.ok() || count == 0 || count > frames - frame) {
                *error = "bad input run";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i, ++frame) {
                core::at(demo->inputs, (static_cast<std::size_t>(frame) * ports) + port) = data;
            }
        }
    }
    return true;
}

float distance(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    float sum = 0.0F;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float difference = core::at(a, i) - core::at(b, i);
        sum += difference * difference;
    }
    return std::sqrt(sum);
}

// --- the seal (demo_format.h, SEAL) ---

sha256::Hasher start_hasher(uint32_t version)
{
    sha256::Hasher hasher;
    hasher.add(std::span(raw::bytes(kSealLabel.data()), kSealLabel.size()));
    hasher.add_u32(version);
    return hasher;
}

// A chunk before the seal into its start (the input and the check points are the links').
void add_to_start(sha256::Hasher* start, uint32_t id, std::span<const uint8_t> body)
{
    if (id == kInput || id == kChecks || id == kSeal) {
        return;
    }
    start->add_u32(id);
    start->add_u32(static_cast<uint32_t>(body.size()));
    start->add(body);
}

struct Chain {
    std::vector<sha256::Digest> links;
    sha256::Digest end{};
};

Chain make_chain(const sha256::Digest& start, const Demo& demo, uint32_t stride)
{
    Chain chain;
    sha256::Digest link = start;
    const uint32_t frames = demo.frames();
    std::size_t next_check = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        sha256::Hasher hasher;
        hasher.add(link);
        hasher.add_u32(frame);
        for (uint32_t port = 0; port < demo.ports; ++port) {
            hasher.add(*demo.input(frame, port));
        }
        while (next_check < demo.checks.size() && core::at(demo.checks, next_check).frame == frame) {
            Out point;
            write_point(point, core::at(demo.checks, next_check));
            hasher.add(point.bytes());
            ++next_check;
        }
        link = hasher.finish();
        if ((frame + 1) % stride == 0 || frame + 1 == frames) {
            chain.links.push_back(link);
        }
    }
    sha256::Hasher end;
    end.add(link);
    end.add_u32(static_cast<uint32_t>(demo.checks.size() - next_check));
    for (; next_check < demo.checks.size(); ++next_check) {
        Out point;
        write_point(point, core::at(demo.checks, next_check));
        end.add(point.bytes());
    }
    chain.end = end.finish();
    return chain;
}

sha256::Digest read_digest(util::ByteReader& in)
{
    sha256::Digest digest{};
    in.bytes(digest.data(), digest.size());
    return digest;
}

// The seal checked against the file: `start` made from the chunks before it, the demo read.
void check_seal(std::span<const uint8_t> body, const sha256::Digest& start, Demo* demo)
{
    Seal& seal = demo->seal;
    seal.state = SealState::kChanged;
    util::ByteReader in(body);
    const uint32_t version = in.u32();
    seal.profile_hash = read_digest(in);
    const sha256::Digest stored_start = read_digest(in);
    const uint32_t stride = in.u32();
    const uint32_t count = in.u32();
    if (!in.ok() || version != kSealVersion || stride == 0 || count > kMaxLinks) {
        seal.end_changed = true;
        return;
    }
    std::vector<sha256::Digest> links(count);
    for (sha256::Digest& link : links) {
        link = read_digest(in);
    }
    const sha256::Digest stored_end = read_digest(in);
    if (!in.ok()) {
        seal.end_changed = true;
        return;
    }
    seal.profile_changed = profile_hash(*demo) != seal.profile_hash;
    seal.header_changed = stored_start != start;
    const Chain chain = make_chain(stored_start, *demo, stride);
    if (chain.links.size() != links.size()) {
        seal.changed_from = 0;
        seal.changed_to = demo->frames();
    } else {
        for (std::size_t i = 0; i < links.size(); ++i) {
            if (core::at(links, i) != core::at(chain.links, i)) {
                seal.changed_from = static_cast<int64_t>(i) * stride;
                seal.changed_to = std::min<int64_t>((static_cast<int64_t>(i) + 1) * stride, demo->frames());
                break;
            }
        }
    }
    seal.end_changed = seal.changed_from < 0 && chain.end != stored_end;
    if (!seal.profile_changed && !seal.header_changed && seal.changed_from < 0 && !seal.end_changed) {
        seal.state = SealState::kIntact;
    }
}

} // namespace

sha256::Digest profile_hash(const Demo& demo)
{
    sha256::Hasher hasher;
    hasher.add_u32(kProfile);
    hasher.add_u32(static_cast<uint32_t>(demo.profile.size()));
    hasher.add(demo.profile);
    hasher.add_u32(kDetails);
    hasher.add_u32(static_cast<uint32_t>(demo.details.size()));
    hasher.add(demo.details);
    return hasher.finish();
}

std::string seal_text(const Seal& seal)
{
    if (seal.state == SealState::kNone) {
        return "none (recorded before replays were sealed)";
    }
    if (seal.state == SealState::kIntact) {
        return "intact";
    }
    std::string what;
    const auto add = [&what](const std::string& part) { what += (what.empty() ? "" : ", ") + part; };
    if (seal.header_changed) {
        add("its header (level, skater, date, build, settings)");
    }
    if (seal.profile_changed) {
        add("the skater");
    }
    if (seal.changed_from >= 0) {
        add(text::format("the input or check points in frames %lld - %lld", static_cast<long long>(seal.changed_from),
                         static_cast<long long>(seal.changed_to - 1)));
    }
    if (seal.end_changed) {
        add("the seal itself or the last check points");
    }
    return "changed after recording: " + what;
}

const PortData* Demo::input(uint32_t frame, uint32_t port) const
{
    const std::size_t index = (static_cast<std::size_t>(frame) * ports) + port;
    if (port >= ports || index >= inputs.size()) {
        return nullptr;
    }
    return &core::at(inputs, index);
}

const CheckPoint* Demo::check_at(uint32_t frame) const
{
    const auto found = std::ranges::lower_bound(checks, frame, {}, &CheckPoint::frame);
    return found != checks.end() && found->frame == frame ? &*found : nullptr;
}

std::string Demo::setting(const std::string& key) const
{
    for (const auto& [name, value] : settings) {
        if (name == key) {
            return value;
        }
    }
    return {};
}

std::vector<uint8_t> write_demo(const Demo& demo)
{
    Out file;
    file.put(std::span(raw::bytes(kMagic.data()), kMagic.size()));
    file.u32(kVersion);
    // (every chunk into the file and into the seal's start: the SEAL chunk goes last)
    sha256::Hasher start = start_hasher(kVersion);
    const auto put = [&file, &start](uint32_t id, Out& body) {
        file.chunk(id, body);
        add_to_start(&start, id, body.bytes());
    };

    Out info;
    info.text(demo.level);
    info.text(demo.skater);
    info.text(demo.created);
    info.text(demo.build);
    info.u32(demo.level_load);
    info.u32(demo.skater_name);
    info.u32(demo.seed);
    info.u32(demo.crt_seed);
    info.u32(demo.up_frame);
    info.u32(demo.frames());
    info.u32(demo.ports);
    info.u32(demo.check_stride);
    for (const uint32_t word : demo.random_at_up.streams) {
        info.u32(word);
    }
    info.u32(demo.random_at_up.crt);
    put(kInfo, info);

    Out settings;
    settings.u32(static_cast<uint32_t>(demo.settings.size()));
    for (const auto& [key, value] : demo.settings) {
        settings.text(key);
        settings.text(value);
    }
    put(kSettings, settings);

    if (!demo.profile.empty()) {
        Out profile;
        profile.put(demo.profile);
        put(kProfile, profile);
    }
    if (!demo.details.empty()) {
        Out details;
        details.put(demo.details);
        put(kDetails, details);
    }

    Out input;
    const uint32_t frames = demo.frames();
    input.u32(demo.ports);
    input.u32(frames);
    for (uint32_t port = 0; port < demo.ports; ++port) {
        uint32_t frame = 0;
        while (frame < frames) {
            const PortData& data = *demo.input(frame, port);
            uint32_t count = 1;
            while (frame + count < frames && *demo.input(frame + count, port) == data) {
                ++count;
            }
            input.u32(count);
            input.put(data);
            frame += count;
        }
    }
    put(kInput, input);

    Out checks;
    checks.u32(static_cast<uint32_t>(demo.checks.size()));
    for (const CheckPoint& point : demo.checks) {
        write_point(checks, point);
    }
    put(kChecks, checks);

    if (demo.clocks.present) {
        Out clocks;
        clocks.u32(static_cast<uint32_t>(demo.clocks.vblanks));
        clocks.u32(static_cast<uint32_t>(demo.clocks.vblanks >> kWordBits));
        clocks.u32(demo.clocks.last_change);
        clocks.f32(demo.clocks.pace);
        clocks.i32(demo.clocks.offset);
        put(kClocks, clocks);
    }
    if (demo.random_flags != 0) {
        Out random;
        random.u32(demo.random_flags);
        put(kRandom, random);
    }
    if (!demo.globals.empty()) {
        Out globals;
        globals.u32(static_cast<uint32_t>(demo.globals.size()));
        for (const Global& global : demo.globals) {
            globals.u32(global.name);
            globals.u8(global.kind);
            globals.u32(global.word);
        }
        put(kGlobals, globals);
    }
    if (demo.options.present) {
        Out options;
        options.u8(demo.options.auto_kick ? 1 : 0);
        options.u8(demo.options.spin_taps ? 1 : 0);
        options.f32(demo.options.stat_override);
        put(kOptions, options);
    }

    const sha256::Digest begun = start.finish();
    const Chain chain = make_chain(begun, demo, kSealStride);
    Out seal;
    seal.u32(kSealVersion);
    seal.put(profile_hash(demo));
    seal.put(begun);
    seal.u32(kSealStride);
    seal.u32(static_cast<uint32_t>(chain.links.size()));
    for (const sha256::Digest& link : chain.links) {
        seal.put(link);
    }
    seal.put(chain.end);
    file.chunk(kSeal, seal);
    return std::move(file.bytes());
}

namespace {
// The chunks of what the recording's session had (CLCK, GLOB, RAND); false (and why) for a broken one.
// A version 2 demo's RATE chunk: the ticks a second it was recorded at - only the game's own 60 plays.
bool rate_is_ours(util::ByteReader& body, std::string* why)
{
    const uint32_t rate = body.u32();
    if (!body.ok()) {
        *why = "bad rate chunk";
        return false;
    }
    if (rate != kTickRate) {
        *why = text::format("recorded at %u ticks a second - the game simulates at %u", rate, kTickRate);
        return false;
    }
    return true;
}

bool read_session_chunk(uint32_t id, util::ByteReader& body, Demo* demo, std::string* why)
{
    if (id == kClocks) {
        const uint32_t low = body.u32();
        const uint32_t high = body.u32();
        demo->clocks.vblanks = static_cast<uint64_t>(high) << kWordBits | low;
        demo->clocks.last_change = body.u32();
        demo->clocks.pace = body.f32();
        demo->clocks.offset = body.i32();
        if (!body.ok()) {
            *why = "bad clock chunk";
            return false;
        }
        demo->clocks.present = true;
        return true;
    }
    if (id == kRandom) {
        demo->random_flags = body.u32();
        if (!body.ok()) {
            *why = "bad random chunk";
            return false;
        }
        return true;
    }
    const uint32_t count = body.u32();
    if (!body.ok() || count > body.remaining() / kGlobalBytes) {
        *why = "bad globals chunk";
        return false;
    }
    demo->globals.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Global global;
        global.name = body.u32();
        global.kind = body.u8();
        global.word = body.u32();
        demo->globals.push_back(global);
    }
    return true;
}
} // namespace

bool read_demo(std::span<const uint8_t> bytes, Demo* out, std::string* error)
{
    std::string ignored;
    std::string& why = error ? *error : ignored;
    util::ByteReader in(bytes);
    std::array<char, kMagic.size()> magic{};
    in.bytes(magic.data(), magic.size());
    if (!in.ok() || magic != kMagic) {
        why = "not a demo file";
        return false;
    }
    Demo demo;
    demo.version = in.u32();
    if (!in.ok() || demo.version == 0) {
        why = "not a demo file";
        return false;
    }
    if (demo.version > kNewestVersion) {
        why = text::format("a newer demo (version %u; this game reads up to %u)", demo.version, kNewestVersion);
        return false;
    }
    bool has_info = false;
    bool has_input = false;
    // (the seal's start: the chunks before the first SEAL; the chunks after it - a later version's - aren't sealed)
    sha256::Hasher start = start_hasher(demo.version);
    std::span<const uint8_t> seal_body;
    bool sealed = false;
    sha256::Digest begun{};
    while (in.remaining() > 0) {
        const uint32_t id = in.u32();
        const uint32_t size = in.u32();
        if (!in.ok() || size > in.remaining()) {
            why = "a chunk runs past the end";
            return false;
        }
        util::ByteReader body(in.rest().first(size));
        in.skip(size);
        if (!sealed) {
            if (id == kSeal) {
                sealed = true;
                seal_body = body.rest();
                begun = start.finish();
            } else {
                add_to_start(&start, id, body.rest());
            }
        }
        if (id == kOptions) {
            demo.options.auto_kick = body.u8() != 0;
            demo.options.spin_taps = body.u8() != 0;
            demo.options.stat_override = body.f32();
            if (!body.ok()) {
                why = "bad options chunk";
                return false;
            }
            demo.options.present = true;
        } else if (id == kInfo) {
            if (!read_info(body, &demo)) {
                why = "bad info chunk";
                return false;
            }
            has_info = true;
        } else if (id == kSettings) {
            const uint32_t count = body.u32();
            if (count > kMaxStrings) {
                why = "bad settings chunk";
                return false;
            }
            for (uint32_t i = 0; i < count && body.ok(); ++i) {
                std::string key = read_text(body);
                std::string value = read_text(body);
                demo.settings.emplace_back(std::move(key), std::move(value));
            }
            if (!body.ok()) {
                why = "bad settings chunk";
                return false;
            }
        } else if (id == kProfile) {
            demo.profile.assign(body.rest().begin(), body.rest().end());
        } else if (id == kDetails) {
            demo.details.assign(body.rest().begin(), body.rest().end());
        } else if (id == kInput) {
            if (!read_input(body, &demo, &why)) {
                return false;
            }
            has_input = true;
        } else if (id == kClocks || id == kGlobals || id == kRandom) {
            if (!read_session_chunk(id, body, &demo, &why)) {
                return false;
            }
        } else if (id == kRate) {
            if (!rate_is_ours(body, &why)) {
                return false;
            }
        } else if (id == kChecks) {
            const uint32_t count = body.u32();
            if (!body.ok() || count > body.remaining() / kCheckPointBytes) {
                why = "bad check chunk";
                return false;
            }
            demo.checks.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                demo.checks.push_back(read_point(body));
            }
        }
        // (any other chunk: a later version's - skipped)
    }
    if (!has_info || !has_input) {
        why = has_info ? "no input chunk" : "no info chunk";
        return false;
    }
    if (sealed) {
        check_seal(seal_body, begun, &demo);
    }
    if (out) {
        *out = std::move(demo);
    }
    return true;
}

std::string check_line(const CheckPoint& point)
{
    // (positions and velocities to 3 places, unit vectors to 4)
    const auto add = [](std::string* line, const std::array<float, 3>& value, bool unit) {
        for (const float v : value) {
            *line +=
                unit ? text::format(" %.4f", static_cast<double>(v)) : text::format(" %.3f", static_cast<double>(v));
        }
    };
    std::string line = text::format("%u %u", point.frame, point.flags);
    add(&line, point.position, false);
    add(&line, point.velocity, false);
    add(&line, point.at, true);
    add(&line, point.up, true);
    line += text::format(" %d", point.state);
    add(&line, point.camera, false);
    add(&line, point.camera_forward, true);
    return line;
}

Difference compare_points(const CheckPoint& a, const CheckPoint& b, float tolerance)
{
    Difference difference;
    difference.skater = distance(a.position, b.position);
    // (the camera only where both runs drew the frame: one left undrawn - a hitch, a device reset - has none, which
    // says nothing about the run)
    const bool cameras = (a.flags & b.flags & kHasCamera) != 0U;
    difference.camera = cameras ? distance(a.camera, b.camera) : 0.0F;
    difference.other = ((a.flags ^ b.flags) & ~kHasCamera) != 0U || a.state != b.state ||
                       distance(a.velocity, b.velocity) > tolerance || distance(a.at, b.at) > tolerance ||
                       distance(a.up, b.up) > tolerance ||
                       (cameras && distance(a.camera_forward, b.camera_forward) > tolerance);
    return difference;
}

void add_to_drift(Drift* drift, const CheckPoint& recorded, const CheckPoint& played, float tolerance)
{
    const Difference difference = compare_points(recorded, played, tolerance);
    ++drift->compared;
    drift->max_skater = std::max(drift->max_skater, difference.skater);
    drift->max_camera = std::max(drift->max_camera, difference.camera);
    if (drift->first_frame < 0 &&
        (difference.skater > tolerance || difference.camera > tolerance || difference.other)) {
        drift->first_frame = played.frame;
        drift->first = difference;
    }
}

} // namespace demo_format
