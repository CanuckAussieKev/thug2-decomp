// NOT ORIGINAL - the fixed-function shader check's model of the Direct3D 9 backend's draws (ff_shader_check_model.h).
// Each site names the code it reads. The model assumes pixel shader support, as every Vulkan device has: the Direct3D 9
// backend's stand-in stages for devices without ps.1.1 (render_state.cpp set_fixed_function_stages, the multi-pass
// branches of pc_mesh_render.cpp / pc_billboards.cpp) never run. A pixel shader slot the backend leaves empty with
// shaders (render_pc/shader_slots.h: every slot but the six fixed shaders) draws with the texture stages.
#include "tests/ff_shader_check_model.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "gpu_vulkan/ff_key.h"

namespace ff_check {

namespace {

using render::vulkan::arg;
using render::vulkan::ArgSource;
using render::vulkan::CompareFunc;
using render::vulkan::FogMode;
using render::vulkan::FragmentKey;
using render::vulkan::kFvfDiffuse;
using render::vulkan::kFvfNormal;
using render::vulkan::kFvfXyz;
using render::vulkan::kFvfXyzRhw;
using render::vulkan::kStageCount;
using render::vulkan::PixelProgram;
using render::vulkan::ShaderKey;
using render::vulkan::StageKey;
using render::vulkan::StageOp;
using render::vulkan::TexCoordKey;
using render::vulkan::TexGen;
using render::vulkan::VertexKey;

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr uint32_t kFvfTex1 = 0x100U;
constexpr uint32_t kMeshFvf = kFvfXyz | kFvfNormal | kFvfDiffuse; // pc_mesh_load.cpp kFvfBase
constexpr uint32_t kScreenFvf = kFvfXyzRhw | kFvfDiffuse | kFvfTex1; // render_state.h kScreenVertexFormat
constexpr uint8_t kLights = 2;                                       // pc_instance.cpp: lights 0 and 1
constexpr uint8_t kModeCount = 14;                                   // the modes with shader code: 0 (none) - 0xD
constexpr std::size_t kMaxPasses = 4;
constexpr uint32_t kSpecularBit = 16;  // (the memo of the materials done: passes x 2 + fog, these flags)
constexpr uint32_t kAlphaTestBit = 32;

// A stage's Direct3D 9 state: its operations and its coordinates.
struct StageState {
    StageKey ops{};
    TexCoordKey coords{};
    bool operator==(const StageState&) const = default;
};
using TssState = std::array<StageState, kStageCount>;

struct TssHash {
    std::size_t operator()(const TssState& state) const
    {
        const auto bytes = std::bit_cast<std::array<std::byte, sizeof(TssState)>>(state);
        uint64_t hash = kFnvOffset;
        for (const std::byte value : bytes) {
            hash = (hash ^ static_cast<uint64_t>(value)) * kFnvPrime;
        }
        return static_cast<std::size_t>(hash);
    }
};

constexpr uint8_t kTexture = arg(ArgSource::kTexture);
constexpr uint8_t kCurrent = arg(ArgSource::kCurrent);
constexpr uint8_t kDiffuse = arg(ArgSource::kDiffuse);
constexpr uint8_t kFactor = arg(ArgSource::kFactor);

// The device's defaults (D3DTSS_ in the Direct3D 9 documentation): stage 0 MODULATE(TEXTURE, CURRENT) /
// SELECTARG1(TEXTURE), the others DISABLE; each stage's coordinates its own set, untransformed.
TssState device_defaults()
{
    TssState state{};
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        StageState& entry = core::at(state, stage);
        entry.ops = {.colour_op = stage == 0 ? StageOp::kModulate : StageOp::kDisable,
                     .colour_arg0 = kCurrent,
                     .colour_arg1 = kTexture,
                     .colour_arg2 = kCurrent,
                     .alpha_op = stage == 0 ? StageOp::kSelectArg1 : StageOp::kDisable,
                     .alpha_arg0 = kCurrent,
                     .alpha_arg1 = kTexture,
                     .alpha_arg2 = kCurrent};
        entry.coords = {.set = static_cast<uint8_t>(stage)};
    }
    return state;
}

StageKey& ops(TssState& state, std::size_t stage) { return core::at(state, stage).ops; }

// A stage's colour and alpha operations.
struct OpPair {
    StageOp colour = StageOp::kDisable;
    StageOp alpha = StageOp::kDisable;
};

void set_ops(TssState& state, std::size_t stage, OpPair pair)
{
    ops(state, stage).colour_op = pair.colour;
    ops(state, stage).alpha_op = pair.alpha;
}

// A material pass's coordinates (pc_material.cpp hand_in / submit_pass): an environment map's reflection vector
// through its tiling matrix (COUNT2), a UV wibble's set through its matrix (COUNT2), or the set untransformed.
enum class Coords : uint8_t {
    kPlain,
    kWibble,
    kEnvironment,
};
constexpr std::array<Coords, 3> kAllCoords = {Coords::kPlain, Coords::kWibble, Coords::kEnvironment};

void set_coords(TssState& state, std::size_t stage, Coords coords, uint32_t pass)
{
    TexCoordKey& key = core::at(state, stage).coords;
    key.set = static_cast<uint8_t>(pass);
    key.gen = coords == Coords::kEnvironment ? TexGen::kCameraSpaceReflection : TexGen::kPassThrough;
    key.count = coords == Coords::kPlain ? 0 : 2;
    key.projected = false;
}

// pc_material.cpp submit_pass (0x004D24D0) for pass `pass`: stage 0's coordinates (use_pass_coordinates for a plain
// pass: stage 1 too for the modes blending with the pass before), then one of its three operation branches.
struct PassSubmit {
    uint32_t pass = 0;
    Coords coords = Coords::kPlain;
    bool lerp = false;          // the pass's mode is 0xC / 0xD
    bool colour_changed = false; // the pass colour isn't 0.5 grey, or a tint is given
    bool alpha_from_texture = false;
};

void submit_pass(TssState& state, const PassSubmit& submit)
{
    set_coords(state, 0, submit.coords, submit.pass);
    if (submit.coords == Coords::kPlain && submit.lerp) {
        set_coords(state, 1, Coords::kPlain, submit.pass > 0 ? submit.pass - 1 : submit.pass);
    }
    const StageOp alpha0 = submit.alpha_from_texture ? StageOp::kSelectArg1 : StageOp::kModulate2x;
    if (!submit.colour_changed && !submit.lerp) {
        ops(state, 1).colour_op = StageOp::kDisable;
        ops(state, 0) = {.colour_op = StageOp::kModulate2x, .colour_arg0 = ops(state, 0).colour_arg0, .colour_arg1 = kTexture,
                         .colour_arg2 = kCurrent, .alpha_op = alpha0, .alpha_arg0 = ops(state, 0).alpha_arg0,
                         .alpha_arg1 = kTexture, .alpha_arg2 = kCurrent};
        return;
    }
    if (!submit.colour_changed) {
        ops(state, 0) = {.colour_op = StageOp::kModulate2x, .colour_arg0 = ops(state, 0).colour_arg0, .colour_arg1 = kTexture,
                         .colour_arg2 = kCurrent, .alpha_op = StageOp::kSelectArg2, .alpha_arg0 = ops(state, 0).alpha_arg0,
                         .alpha_arg1 = kTexture, .alpha_arg2 = kCurrent};
        set_ops(state, 1, {.colour = StageOp::kSelectArg2, .alpha = alpha0});
        ops(state, 1).alpha_arg1 = kTexture;
        ops(state, 1).colour_arg2 = kCurrent;
    } else {
        StageKey& first = ops(state, 0);
        first.colour_op = StageOp::kModulate2x;
        first.colour_arg1 = kTexture;
        first.colour_arg2 = kFactor;
        first.alpha_op = alpha0;
        if (!submit.alpha_from_texture) {
            first.alpha_arg2 = kCurrent;
        }
        first.alpha_arg1 = kTexture;
        StageKey& second = ops(state, 1);
        second.colour_op = StageOp::kModulate2x;
        second.colour_arg1 = kCurrent;
        second.colour_arg2 = kDiffuse;
        second.alpha_op = submit.lerp ? StageOp::kSelectArg1 : StageOp::kSelectArg2;
        if (!submit.lerp) {
            second.alpha_arg2 = kCurrent;
        }
    }
    ops(state, 2).colour_op = StageOp::kDisable;
}

// What a site draws with.
struct Draw {
    std::vector<PixelProgram> programs;
    uint8_t passes = 1; // kMaterial's passes (each stage below them sampled)
    std::vector<uint32_t> fvfs;
    bool points = false;
    bool point_scale = false;
    bool point_sprite = false;
    bool fog = true; // fog can be on (the game's fog state is the level's, sticky across the 2D)
};

struct Site {
    const char* where = "";
    std::function<void(TssState&)> before;
    std::function<void(TssState&)> after;
    std::vector<Draw> draws;
};

std::vector<PixelProgram> stages_only() { return {PixelProgram::kStages}; }
// A one-pass material's shaders (pixel_shaders.cpp material_pixel_shader): no texture 0x95C, brighten 0x940 / 0x944,
// else 0x960 / 0x964.
std::vector<PixelProgram> one_pass()
{
    return {PixelProgram::kTextureVertexX4, PixelProgram::kTextureConstantAlpha, PixelProgram::kVertexAlphaTextureAlpha,
            PixelProgram::kConstantAlphaTextureAlpha, PixelProgram::kVertexFactor};
}

std::vector<uint32_t> mesh_fvfs(uint32_t lowest_sets)
{
    std::vector<uint32_t> fvfs;
    for (uint32_t sets = lowest_sets; sets <= kMaxPasses; ++sets) {
        fvfs.push_back(kMeshFvf | (sets * kFvfTex1));
    }
    return fvfs;
}

// The meshes (pc_mesh_render.cpp 0x004D3F40 -> MaterialRef::hand_in, pc_material.cpp 0x004D1F00: each pass p's
// coordinates on stage p), with their material's shader (pixel_shaders.cpp); the same for the billboards, shatter, the
// texture projections, splats, the primitives and the flexible particles that hand a material in.
void add_material_sites(std::vector<Site>& sites)
{
    for (uint32_t passes = 1; passes <= kMaxPasses; ++passes) {
        uint32_t combinations = 1;
        for (uint32_t pass = 0; pass < passes; ++pass) {
            combinations *= static_cast<uint32_t>(kAllCoords.size());
        }
        for (uint32_t combination = 0; combination < combinations; ++combination) {
            Site site;
            site.where = "pc_mesh_render.cpp / pc_material.cpp hand_in: a material's passes";
            site.before = [passes, combination](TssState& state) {
                uint32_t rest = combination;
                for (uint32_t pass = 0; pass < passes; ++pass) {
                    set_coords(state, pass, core::at(kAllCoords, rest % kAllCoords.size()), pass);
                    rest /= static_cast<uint32_t>(kAllCoords.size());
                }
            };
            Draw draw;
            draw.programs = passes == 1 ? one_pass() : std::vector<PixelProgram>{PixelProgram::kMaterial};
            draw.passes = static_cast<uint8_t>(passes);
            draw.fvfs = mesh_fvfs(passes == 1 ? 0 : passes);
            if (passes == 1) {
                // pc_primitive.cpp (0x42 / 0x142 / 0x152), texture_splats.cpp, pc_flexible_particles.cpp (0x142, 0x42)
                for (const uint32_t fvf : {kFvfXyz | kFvfDiffuse, kFvfXyz | kFvfDiffuse | kFvfTex1,
                                           kFvfXyz | kFvfDiffuse | kFvfTex1 | kFvfNormal}) {
                    draw.fvfs.push_back(fvf);
                }
            }
            site.draws.push_back(draw);
            sites.push_back(site);
        }
    }
}

// pc_particles.cpp (0x004E1E80): submit_pass(0) - its stage states - then the texture x vertex colour x4 shader.
void add_particle_sites(std::vector<Site>& sites)
{
    for (const Coords coords : kAllCoords) {
        for (const bool lerp : {false, true}) {
            for (const bool changed : {false, true}) {
                for (const bool alpha : {false, true}) {
                    Site site;
                    site.where = "pc_particles.cpp: submit_pass(0), then the 0x964 shader";
                    site.before = [=](TssState& state) {
                        submit_pass(state, {.pass = 0, .coords = coords, .lerp = lerp, .colour_changed = changed,
                                            .alpha_from_texture = alpha});
                    };
                    site.draws.push_back({.programs = {PixelProgram::kTextureVertexX4},
                                          .fvfs = {kFvfXyz | kFvfDiffuse | kFvfTex1}});
                    sites.push_back(site);
                }
            }
        }
    }
}

void reset_coords0(TssState& state) { core::at(state, 0).coords = {.set = 0}; }

void modulate_both(TssState& state) { set_ops(state, 0, {.colour = StageOp::kModulate, .alpha = StageOp::kModulate}); }

// The draws with no pixel shader (the stages as each site sets them, over what was left).
void add_stage_sites(std::vector<Site>& sites)
{
    const uint32_t colour = kFvfXyz | kFvfDiffuse;
    const uint32_t colour_uv = colour | kFvfTex1;
    const uint32_t uv = kFvfXyz | kFvfTex1;
    sites.push_back({.where = "glow.cpp 0x004E9F80: SELECTARG2 x2, stage 1 off; MODULATE x2 after",
                     .before = [](TssState& state) {
                         set_ops(state, 0, {.colour = StageOp::kSelectArg2, .alpha = StageOp::kSelectArg2});
                         ops(state, 1).colour_op = StageOp::kDisable;
                     },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {colour}}}});
    sites.push_back({.where = "glow.cpp, pc_viewport.cpp, screen_fx.cpp, shadow_base.cpp: the empty slots 0x94C / 0x950",
                     .before = {},
                     .after = {},
                     .draws = {{.programs = stages_only(), .fvfs = {colour, kFvfXyzRhw | kFvfDiffuse, kScreenFvf, kFvfXyz}}}});
    sites.push_back({.where = "screen_fx.cpp 0x004E4D60: stage 0's coordinates reset; the bloom cut-off",
                     .before = reset_coords0,
                     .after = {},
                     .draws = {{.programs = {PixelProgram::kStages, PixelProgram::kBloomCutoff}, .fvfs = {kScreenFvf}}}});
    for (const bool textured : {false, true}) {
        sites.push_back({.where = "billboard_quad.cpp open_sketch: MODULATE2X / SELECTARG2, arg2 CURRENT; MODULATE after",
                         .before = [textured](TssState& state) {
                             const StageOp op = textured ? StageOp::kModulate2x : StageOp::kSelectArg2;
                             set_ops(state, 0, {.colour = op, .alpha = op});
                             ops(state, 0).colour_arg2 = kCurrent;
                             ops(state, 0).alpha_arg2 = kCurrent;
                         },
                         .after = modulate_both,
                         .draws = {{.programs = stages_only(), .fvfs = {kScreenFvf}}}});
    }
    sites.push_back({.where = "text.cpp 0x004E0E10: MODULATE2X(TEXTURE, CURRENT) x2, stage 1 off; MODULATE after",
                     .before = [](TssState& state) {
                         ops(state, 0) = {.colour_op = StageOp::kModulate2x, .colour_arg0 = ops(state, 0).colour_arg0,
                                      .colour_arg1 = kTexture, .colour_arg2 = kCurrent, .alpha_op = StageOp::kModulate2x,
                                      .alpha_arg0 = ops(state, 0).alpha_arg0, .alpha_arg1 = kTexture, .alpha_arg2 = kCurrent};
                         ops(state, 1).colour_op = StageOp::kDisable;
                     },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {kScreenFvf}}}});
    sites.push_back({.where = "draw_frame.cpp 0x004E4710: SELECTARG1(TEXTURE), alpha DISABLE",
                     .before = [](TssState& state) {
                         set_ops(state, 0, {.colour = StageOp::kSelectArg1, .alpha = StageOp::kDisable});
                         ops(state, 0).colour_arg1 = kTexture;
                     },
                     .after = {},
                     .draws = {{.programs = stages_only(), .fvfs = {kScreenFvf}}}});
    sites.push_back({.where = "editor_support.cpp, flash.cpp, loading_bar.cpp: the stages as left",
                     .before = {},
                     .after = {},
                     .draws = {{.programs = stages_only(), .fvfs = {kScreenFvf, kFvfXyzRhw | kFvfDiffuse}}}});
    sites.push_back({.where = "movie.cpp 0x004E6D00: MODULATE2X(TEXTURE, CURRENT), alpha SELECTARG2; MODULATE after",
                     .before = [](TssState& state) {
                         set_ops(state, 0, {.colour = StageOp::kModulate2x, .alpha = StageOp::kSelectArg2});
                         ops(state, 0).colour_arg1 = kTexture;
                         ops(state, 0).colour_arg2 = kCurrent;
                         ops(state, 1).colour_op = StageOp::kDisable;
                     },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {kScreenFvf}}}});
    sites.push_back({.where = "loading_console_panel.cpp: MODULATE(TEXTURE, DIFFUSE) x2, coordinates reset, stage 1 off",
                     .before = [](TssState& state) {
                         ops(state, 0) = {.colour_op = StageOp::kModulate, .colour_arg0 = ops(state, 0).colour_arg0,
                                      .colour_arg1 = kTexture, .colour_arg2 = kDiffuse, .alpha_op = StageOp::kModulate,
                                      .alpha_arg0 = ops(state, 0).alpha_arg0, .alpha_arg1 = kTexture, .alpha_arg2 = kDiffuse};
                         reset_coords0(state);
                         set_ops(state, 1, {.colour = StageOp::kDisable, .alpha = StageOp::kDisable});
                     },
                     .after = {},
                     .draws = {{.programs = stages_only(), .fvfs = {kScreenFvf}, .fog = false}}});
    sites.push_back({.where = "loading_screen.cpp 0x004E7B20: SELECTARG1(TEXTURE) x2",
                     .before = [](TssState& state) {
                         set_ops(state, 0, {.colour = StageOp::kSelectArg1, .alpha = StageOp::kSelectArg1});
                         ops(state, 0).colour_arg1 = kTexture;
                         ops(state, 0).alpha_arg1 = kTexture;
                     },
                     .after = {},
                     .draws = {{.programs = stages_only(), .fvfs = {kFvfXyzRhw | kFvfTex1, kFvfXyzRhw | kFvfDiffuse}}}});
    sites.push_back({.where = "pc_weather.cpp 0x004EB3B0: the rain's lines, SELECTARG2 x2; MODULATE after",
                     .before = [](TssState& state) { set_ops(state, 0, {.colour = StageOp::kSelectArg2, .alpha = StageOp::kSelectArg2}); },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {colour}}}});
    sites.push_back({.where = "pc_weather.cpp 0x004EB6A0: the snow's point sprites, MODULATE2X x2; MODULATE after",
                     .before = [](TssState& state) { set_ops(state, 0, {.colour = StageOp::kModulate2x, .alpha = StageOp::kModulate2x}); },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {colour}, .points = true, .point_scale = true,
                                .point_sprite = true}}});
    sites.push_back({.where = "pc_weather.cpp 0x004EBA50: the splashes, MODULATE2X x2; MODULATE after",
                     .before = [](TssState& state) { set_ops(state, 0, {.colour = StageOp::kModulate2x, .alpha = StageOp::kModulate2x}); },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {colour_uv}}}});
    sites.push_back({.where = "primitive_states.cpp: SELECTARG1 x2, stage 0's coordinates reset; MODULATE after",
                     .before = [](TssState& state) {
                         set_ops(state, 0, {.colour = StageOp::kSelectArg1, .alpha = StageOp::kSelectArg1});
                         reset_coords0(state);
                     },
                     .after = modulate_both,
                     .draws = {{.programs = stages_only(), .fvfs = {uv}}}});
    sites.push_back({.where = "shadow_base.cpp 0x004DBB40: TFACTOR / TEXTURE, coordinates reset; MODULATE(TEXTURE) after",
                     .before = [](TssState& state) {
                         reset_coords0(state);
                         set_ops(state, 0, {.colour = StageOp::kSelectArg1, .alpha = StageOp::kSelectArg1});
                         ops(state, 0).colour_arg1 = kFactor;
                         ops(state, 0).alpha_arg1 = kTexture;
                         ops(state, 1).colour_op = StageOp::kDisable;
                     },
                     .after = [](TssState& state) {
                         modulate_both(state);
                         ops(state, 0).colour_arg1 = kTexture;
                         ops(state, 0).alpha_arg1 = kTexture;
                         ops(state, 1).colour_op = StageOp::kDisable;
                     },
                     .draws = {{.programs = stages_only(), .fvfs = {uv}}}});
    sites.push_back({.where = "device.cpp: stage 0's coordinates reset", .before = reset_coords0, .after = {}, .draws = {}});
}

