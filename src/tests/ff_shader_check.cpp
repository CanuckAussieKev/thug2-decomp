// NOT ORIGINAL - thug2_ff_shader_check.exe: the Vulkan backend's fixed-function emulation checked offline
// (gpu_vulkan/ff_key.h, ff_shaders.h, glsl_compiler.h). It lists every shader key the Direct3D 9 backend's draws can
// make (ff_shader_check_model.h), generates each key's GLSL, compiles it with glslang, validates the SPIR-V with
// SPIRV-Tools' validator (when the build has it: THUG2_SPIRV_VALIDATOR), and - unless --no-gpu - hands every module and
// a pipeline for each sampled pair of shaders to a Vulkan driver (ff_shader_check_gpu.h).
//   thug2_ff_shader_check [--all-materials] [--no-gpu] [--device N] [--pipelines N] [--jobs N] [--dump FOLDER] [--surface] [--motion] [--cel]
//                         [--retro] [--detail]
// --detail (NOT ORIGINAL - rt_detail / rt_delight, gpu_vulkan/vulkan_detail.h): with --surface, every pair lit per pixel
// with a surface map added again with each of the detail layer's bit sets (ff_key.h kDetail*) - after --surface's.
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
// --all-materials: every four-pass material shape, not a covering subset; --device: the physical device for the GPU
// part (the context check's numbering); --pipelines: at most N pipelines (default: every sampled pair); --jobs: the
// compiling threads (default 8); --dump: every generated shader's GLSL written into FOLDER (vNNNN.vert, fNNNN.frag).
// Exit code: 0 when everything compiled, validated and (with the GPU part) was accepted; 1 otherwise; the GPU part
// alone missing (no Vulkan) leaves it 0 and says so.
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
#include <span>
#include <string>
#include <string_view>
#include <thread>
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
    std::string dump;
    bool surface = false;
    bool detail = false; // (NOT ORIGINAL - rt_detail / rt_delight)
    bool motion = false;
    bool cel = false; // (added by cel shading)
    bool retro = false; // (the retro art style)
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
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
    std::string messages;
    tools.SetMessageConsumer([&messages](spv_message_level_t, const char*, const spv_position_t&, const char* text) {
        messages += text;
        messages += '\n';
    });
    const bool valid = tools.Validate(job.spirv);
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
            render::vulkan::CompiledShader compiled = render::vulkan::compile_glsl(job.stage, job.source);
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

void report_failures(const std::vector<Job>& jobs, const char* what, std::size_t& failures)
{
    std::size_t shown = 0;
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

std::vector<Job> make_jobs(const ff_check::KeySets& sets, std::vector<VertexKey>& vertex_order,
                           std::vector<FragmentKey>& fragment_order)
{
    std::vector<Job> jobs;
    vertex_order.assign(sets.vertex.begin(), sets.vertex.end());
    fragment_order.assign(sets.fragment.begin(), sets.fragment.end());
    const auto by_hash = [](const auto& a, const auto& b) { return render::vulkan::hash_of(a) < render::vulkan::hash_of(b); };
    std::ranges::sort(vertex_order, by_hash);
    std::ranges::sort(fragment_order, by_hash);
    jobs.reserve(vertex_order.size() + fragment_order.size());
    for (std::size_t index = 0; index < vertex_order.size(); ++index) {
        const VertexKey& key = core::at(vertex_order, index);
        jobs.push_back({.stage = ShaderStage::kVertex,
                        .name = text::format("v%05u", static_cast<unsigned>(index)),
                        .description = render::vulkan::describe(key),
                        .source = render::vulkan::vertex_shader_glsl(key)});
    }
    for (std::size_t index = 0; index < fragment_order.size(); ++index) {
        const FragmentKey& key = core::at(fragment_order, index);
        jobs.push_back({.stage = ShaderStage::kFragment,
                        .name = text::format("f%05u", static_cast<unsigned>(index)),
                        .description = render::vulkan::describe(key),
                        .source = render::vulkan::fragment_shader_glsl(key)});
    }
    return jobs;
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
        for (const uint8_t bits : {kDetailMaterial, kDetailDelight, static_cast<uint8_t>(kDetailMaterial | kDetailDelight)}) {
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

// (added by cel shading) Each pair again with the bands (the world's bake, lit by lights, the outline's hull) - made into
// pipelines with the rest -, and its halves with the relit world's stream (modules only: this check's pipelines have no
// second stream). The ray-traced visibility (kSurfaceCelShadow) needs SPIR-V 1.4 and a ray-tracing device:
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
    if (options.cel) {
        add_cel_keys(sets);
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

    std::vector<VertexKey> vertex_order;
    std::vector<FragmentKey> fragment_order;
    std::vector<Job> jobs = make_jobs(sets, vertex_order, fragment_order);
    const auto compile_start = std::chrono::steady_clock::now();
    run_jobs(jobs, options.jobs);
    std::size_t failures = 0;
    report_failures(jobs, "compile / validate", failures);
    std::size_t words = 0;
    for (const Job& job : jobs) {
        words += job.spirv.size();
    }
    const std::size_t kilobytes = words * sizeof(uint32_t) / kBytesPerKilobyte;
    text::print("compiled %u shaders with %d threads in %.1f s (%u KB of SPIR-V): %u failed\n",
                static_cast<unsigned>(jobs.size()), options.jobs, seconds_since(compile_start),
                static_cast<unsigned>(kilobytes), static_cast<unsigned>(failures));
    if (!options.dump.empty()) {
        dump_sources(jobs, options.dump);
    }
    if (failures != 0U) {
        return 1;
    }
    if (!options.gpu) {
        return 0;
    }
    ff_check::GpuInput input;
    input.vertex_keys = &vertex_order;
    input.fragment_keys = &fragment_order;
    for (const Job& job : jobs) {
        (job.stage == ShaderStage::kVertex ? input.vertex_spirv : input.fragment_spirv).push_back(&job.spirv);
    }
    input.pairs = &sets.pairs;
    input.device_index = options.device;
    input.pipeline_limit = options.pipelines;
    return ff_check::check_on_gpu(input) ? 0 : 1;
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
