// NOT ORIGINAL - thug2_ff_shader_check.exe: the Vulkan backend's fixed-function emulation checked offline
// (gpu_vulkan/ff_key.h, ff_shaders.h, glsl_compiler.h). It lists every shader key the Direct3D 9 backend's draws can
// make (ff_shader_check_model.h), generates each key's GLSL, compiles it with glslang, validates the SPIR-V with
// SPIRV-Tools' validator (when the build has it: THUG2_SPIRV_VALIDATOR), and - unless --no-gpu - hands every module and
// a pipeline for each sampled pair of shaders to a Vulkan driver (ff_shader_check_gpu.h).
//   thug2_ff_shader_check [--all-materials] [--no-gpu] [--device N] [--pipelines N] [--jobs N] [--dump FOLDER] [--surface] [--motion] [--cel]
//                         [--retro] [--detail] [--water] [--characters] [--trails] [--light]
// --light (NOT ORIGINAL - round V, gpu_vulkan/vulkan_surface_light.h): every pair again as a prepass's draw (FragmentKey::
// light kLightDepthOnly, and kLightDepthHalf - a blended one's; round SKIN: the shaded keys with kLightDepthHalf too, a
// blended draw that writes depth kept where its prepass kept it), every pair not lit per pixel with the light inputs as
// shares (kLightShare) - and as shares reading the front layer with their depth in it where they cover (kLightFront: round
// FRONT's particles) -, and with --surface every pair lit per pixel with the light inputs (kLightInputs) - after
// --surface's, --trails' and --water's, before --motion's.
// --detail (NOT ORIGINAL - rt_detail / rt_delight, gpu_vulkan/vulkan_detail.h): with --surface, every pair lit per pixel
// with a surface map added again with each of the detail layer's bit sets (ff_key.h kDetail*; the relief's - rt_relief -
// alone, with the layer and with both) - after --surface's.
// --characters (added for the characters - rt_characters, gpu_vulkan/vulkan_surface.h "Characters"): with --surface,
// every pair lit by lights with a surface map added again with the characters' bits (skin, cloth, both, hair) - after
// --detail's (its keys and their order unchanged).
// --cel (added by cel shading): every pair that takes the bands added again with them (engine/gfx/rt_cel.h; add_cel_keys).
// --surface (NOT ORIGINAL - rt_pbr): every pair that can be lit per pixel (ff_key.h kSurface*) added again with each
// combination of the per-pixel lighting's bits (the map, lit by lights) - compiled, validated and made into pipelines
// with the rest.
// --motion (the upscaler's): every pair that isn't pre-transformed added again with its motion vectors (ff_key.h
// VertexKey::motion) - after --surface's, so the per-pixel lighting and the motion output meet in one shader. Compiled,
// validated and handed to the driver as modules; not made into this check's pipelines (they have no motion attachment
// or previous-position stream: thug2_vulkan_upscale_check draws with them).
// --retro (NOT ORIGINAL - the retro art style, gpu_vulkan/vulkan_style_film.h): every pair that isn't pre-transformed
// added again with its vertex half's retro bit (ff_key.h VertexKey::retro: snapped, affine) - after --motion's, so
// they meet too. Compiled, validated and made into pipelines with the rest (the retro variant needs nothing else).
// --water (NOT ORIGINAL - rt_water, gpu_vulkan/vulkan_water.h): every pair that can be shaded as water added again
// flat (FragmentKey::water kWaterFlat) and moving (kWaterMoving: its vertex half moves the surface - rt_water_geometry) -
// after --surface's and --cel's, before --motion's, so they all meet. Compiled, validated and made into pipelines.
// --trails (NOT ORIGINAL - the surface trails, ff_key.h FragmentKey::trails: the snow's cover, the paint's coat): every
// pair that takes them added again with the snow (alone, traced - its ray query -, over the relit tint - the stream's
// inputs), the paint, the snow and the paint, and all of them - after --water's (water never takes them), before
// --motion's. Compiled, validated and made into pipelines.
// --all-materials: every four-pass material shape, not a covering subset; --device: the physical device for the GPU
// part (the context check's numbering); --pipelines: at most N pipelines (default: every sampled pair); --jobs: the
// compiling threads (default 8); --batch: the fragment shaders compiled, handed to the driver (modules and pipelines) and
// freed together (default 16384; what the process holds stays bounded however many keys the sweep has - the peak commit
// is printed at the end); --dump: every generated shader's GLSL written into FOLDER (vNNNN.vert, fNNNN.frag).
// Exit code: 0 when everything compiled, validated and (with the GPU part) was accepted; 1 otherwise; the GPU part
// alone missing (no Vulkan) leaves it 0 and says so.
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tests/no_error_dialogs.h"
#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_shaders.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "tests/ff_shader_check_gpu.h"
#include "tests/ff_shader_check_model.h"