std::vector<Site> all_sites()
{
    std::vector<Site> sites;
    add_material_sites(sites);
    add_particle_sites(sites);
    add_stage_sites(sites);
    return sites;
}

// The closure: every stage state a site can leave behind, from the device's defaults, and each site's states while it
// draws (its own settings over every state left).
std::vector<std::unordered_set<TssState, TssHash>> draw_states(const std::vector<Site>& sites, std::size_t& reached)
{
    std::unordered_set<TssState, TssHash> left;
    std::deque<TssState> queue;
    std::vector<std::unordered_set<TssState, TssHash>> drawing(sites.size());
    left.insert(device_defaults());
    queue.push_back(device_defaults());
    while (!queue.empty()) {
        const TssState state = queue.front();
        queue.pop_front();
        for (std::size_t index = 0; index < sites.size(); ++index) {
            const Site& site = core::at(sites, index);
            TssState next = state;
            if (site.before) {
                site.before(next);
            }
            if (!site.draws.empty()) {
                core::at(drawing, index).insert(next);
            }
            if (site.after) {
                site.after(next);
            }
            if (left.insert(next).second) {
                queue.push_back(next);
            }
        }
    }
    reached = left.size();
    return drawing;
}

// The modes of a material's passes 1-3 and its alpha flags.
struct MaterialShape {
    std::array<uint8_t, 4> modes{};
    uint8_t alpha_flags = 0;
};

void add_shapes(std::vector<MaterialShape>& shapes, std::array<uint8_t, 4> modes, uint32_t passes)
{
    for (uint32_t flags = 0; flags < (1U << passes); ++flags) {
        shapes.push_back({.modes = modes, .alpha_flags = static_cast<uint8_t>(flags)});
    }
}

// Every material shape of `passes` passes: all modes at every pass for two and three passes; for four, every mode at
// each pass with the others none, and every combination of the modes that read r1's or the pass before's alpha (or
// with all_materials every combination).
std::vector<MaterialShape> material_shapes(uint32_t passes, bool all_materials)
{
    std::vector<MaterialShape> shapes;
    if (passes < 4 || all_materials) {
        uint32_t combinations = 1;
        for (uint32_t pass = 1; pass < passes; ++pass) {
            combinations *= kModeCount;
        }
        for (uint32_t combination = 0; combination < combinations; ++combination) {
            std::array<uint8_t, 4> modes{};
            uint32_t rest = combination;
            for (uint32_t pass = 1; pass < passes; ++pass) {
                core::at(modes, pass) = static_cast<uint8_t>(rest % kModeCount);
                rest /= kModeCount;
            }
            add_shapes(shapes, modes, passes);
        }
        return shapes;
    }
    for (uint32_t pass = 1; pass < passes; ++pass) {
        for (uint8_t mode = 0; mode < kModeCount; ++mode) {
            std::array<uint8_t, 4> modes{};
            core::at(modes, pass) = mode;
            add_shapes(shapes, modes, passes);
        }
    }
    static constexpr std::array<uint8_t, 6> kInteracting = {1, 3, 5, 9, 0xC, 0xD};
    for (const uint8_t first : kInteracting) {
        for (const uint8_t second : kInteracting) {
            for (const uint8_t third : kInteracting) {
                add_shapes(shapes, {0, first, second, third}, passes);
            }
        }
    }
    return shapes;
}