#ifdef THUG2_SPIRV_VALIDATOR
#include <spirv-tools/libspirv.h>
#include <spirv-tools/libspirv.hpp>
#endif

namespace {

using render::vulkan::FragmentKey;
using render::vulkan::ShaderKey;
using render::vulkan::ShaderStage;
using render::vulkan::VertexKey;

constexpr int kDefaultJobs = 8;
// A batch of fragment shaders holds their sources and SPIR-V (~30 KB each) and the driver's copy of their modules and
// pipelines (~150 KB each): 16384 keeps the process under ~12 GB of commit whatever the sweep.
constexpr int kDefaultBatch = 16384;
constexpr std::size_t kReportedFailures = 5;
constexpr int kBitsPerByte = 8;
constexpr int kProgramDigit = 10;
constexpr std::size_t kBytesPerKilobyte = 1024;

struct Options {
    bool all_materials = false;
    bool gpu = true;
    int device = -1;
    int pipelines = -1;
    int jobs = kDefaultJobs;
    int batch = kDefaultBatch; // (the fragment shaders compiled, handed to the driver and freed together)
    std::string dump;
    bool surface = false;
    bool detail = false; // (NOT ORIGINAL - rt_detail / rt_delight)
    bool motion = false;
    bool cel = false; // (added by cel shading)
    bool retro = false; // (the retro art style)
    bool water = false; // (rt_water)
    bool trails = false; // (the surface trails: snow, paint)
    bool light = false;  // (round V: the light inputs and the prepass)
    bool characters = false; // (added for the characters)
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--surface") {
            options.surface = true;
        } else if (argument == "--cel") {
            options.cel = true;
        } else if (argument == "--motion") {
            options.motion = true;
        } else if (argument == "--detail") {
            options.detail = true;
        } else if (argument == "--retro") {
            options.retro = true;
        } else if (argument == "--water") {
            options.water = true;
        } else if (argument == "--trails") {
            options.trails = true;
        } else if (argument == "--light") {
            options.light = true;
        } else if (argument == "--characters") {
            options.characters = true;
        } else if (argument == "--all-materials") {
            options.all_materials = true;
        } else if (argument == "--no-gpu") {
            options.gpu = false;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--pipelines" && has_value) {
            options.pipelines = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--jobs" && has_value) {
            options.jobs = std::max(1, text::to_int(core::at(arguments, ++i)));
        } else if (argument == "--batch" && has_value) {
            options.batch = std::max(1, text::to_int(core::at(arguments, ++i)));
        } else if (argument == "--dump" && has_value) {
            options.dump = core::at(arguments, ++i);
        }
    }
    return options;
}

// One shader to compile and validate.
struct Job {
    ShaderStage stage = ShaderStage::kVertex;
    std::string name;
    std::string description;
    std::string source;
    bool traced = false; // (round SKIN2: a fragment shader with ray queries - SPIR-V 1.4, compile_rt_glsl)
    std::vector<uint32_t> spirv;
    bool compiled = false;
    bool valid = false;
    std::string log;
};

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

#ifdef THUG2_SPIRV_VALIDATOR
// SPIRV-Tools' validator for Vulkan 1.0 (one per thread).
bool validate(Job& job)
{
    spvtools::SpirvTools tools(job.traced ? SPV_ENV_VULKAN_1_2 : SPV_ENV_VULKAN_1_0); // (ray queries: SPIR-V 1.4)
    std::string messages;
    tools.SetMessageConsumer([&messages](spv_message_level_t, const char*, const spv_position_t&, const char* text) {
        messages += text;
        messages += '\n';
    });
    // (round BIND) the traced keys read the scene's tables and the material table in scalar layout - as every ray pass
    // does; the device enables scalarBlockLayout with ray tracing (vulkan_context.cpp)
    spvtools::ValidatorOptions options;
    options.SetScalarBlockLayout(job.traced);
    const bool valid = tools.Validate(job.spirv.data(), job.spirv.size(), options);
    if (!valid) {
        job.log += messages;
    }
    return valid;
}
constexpr bool kValidator = true;
#else
bool validate(Job& /*job*/) { return true; }
constexpr bool kValidator = false;
#endif