// The vertex half's states besides the stages: lighting (sticky: on for the lit instances, left on or off around
// them), with or without the vertex colour sources, specular and the local viewer; fog off or the game's (linear,
// by range).
struct VertexVariant {
    bool lighting = false;
    bool colour_vertex = false;
    bool specular = false;
    bool local_viewer = false;
};

std::vector<VertexVariant> vertex_variants()
{
    std::vector<VertexVariant> variants = {{}};
    for (const bool colour_vertex : {false, true}) {
        for (const bool specular : {false, true}) {
            for (const bool local_viewer : {false, true}) {
                variants.push_back({.lighting = true, .colour_vertex = colour_vertex, .specular = specular,
                                    .local_viewer = local_viewer});
            }
        }
    }
    return variants;
}

class Collector {
public:
    explicit Collector(bool all_materials)
    {
        for (uint32_t passes = 2; passes <= kMaxPasses; ++passes) {
            core::at(shapes_, passes) = material_shapes(passes, all_materials);
        }
    }

    void site_draw(const TssState& state, const Draw& draw, PixelProgram program)
    {
        for (const bool fog : {false, true}) {
            if (fog && !draw.fog) {
                continue;
            }
            const FragmentKey fragment = fragment_for(state, draw, program, fog);
            add_fragments(fragment, draw, fog);
            add_vertices(state, draw, fragment, fog);
        }
    }

    KeySets& sets() { return sets_; }

    static FragmentKey fragment_for(const TssState& state, const Draw& draw, PixelProgram program, bool fog)
    {
        FragmentKey key;
        key.program = program;
        key.material_passes = program == PixelProgram::kMaterial ? draw.passes : 0;
        key.fog = fog;
        key.point_sprite = draw.point_sprite;
        for (std::size_t stage = 0; stage < kStageCount; ++stage) {
            core::at(key.stages, stage) = core::at(state, stage).ops;
        }
        return key;
    }

private:
    static VertexKey vertex_for(const TssState& state, const Draw& draw, uint32_t fvf, const VertexVariant& variant,
                                bool fog)
    {
        VertexKey key;
        key.fvf = fvf;
        key.lighting = variant.lighting;
        key.colour_vertex = variant.colour_vertex;
        key.specular = variant.specular;
        key.local_viewer = variant.local_viewer;
        key.light_count = kLights;
        // render_state.cpp apply_render_state: diffuse and ambient from the vertex colour, specular from the second
        // colour, emissive from the material.
        key.diffuse_source = render::vulkan::ColourSource::kColour1;
        key.ambient_source = render::vulkan::ColourSource::kColour1;
        key.specular_source = render::vulkan::ColourSource::kColour2;
        key.emissive_source = render::vulkan::ColourSource::kMaterial;
        key.fog = fog ? FogMode::kLinear : FogMode::kNone; // FOGVERTEXMODE LINEAR, RANGEFOGENABLE, no table fog
        key.range_fog = fog;
        key.points = draw.points;
        key.point_scale = draw.point_scale;
        for (std::size_t stage = 0; stage < kStageCount; ++stage) {
            core::at(key.coords, stage) = core::at(state, stage).coords;
        }
        return key;
    }