void run_jobs(std::vector<Job>& jobs, int threads)
{
    std::atomic<std::size_t> next{0};
    const auto work = [&jobs, &next]() {
        for (std::size_t index = next++; index < jobs.size(); index = next++) {
            Job& job = core::at(jobs, index);
            render::vulkan::CompiledShader compiled =
                job.traced ? render::vulkan::compile_rt_glsl(render::vulkan::RtShaderStage::kFragment, job.source)
                           : render::vulkan::compile_glsl(job.stage, job.source);
            job.compiled = compiled.ok;
            job.log = compiled.log;
            job.spirv = std::move(compiled.spirv);
            job.valid = job.compiled && validate(job);
        }
    };
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) {
        pool.emplace_back(work);
    }
    work();
    for (std::thread& thread : pool) {
        thread.join();
    }
}

void report_failures(const std::vector<Job>& jobs, const char* what, std::size_t& failures, std::size_t& shown)
{
    for (const Job& job : jobs) {
        if (job.compiled && job.valid) {
            continue;
        }
        ++failures;
        if (shown++ < kReportedFailures) {
            text::print("FAILED (%s) %s: %s\n%s\n%s\n", what, job.name.c_str(), job.description.c_str(), job.log.c_str(),
                        job.source.c_str());
        }
    }
}

void dump_sources(const std::vector<Job>& jobs, const std::string& folder)
{
    for (const Job& job : jobs) {
        const std::string path = folder + "/" + job.name + (job.stage == ShaderStage::kVertex ? ".vert" : ".frag");
        std::ofstream file(path, std::ios::binary);
        file << job.source;
    }
}

// The keys in a fixed order (by hash).
void order_keys(const ff_check::KeySets& sets, std::vector<VertexKey>& vertex_order,
                std::vector<FragmentKey>& fragment_order)
{
    vertex_order.assign(sets.vertex.begin(), sets.vertex.end());
    fragment_order.assign(sets.fragment.begin(), sets.fragment.end());
    const auto by_hash = [](const auto& a, const auto& b) { return render::vulkan::hash_of(a) < render::vulkan::hash_of(b); };
    std::ranges::sort(vertex_order, by_hash);
    std::ranges::sort(fragment_order, by_hash);
}

std::vector<Job> make_vertex_jobs(const std::vector<VertexKey>& vertex_order)
{
    std::vector<Job> jobs;
    jobs.reserve(vertex_order.size());
    for (std::size_t index = 0; index < vertex_order.size(); ++index) {
        const VertexKey& key = core::at(vertex_order, index);
        jobs.push_back({.stage = ShaderStage::kVertex,
                        .name = text::format("v%05u", static_cast<unsigned>(index)),
                        .description = render::vulkan::describe(key),
                        .source = render::vulkan::vertex_shader_glsl(key)});
    }
    return jobs;
}

// The fragment shaders [begin, end) of the order.
std::vector<Job> make_fragment_jobs(const std::vector<FragmentKey>& fragment_order, std::size_t begin, std::size_t end)
{
    std::vector<Job> jobs;
    jobs.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        const FragmentKey& key = core::at(fragment_order, index);
        jobs.push_back({.stage = ShaderStage::kFragment,
                        .name = text::format("f%05u", static_cast<unsigned>(index)),
                        .description = render::vulkan::describe(key),
                        .source = render::vulkan::fragment_shader_glsl(key),
                        .traced = (key.surface & render::vulkan::kSurfaceSunTraced) != 0U});
    }
    return jobs;
}

// The process's peak commit (private bytes) in MB; 0 where it can't be asked.
std::size_t peak_commit_megabytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != FALSE) {
        return counters.PeakPagefileUsage / (kBytesPerKilobyte * kBytesPerKilobyte);
    }
#endif
    return 0;
}

void print_breakdown(const ff_check::KeySets& sets)
{
    std::map<uint32_t, std::size_t> by_fvf;
    for (const VertexKey& key : sets.vertex) {
        ++by_fvf[key.fvf];
    }
    std::map<int, std::size_t> by_program;
    for (const FragmentKey& key : sets.fragment) {
        ++by_program[(core::to_underlying(key.program) * kProgramDigit) + key.material_passes];
    }
    text::print("vertex keys by vertex format:");
    for (const auto& [fvf, count] : by_fvf) {
        text::print(" 0x%x:%u", fvf, static_cast<unsigned>(count));
    }
    text::print("\nfragment keys by program (program x 10 + material passes):");
    for (const auto& [program, count] : by_program) {
        text::print(" %d:%u", program, static_cast<unsigned>(count));
    }
    text::print("\n");
}

} // namespace

namespace {

// (rt_pbr) Each pair again with the per-pixel lighting's bits, where the key takes them (normalised() keeps them).
void add_surface_keys(ff_check::KeySets& sets)
{
    const std::vector<ShaderKey> pairs = sets.pairs;
    std::size_t added = 0;
    for (const ShaderKey& pair : pairs) {
        for (const uint8_t bits : {render::vulkan::kSurfaceLighting,
                                   static_cast<uint8_t>(render::vulkan::kSurfaceLighting | render::vulkan::kSurfaceMap),
                                   static_cast<uint8_t>(render::vulkan::kSurfaceLighting | render::vulkan::kSurfaceLit),
                                   static_cast<uint8_t>(render::vulkan::kSurfaceLighting | render::vulkan::kSurfaceMap |
                                                        render::vulkan::kSurfaceLit)}) {
            ShaderKey key = pair;
            key.fragment.surface = bits;
            key = render::vulkan::normalised(key);
            if (key.fragment.surface == 0) {
                continue;
            }
            sets.vertex.insert(key.vertex);
            if (sets.fragment.insert(key.fragment).second) {
                ++added;
            }
            sets.pairs.push_back(key);
            if ((key.fragment.surface & render::vulkan::kSurfaceLit) == 0U) {
                // (round M) the relit world's draw: the light store's words read and lit by the light model (the stream
                // at binding 2 - modules only, as the cel stream's)
                ShaderKey relit = key;
                relit.fragment.surface = static_cast<uint8_t>(relit.fragment.surface | render::vulkan::kSurfaceRelit);
                relit.vertex.surface = static_cast<uint8_t>(relit.vertex.surface | render::vulkan::kVertexCelStream |
                                                            render::vulkan::kVertexRelitColour);
                sets.vertex.insert(relit.vertex);
                if (sets.fragment.insert(relit.fragment).second) {
                    ++added;
                }
            }
        }
    }
    text::print("surface: %u fragment keys lit per pixel added\n", static_cast<unsigned>(added));
}

// (NOT ORIGINAL - rt_detail / rt_delight) Each pair lit per pixel with a surface map again with the detail layer's bits
// (normalised() keeps them only there).
void add_detail_keys(ff_check::KeySets& sets)
{
    using namespace render::vulkan;
    const std::vector<ShaderKey> pairs = sets.pairs;
    std::size_t added = 0;
    for (const ShaderKey& pair : pairs) {
        if ((pair.fragment.surface & kSurfaceMap) == 0U || pair.fragment.detail != 0U) {
            continue;
        }
        for (const uint8_t bits : {kDetailMaterial, kDetailRelief,
                                   static_cast<uint8_t>(kDetailRelief | kDetailMaterial)}) { // (added by surface relief)
            ShaderKey key = pair;
            key.fragment.detail = bits;
            key = normalised(key);
            if (key.fragment.detail != bits) {
                continue;
            }
            sets.vertex.insert(key.vertex);
            if (sets.fragment.insert(key.fragment).second) {
                ++added;
            }
            sets.pairs.push_back(key);
        }
    }
    text::print("detail: %u fragment keys with the detail layer added\n", static_cast<unsigned>(added));
}

// (added for the characters) Each pair lit by lights with a surface map again with the characters' bits (normalised()
// keeps them only there).
void add_character_keys(ff_check::KeySets& sets)
{
    using namespace render::vulkan;
    const std::vector<ShaderKey> pairs = sets.pairs;
    std::size_t added = 0;
    for (const ShaderKey& pair : pairs) {
        if ((pair.fragment.surface & kSurfaceMap) == 0U || (pair.fragment.surface & kSurfaceLit) == 0U ||
            pair.fragment.detail != 0U) {
            continue;
        }
        for (const uint8_t bits : {kDetailSkin, kDetailCloth, static_cast<uint8_t>(kDetailSkin | kDetailCloth), kDetailHair}) {
            ShaderKey key = pair;
            key.fragment.detail = bits;
            key = normalised(key);
            if (key.fragment.detail != bits) {
                continue;
            }
            sets.vertex.insert(key.vertex);
            if (sets.fragment.insert(key.fragment).second) {
                ++added;
            }
            sets.pairs.push_back(key);
        }
    }
    text::print("characters: %u fragment keys with skin, cloth or hair added\n", static_cast<unsigned>(added));
}

// (added by cel shading) Each pair again with the bands (the world's bake, lit by lights, the outline's hull) - made into
// pipelines with the rest -, and its halves with the relit world's stream (modules only: this check's pipelines have no
// second stream). The ray-traced visibility (kSurfaceSunTraced) needs SPIR-V 1.4 and a ray-tracing device:
// thug2_vulkan_cel_check makes those.
void add_cel_keys(ff_check::KeySets& sets)
{
    using namespace render::vulkan;
    const std::vector<ShaderKey> pairs = sets.pairs;
    std::size_t added = 0;
    std::size_t streams = 0;
    for (const ShaderKey& pair : pairs) {
        for (const uint8_t bits :
             {static_cast<uint8_t>(kSurfaceLighting | kSurfaceCel), static_cast<uint8_t>(kSurfaceLighting | kSurfaceCel | kSurfaceLit),
              static_cast<uint8_t>(kSurfaceLighting | kSurfaceCel | kSurfaceLit | kSurfaceCelHull),
              static_cast<uint8_t>(kSurfaceLighting | kSurfaceCel | kSurfaceCelStream)}) {
            ShaderKey key = pair;
            key.fragment.surface = bits;
            key = normalised(key);
            if (key.fragment.surface == 0 || (key.fragment.surface & kSurfaceCel) == 0U) {
                continue;
            }
            sets.vertex.insert(key.vertex);
            if (sets.fragment.insert(key.fragment).second) {
                ++added;
            }
            if ((key.vertex.surface & kVertexCelStream) != 0U) {
                ++streams;
                continue;
            }
            sets.pairs.push_back(key);
        }
    }
    text::print("cel: %u fragment keys in bands added (%u with the stream: modules only)\n", static_cast<unsigned>(added),
                static_cast<unsigned>(streams));
}

// (NOT ORIGINAL - rt_water) Each pair again shaded as water, flat and moving, where the key takes it.
void add_water_keys(ff_check::KeySets& sets)
{
    using namespace render::vulkan;
    const std::vector<ShaderKey> pairs = sets.pairs;
    std::size_t fragments = 0;
    std::size_t vertices = 0;
    for (const ShaderKey& pair : pairs) {
        for (const uint8_t water : {kWaterFlat, kWaterMoving}) {
            ShaderKey key = pair;
            key.fragment.water = water;
            key = normalised(key);
            if (key.fragment.water != water) {
                continue;
            }
            vertices += sets.vertex.insert(key.vertex).second ? 1U : 0U;
            fragments += sets.fragment.insert(key.fragment).second ? 1U : 0U;
            sets.pairs.push_back(key);
        }
    }
    text::print("water: %u fragment keys and %u vertex keys shaded as water (flat or moving) added\n", static_cast<unsigned>(fragments),
                static_cast<unsigned>(vertices));
}

// (NOT ORIGINAL - the surface trails) Each pair again with the snow's cover, the paint's coat and both, where it takes them.
void add_trail_keys(ff_check::KeySets& sets)
{
    using namespace render::vulkan;
    const std::vector<ShaderKey> pairs = sets.pairs;
    std::size_t fragments = 0;
    for (const ShaderKey& pair : pairs) {
        // (the snow's alone, traced, over the relit tint; the paint; all of them)
        constexpr uint8_t kSnowTraced = kTrailSnow | kTrailSnowTraced;
        constexpr uint8_t kSnowTint = kTrailSnow | kTrailSnowTint;
        constexpr uint8_t kSnowPaint = kTrailSnow | kTrailPaint;
        // (NOT ORIGINAL - the rain's wet ground: alone and with the paint)
        constexpr uint8_t kWetPaint = kTrailWet | kTrailPaint;
        for (const uint8_t trails : {kTrailSnow, kSnowTraced, kSnowTint, kTrailPaint, kSnowPaint, kTrailWet, kWetPaint, kTrailKnown}) {
            ShaderKey key = pair;
            key.fragment.trails = trails;
            key = normalised(key);
            if (key.fragment.trails != trails) {
                continue;
            }
            sets.vertex.insert(key.vertex);
            fragments += sets.fragment.insert(key.fragment).second ? 1U : 0U;
            sets.pairs.push_back(key);
        }
    }
    text::print("trails: %u fragment keys with the snow, the paint or both added\n", static_cast<unsigned>(fragments));
}

// (the upscaler's) Each pair's halves again with the motion vectors, where the key takes them (not pre-transformed).
void add_motion_keys(ff_check::KeySets& sets)
{
    std::size_t added = 0;
    for (const ShaderKey& pair : sets.pairs) {
        ShaderKey key = pair;
        key.vertex.motion = 1;
        key = render::vulkan::normalised(key);
        if (key.vertex.motion == 0) {
            continue;
        }
        sets.vertex.insert(key.vertex);
        if (sets.fragment.insert(key.fragment).second) {
            ++added;
        }
    }
    text::print("motion: %u fragment keys with motion vectors added (modules only)\n", static_cast<unsigned>(added));
}

// (NOT ORIGINAL - round V) Each pair again with the light bits it can take: the prepass's (depth alone, a blended one's
// half), the shares (not lit per pixel), the inputs (lit per pixel). Pre-transformed and water pairs are never kept.
void add_light_keys(ff_check::KeySets& sets)
{
    using render::vulkan::kLightInputs;
    using render::vulkan::kLightShare;
    std::size_t added = 0;
    const std::vector<ShaderKey> pairs = sets.pairs;
    for (const ShaderKey& pair : pairs) {
        if (render::vulkan::fvf_pre_transformed(pair.vertex.fvf) || pair.fragment.water != 0U ||
            (pair.fragment.surface & (render::vulkan::kSurfaceCel | render::vulkan::kSurfaceCelHull)) != 0U) {
            continue;
        }
        std::vector<ShaderKey> keys;
        // (the prepass: the shaded draw's vertex half - with the upscaler's motion too, as a view drawn for upscaling
        // has it - and its fragment half the program alone, a blended one's half)
        for (const bool half : {false, true}) {
            ShaderKey depth = pair;
            depth.fragment = render::vulkan::depth_only_fragment(pair.fragment, half);
            keys.push_back(depth);
            depth.vertex.motion = 1;
            if (render::vulkan::normalised(depth).vertex.motion != 0U) {
                keys.push_back(depth);
            }
        }
        ShaderKey lit = pair;
        lit.fragment.light = (pair.fragment.surface & render::vulkan::kSurfaceLighting) != 0U ? kLightInputs
                             : pair.fragment.surface == 0                                    ? kLightShare
                                                                                             : uint8_t{0};
        if (lit.fragment.light != 0U) {
            keys.push_back(lit);
            if ((lit.fragment.surface & render::vulkan::kSurfaceLit) != 0U) {
                // (round SKIN2: a see-through lit overlay - an object's glass, hair cards - reading the front layer where
                // it is the nearest, else tracing its own sun - ff_shaders.cpp kSurfaceSunGlsl; modules only)
                for (const uint8_t front : {uint8_t{0}, render::vulkan::kLightFront}) {
                    FragmentKey traced = lit.fragment;
                    traced.light = static_cast<uint8_t>(kLightInputs | front);
                    traced.surface = static_cast<uint8_t>(traced.surface | render::vulkan::kSurfaceSunTraced);
                    added += sets.fragment.insert(traced).second ? 1U : 0U;
                }
            }
            // (round SKIN - engine/gfx/rt_coverage.h: a blended draw that writes depth, shaded where its prepass kept it -
            // kLightDepthHalf - with Ray Reconstruction's guides, as the game draws it under RR; the guides' code is the
            // plain lit keys' as well)
            ShaderKey covered = lit;
            covered.fragment.light =
                static_cast<uint8_t>(covered.fragment.light | render::vulkan::kLightDepthHalf | render::vulkan::kLightGuides);
            keys.push_back(covered);
        }
        if (lit.fragment.light == kLightShare) {
            // (round FRONT: a particle - shares that read the front layer - and its depth in the layer where it covers)
            ShaderKey particle = lit;
            particle.fragment.light = static_cast<uint8_t>(kLightShare | render::vulkan::kLightFront);
            keys.push_back(particle);
            ShaderKey covered = pair;
            covered.fragment = render::vulkan::depth_only_fragment(pair.fragment, false);
            covered.fragment.light = static_cast<uint8_t>(covered.fragment.light | render::vulkan::kLightFront);
            keys.push_back(covered);
        }
        for (const ShaderKey& key : keys) {
            sets.vertex.insert(key.vertex);
            if (sets.fragment.insert(key.fragment).second) {
                ++added;
            }
            sets.pairs.push_back(key);
        }
    }
    // (the relit world's keys - modules only, as --surface adds them) with the inputs
    for (FragmentKey key : std::vector<FragmentKey>(sets.fragment.begin(), sets.fragment.end())) {
        if ((key.surface & render::vulkan::kSurfaceRelit) != 0U && key.light == 0U && key.water == 0U &&
            (key.surface & render::vulkan::kSurfaceCel) == 0U) {
            key.light = kLightInputs;
            if (sets.fragment.insert(key).second) {
                ++added;
            }
            FragmentKey covered = key; // (round SKIN: a blended relit draw that writes depth - kLightDepthHalf)
            covered.light = static_cast<uint8_t>(kLightInputs | render::vulkan::kLightDepthHalf);
            if (sets.fragment.insert(covered).second) {
                ++added;
            }
            // (round SKIN2: a see-through relit overlay - read the front layer where it is the nearest, else its own sun
            // ray - ff_shaders.cpp kSurfaceSunGlsl; modules only)
            for (const uint8_t front : {uint8_t{0}, render::vulkan::kLightFront}) {
                FragmentKey traced = key;
                traced.light = static_cast<uint8_t>(kLightInputs | front);
                traced.surface = static_cast<uint8_t>(traced.surface | render::vulkan::kSurfaceSunTraced);
                if (sets.fragment.insert(traced).second) {
                    ++added;
                }
            }
            // (round DECAL: a see-through overlay laid into its surface - its shaded draw and its laying)
            for (const uint8_t layer : {render::vulkan::kLightLayer,
                                        static_cast<uint8_t>(render::vulkan::kLightLayer | render::vulkan::kLightLayerWrite)}) {
                FragmentKey laid = key;
                laid.light = static_cast<uint8_t>(kLightInputs | layer);
                if ((layer & render::vulkan::kLightLayerWrite) != 0U) {
                    laid.motion = 0;
                }
                if (sets.fragment.insert(laid).second) {
                    ++added;
                }
            }
        }
    }
    text::print("light: %u fragment keys with the light inputs or depth alone added\n", static_cast<unsigned>(added));
}

// (NOT ORIGINAL - the retro art style) Each pair again with its vertex half's retro bit, where the key takes it (not
// pre-transformed); the vertex keys with motion too.
void add_retro_keys(ff_check::KeySets& sets)
{
    std::size_t added = 0;
    const std::vector<ShaderKey> pairs = sets.pairs;
    for (const ShaderKey& pair : pairs) {
        ShaderKey key = pair;
        key.vertex.retro = 1;
        key = render::vulkan::normalised(key);
        if (key.vertex.retro == 0) {
            continue;
        }
        if (sets.vertex.insert(key.vertex).second) {
            ++added;
        }
        sets.pairs.push_back(key);
    }
    for (VertexKey key : std::vector<VertexKey>(sets.vertex.begin(), sets.vertex.end())) {
        if (key.motion != 0U && key.retro == 0U) {
            key.retro = 1;
            if (sets.vertex.insert(key).second) { // (motion keys are never pre-transformed)
                ++added;
            }
        }
    }
    text::print("retro: %u vertex keys snapped and affine added\n", static_cast<unsigned>(added));
}

// What the compiling keeps across the batches.
struct Totals {
    std::size_t shaders = 0;
    std::size_t words = 0;
    std::size_t failures = 0;
    std::size_t shown = 0;
    double seconds = 0.0;
};

// Compiles and validates the jobs, reports the failures and counts them; dumps the sources when asked.
void compile_batch(std::vector<Job>& jobs, const Options& options, Totals& totals)
{
    const auto start = std::chrono::steady_clock::now();
    run_jobs(jobs, options.jobs);
    totals.seconds += seconds_since(start);
    report_failures(jobs, "compile / validate", totals.failures, totals.shown);
    for (const Job& job : jobs) {
        totals.words += job.spirv.size();
    }
    totals.shaders += jobs.size();
    if (!options.dump.empty()) {
        dump_sources(jobs, options.dump);
    }
}

// The valid shaders' SPIR-V in the jobs' order (null for one that failed: the GPU part leaves it out).
std::vector<const std::vector<uint32_t>*> spirv_of(const std::vector<Job>& jobs)
{
    std::vector<const std::vector<uint32_t>*> spirv;
    spirv.reserve(jobs.size());
    for (const Job& job : jobs) {
        spirv.push_back(job.compiled && job.valid ? &job.spirv : nullptr);
    }
    return spirv;
}

// (round SWEEP) The vertex shaders first (the GPU part keeps their modules), then the fragment shaders in batches: each
// batch is compiled, validated, handed to the driver with the pipelines of the pairs that use it, and freed before the
// next - so what the process holds doesn't grow with the number of keys.
int sweep(const ff_check::KeySets& sets, const Options& options)
{
    std::vector<VertexKey> vertex_order;
    std::vector<FragmentKey> fragment_order;
    order_keys(sets, vertex_order, fragment_order);
    Totals totals;
    std::unique_ptr<ff_check::GpuSession> gpu;
    bool gpu_ok = true;
    {
        std::vector<Job> jobs = make_vertex_jobs(vertex_order);
        compile_batch(jobs, options, totals);
        if (options.gpu) {
            gpu = ff_check::GpuSession::open(options.device, options.pipelines, gpu_ok);
            if (gpu) {
                gpu->add_vertex_modules(vertex_order, spirv_of(jobs));
            }
        }
    }
    // The pairs by their fragment shader's place, so a batch takes the ones that use it.
    std::vector<std::size_t> fragment_of(sets.pairs.size());
    std::vector<std::size_t> pair_order(sets.pairs.size());
    if (gpu) {
        std::unordered_map<FragmentKey, std::size_t, render::vulkan::FragmentKeyHash> fragment_index;
        for (std::size_t index = 0; index < fragment_order.size(); ++index) {
            fragment_index.emplace(core::at(fragment_order, index), index);
        }
        for (std::size_t index = 0; index < sets.pairs.size(); ++index) {
            core::at(fragment_of, index) = fragment_index.at(core::at(sets.pairs, index).fragment);
            core::at(pair_order, index) = index;
        }
        std::ranges::stable_sort(pair_order, [&fragment_of](std::size_t a, std::size_t b) {
            return core::at(fragment_of, a) < core::at(fragment_of, b);
        });
    }
    const std::size_t batch = static_cast<std::size_t>(options.batch);
    const std::size_t batches = (fragment_order.size() + batch - 1) / batch;
    std::size_t next_pair = 0;
    std::size_t number = 0;
    for (std::size_t begin = 0; begin < fragment_order.size(); begin += batch) {
        const std::size_t end = std::min(begin + batch, fragment_order.size());
        std::vector<Job> jobs = make_fragment_jobs(fragment_order, begin, end);
        compile_batch(jobs, options, totals);
        if (gpu) {
            std::vector<ff_check::BatchPair> pairs;
            for (; next_pair < pair_order.size() && core::at(fragment_of, core::at(pair_order, next_pair)) < end; ++next_pair) {
                const std::size_t pair = core::at(pair_order, next_pair);
                pairs.push_back({.key = &core::at(sets.pairs, pair), .fragment = core::at(fragment_of, pair) - begin});
            }
            gpu->run_batch(spirv_of(jobs), pairs);
        }
        text::print("batch %u / %u: %u fragment shaders\n", static_cast<unsigned>(++number), static_cast<unsigned>(batches),
                    static_cast<unsigned>(end - begin));
    }
    const std::size_t kilobytes = totals.words * sizeof(uint32_t) / kBytesPerKilobyte;
    text::print("compiled %u shaders with %d threads in %.1f s (%u KB of SPIR-V): %u failed\n",
                static_cast<unsigned>(totals.shaders), options.jobs, totals.seconds, static_cast<unsigned>(kilobytes),
                static_cast<unsigned>(totals.failures));
    bool ok = totals.failures == 0U && gpu_ok;
    if (gpu) {
        ok = gpu->finish() && ok;
    }
    return ok ? 0 : 1;
}

int run(const Options& options)
{
    text::print("thug2_ff_shader_check: a %d-bit process, SPIR-V validator %s\n",
                static_cast<int>(sizeof(void*)) * kBitsPerByte, kValidator ? "on" : "not built");
    const auto start = std::chrono::steady_clock::now();
    ff_check::KeySets sets = ff_check::measured_keys(options.all_materials);
    if (options.surface) {
        add_surface_keys(sets);
    }
    if (options.detail) {
        add_detail_keys(sets); // (NOT ORIGINAL - rt_detail / rt_delight: needs --surface's keys)
    }
    if (options.characters) {
        add_character_keys(sets); // (added for the characters: needs --surface's keys)
    }
    if (options.cel) {
        add_cel_keys(sets);
    }
    if (options.water) {
        add_water_keys(sets);
    }
    if (options.trails) {
        add_trail_keys(sets);
    }
    if (options.light) {
        add_light_keys(sets); // (round V)
    }
    if (options.motion) {
        add_motion_keys(sets);
    }
    if (options.retro) {
        add_retro_keys(sets);
    }
    text::print("model: %u sites, %u stage states reached, %u keys made, %u unsupported -> %u vertex keys, %u fragment "
                "keys, %u sampled pairs (%.1f s)\n",
                static_cast<unsigned>(sets.sites), static_cast<unsigned>(sets.tss_states),
                static_cast<unsigned>(sets.raw_keys), static_cast<unsigned>(sets.unsupported),
                static_cast<unsigned>(sets.vertex.size()), static_cast<unsigned>(sets.fragment.size()),
                static_cast<unsigned>(sets.pairs.size()), seconds_since(start));
    print_breakdown(sets);

    const int code = sweep(sets, options);
    text::print("peak commit %u MB, %.1f s\n", static_cast<unsigned>(peak_commit_megabytes()), seconds_since(start));
    return code;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    try {
        return run(read_options(std::span(argv, static_cast<std::size_t>(argc))));
    } catch (const std::exception& failure) {
        text::print("thug2_ff_shader_check: %s\n", failure.what());
        return 1;
    }
}