    void add_pair(const ShaderKey& raw)
    {
        ++sets_.raw_keys;
        const ShaderKey key = render::vulkan::normalised(raw);
        if (!render::vulkan::supported(key, nullptr)) {
            ++sets_.unsupported;
            return;
        }
        const bool new_vertex = sets_.vertex.insert(key.vertex).second;
        const bool new_fragment = sets_.fragment.insert(key.fragment).second;
        if (new_vertex || new_fragment) {
            sets_.pairs.push_back(key);
        }
    }

    // A fragment key, and for a ps.1.1 program its unlit twin too (the level editor's Unlit view mode: render_pc/
    // view_mode.cpp).
    void add_with_twin(const VertexKey& vertex, const FragmentKey& key)
    {
        add_pair({.vertex = vertex, .fragment = key});
        if (key.program != PixelProgram::kStages) {
            FragmentKey twin = key;
            twin.unlit = 1;
            add_pair({.vertex = vertex, .fragment = twin});
        }
    }

    // The fragment half's own variants: the alpha test (off, or GREATEREQUAL: render_state.cpp), the specular add
    // (D3DRS_SPECULARENABLE, sticky), a material's shapes, and the ps.1.1 programs' unlit twins.
    void add_fragments(const FragmentKey& base, const Draw& draw, bool fog)
    {
        VertexKey vertex;
        vertex.fvf = core::at(draw.fvfs, 0);
        vertex.points = draw.points;
        vertex.fog = fog ? FogMode::kLinear : FogMode::kNone;
        for (const CompareFunc test : {CompareFunc::kOff, CompareFunc::kGreaterEqual}) {
            for (const bool specular : {false, true}) {
                FragmentKey key = base;
                key.alpha_test = test;
                key.specular_add = specular;
                if (key.program != PixelProgram::kMaterial) {
                    add_with_twin(vertex, key);
                    continue;
                }
                // (a material's shader doesn't read the stage states: its shapes once per pass count and fog)
                if (!materials_done_.insert((key.material_passes * 2U) + (fog ? 1U : 0U) + (specular ? kSpecularBit : 0U) +
                                            (test == CompareFunc::kOff ? 0U : kAlphaTestBit))
                         .second) {
                    continue;
                }
                for (const MaterialShape& shape : core::at(shapes_, key.material_passes)) {
                    key.material_modes = shape.modes;
                    key.material_alpha_flags = shape.alpha_flags;
                    add_with_twin(vertex, key);
                }
            }
        }
    }

    void add_vertices(const TssState& state, const Draw& draw, const FragmentKey& fragment, bool fog)
    {
        FragmentKey shown = fragment;
        if (shown.program == PixelProgram::kMaterial) {
            shown.material_modes = core::at(shapes_, shown.material_passes).front().modes;
        }
        for (const uint32_t fvf : draw.fvfs) {
            for (const VertexVariant& variant : variants_) {
                if (variant.lighting && render::vulkan::fvf_pre_transformed(fvf)) {
                    continue;
                }
                add_pair({.vertex = vertex_for(state, draw, fvf, variant, fog), .fragment = shown});
            }
        }
    }

    KeySets sets_;
    std::unordered_set<uint32_t> materials_done_;
    std::array<std::vector<MaterialShape>, kMaxPasses + 1> shapes_{};
    std::vector<VertexVariant> variants_ = vertex_variants();
};

// The part of `state` a draw's shaders read: the normalised operations and the sampled stages' coordinates.
TssState what_is_read(const TssState& state, const Draw& draw, PixelProgram program)
{
    ShaderKey key;
    key.fragment = Collector::fragment_for(state, draw, program, false);
    key.vertex.points = draw.points;
    key = render::vulkan::normalised(key);
    const uint32_t sampled = draw.point_sprite ? 0U : render::vulkan::sampled_stages(key.fragment);
    TssState read{};
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        core::at(read, stage).ops = core::at(key.fragment.stages, stage);
        if ((sampled & (1U << stage)) != 0U) {
            core::at(read, stage).coords = core::at(state, stage).coords;
        }
    }
    return read;
}

} // namespace

KeySets measured_keys(bool all_materials)
{
    const std::vector<Site> sites = all_sites();
    std::size_t reached = 0;
    const auto drawing = draw_states(sites, reached);
    Collector collector(all_materials);
    for (std::size_t index = 0; index < sites.size(); ++index) {
        for (const Draw& draw : core::at(sites, index).draws) {
            for (const PixelProgram program : draw.programs) {
                // Only what the draw's shaders read of each state: its stages' operations (normalised) and the
                // coordinates of the stages it samples - far fewer than the states.
                std::unordered_set<TssState, TssHash> seen;
                for (const TssState& state : core::at(drawing, index)) {
                    const TssState read = what_is_read(state, draw, program);
                    if (seen.insert(read).second) {
                        collector.site_draw(read, draw, program);
                    }
                }
            }
        }
    }
    KeySets& sets = collector.sets();
    sets.sites = sites.size();
    sets.tss_states = reached;
    return std::move(sets);
}

} // namespace ff_check
