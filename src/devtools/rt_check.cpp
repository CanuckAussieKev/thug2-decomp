// NOT ORIGINAL - ray tracing's contract checked: see rt_check.h.

#include "devtools/rt_check.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/ext/extension.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/light_sets.h"
#include "engine/gfx/rt_checks.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"
#include "platform/run_log.h"

namespace devtools::rt_check {

namespace rt = render::rt;

namespace {

constexpr uint64_t kSummaryFrames = 600; // a summary line this often
constexpr int kLoggedViolations = 200;   // violations logged in full; the rest counted
constexpr std::size_t kMaxSceneLights = 31;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr float kPerByteLight = 1.0F / 128.0F;
constexpr float kDirectionTolerance = 1.0e-4F;
constexpr float kColourTolerance = 1.0e-4F;
constexpr std::size_t kFloatTextBytes = 32; // the longest float to_chars writes, and more
constexpr uint32_t kFirstAddress = 1;       // a sampler's address modes: wrap ...
constexpr uint32_t kLastAddress = 5;        // ... mirror once
constexpr uint32_t kLastFilter = 3;         // its filters: none, point, linear, anisotropic

// A float as JSON (finite: the shortest text that reads back; else null).
void put_float(std::string& out, float value)
{
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    std::array<char, kFloatTextBytes> text{};
    const auto [end, error] = std::to_chars(text.data(), std::to_address(text.end()), value);
    out.append(text.data(), error == std::errc{} ? end : text.data());
}

template <std::size_t N>
void put_floats(std::string& out, const std::array<float, N>& values)
{
    out += '[';
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) {
            out += ',';
        }
        put_float(out, values.at(i));
    }
    out += ']';
}

// ,"name":value (`first`: without the comma).
void put_number(std::string& out, const char* name, uint64_t value, bool first = false)
{
    out += first ? "\"" : ",\"";
    out += name;
    out += "\":";
    out += std::to_string(value);
}
void put_flag(std::string& out, const char* name, bool value)
{
    out += ",\"";
    out += name;
    out += "\":";
    out += value ? "true" : "false";
}

template <std::size_t Rows>
void put_matrix(std::string& out, const std::array<std::array<float, 4>, Rows>& rows)
{
    out += '[';
    for (std::size_t i = 0; i < Rows; ++i) {
        if (i > 0) {
            out += ',';
        }
        put_floats(out, rows.at(i));
    }
    out += ']';
}

const char* light_kind(rt::Light::Kind kind)
{
    switch (kind) {
    case rt::Light::Kind::kAmbient:
        return "ambient";
    case rt::Light::Kind::kDirectional:
        return "directional";
    case rt::Light::Kind::kPoint:
        return "point";
    }
    return "?";
}

bool finite_matrix(const std::array<std::array<float, 4>, 4>& matrix)
{
    return std::ranges::all_of(matrix, [](const auto& row) {
        return std::ranges::all_of(row, [](float value) { return std::isfinite(value); });
    });
}

bool near_value(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// The checking backend.
class Checker final : public rt::RayTracing {
public:
    // The backend it sits in front of (null: none) - the registrations made before it are taken on trust (adopted).
    void set_inner(rt::RayTracing* inner, bool adopt)
    {
        inner_ = inner;
        adopt_ = adopt;
    }
    [[nodiscard]] rt::RayTracing* inner() const { return inner_; }

    [[nodiscard]] bool available() const override { return true; }

    void add_mesh(rt::MeshId mesh, const rt::Geometry& geometry) override
    {
        ++totals_.mesh_adds;
        if (mesh == 0) {
            violation("add_mesh with id 0");
        } else if (meshes_.contains(mesh)) {
            violation("add_mesh " + std::to_string(mesh) + ": added twice");
        } else if (removed_meshes_.contains(mesh)) {
            violation("add_mesh " + std::to_string(mesh) + ": an id used before (removed)");
        }
        check_geometry("add_mesh", mesh, geometry);
        meshes_[mesh] = geometry;
        if (inner_ != nullptr) {
            inner_->add_mesh(mesh, geometry);
        }
    }

    void remove_mesh(rt::MeshId mesh) override
    {
        ++totals_.mesh_removes;
        if (!meshes_.contains(mesh)) {
            violation("remove_mesh " + std::to_string(mesh) + mesh_absence(mesh));
        }
        meshes_.erase(mesh);
        removed_meshes_.insert(mesh);
        if (inner_ != nullptr) {
            inner_->remove_mesh(mesh);
        }
    }

    void mesh_changed(rt::MeshId mesh) override
    {
        ++totals_.mesh_changes;
        if (!known_mesh(mesh)) {
            violation("mesh_changed " + std::to_string(mesh) + mesh_absence(mesh));
        } else if (!meshes_.at(mesh).dynamic) {
            ++totals_.static_changes; // (moved by the editor, a mesh's own vertices rewritten: rare)
        }
        if (inner_ != nullptr) {
            inner_->mesh_changed(mesh);
        }
    }

    void update_mesh(rt::MeshId mesh, const rt::Geometry& geometry) override
    {
        ++totals_.mesh_updates;
        if (!known_mesh(mesh)) {
            violation("update_mesh " + std::to_string(mesh) + mesh_absence(mesh));
        }
        check_geometry("update_mesh", mesh, geometry);
        meshes_[mesh] = geometry;
        if (inner_ != nullptr) {
            inner_->update_mesh(mesh, geometry);
        }
    }

    void set_material(rt::MaterialId material, const rt::Material& surface) override
    {
        ++totals_.material_sets;
        if (material == 0) {
            violation("set_material with id 0");
        } else if (removed_materials_.contains(material)) {
            violation("set_material " + std::to_string(material) + ": used after removal");
        }
        const bool finite = std::ranges::all_of(surface.colour, [](float v) { return std::isfinite(v); }) &&
                            std::isfinite(surface.alpha_ref);
        if (!finite) {
            violation("set_material " + std::to_string(material) + ": a colour or alpha reference not finite");
        }
        if ((surface.flags & ~(rt::kAlphaTested | rt::kTransparent)) != 0U) {
            violation("set_material " + std::to_string(material) + ": flags other than alpha-tested / transparent");
        }
        const auto address_ok = [](uint32_t mode) { return mode >= kFirstAddress && mode <= kLastAddress; };
        if (!address_ok(surface.address_u) || !address_ok(surface.address_v) || surface.filter > kLastFilter) {
            violation("set_material " + std::to_string(material) + ": a sampler state out of range");
        }
        if (!(surface.alpha_ref >= 0.0F && surface.alpha_ref <= 1.0F)) {
            violation("set_material " + std::to_string(material) + ": an alpha reference outside 0..1");
        }
        materials_[material] = surface;
        if (inner_ != nullptr) {
            inner_->set_material(material, surface);
        }
    }

    void remove_material(rt::MaterialId material) override
    {
        ++totals_.material_removes;
        if (!materials_.contains(material)) {
            violation("remove_material " + std::to_string(material) +
                      (removed_materials_.contains(material) ? ": removed twice" : ": never given"));
        }
        materials_.erase(material);
        removed_materials_.insert(material);
        if (inner_ != nullptr) {
            inner_->remove_material(material);
        }
    }

    void begin_frame(const rt::FrameRecord& record) override
    {
        if (begun_ && record.frame <= frame_) {
            violation("begin_frame " + std::to_string(record.frame) + " after frame " + std::to_string(frame_));
        }
        begun_ = true;
        frame_ = record.frame;
        record_ = &record;
        ++totals_.frames;
        totals_.instances += record.instances.size();
        drawn_.clear();
        check_instances(record);
        check_lights(record);
        check_cameras(record);
        if (inner_ != nullptr) {
            inner_->begin_frame(record);
        }
        if (totals_.frames % kSummaryFrames == 0) {
            get_log("%s", summary().c_str());
        }
    }

    [[nodiscard]] bool wants_draws() const override { return true; }

    void mesh_drawn(rt::MeshId mesh, const rt::Rows& object_to_world) override
    {
        ++totals_.draws;
        if (!begun_) {
            violation("a mesh drawn before any begin_frame");
            return;
        }
        if (mesh == 0) {
            violation("frame " + std::to_string(frame_) + ": a mesh drawn in the world's passes that isn't registered");
            return;
        }
        if (!known_mesh(mesh)) {
            violation("frame " + std::to_string(frame_) + ": mesh " + std::to_string(mesh) + " drawn" +
                      mesh_absence(mesh));
            return;
        }
        const auto found = transforms_.find(mesh);
        if (found == transforms_.end()) {
            violation("frame " + std::to_string(frame_) + ": mesh " + std::to_string(mesh) +
                      " drawn but not in the frame's record");
            return;
        }
        const bool matched = std::ranges::any_of(found->second,
                                                 [&](const rt::Rows& rows) { return rt::same_rows(rows, object_to_world); });
        if (!matched) {
            violation("frame " + std::to_string(frame_) + ": mesh " + std::to_string(mesh) +
                      " drawn with a transform the record doesn't have (translation " +
                      std::to_string(core::at(core::at(object_to_world, 0), 3)) + ", " +
                      std::to_string(core::at(core::at(object_to_world, 1), 3)) + ", " +
                      std::to_string(core::at(core::at(object_to_world, 2), 3)) + ")");
            return;
        }
        if (drawn_.insert(mesh).second) {
            ++totals_.distinct_draws;
        }
    }

    [[nodiscard]] std::string summary() const
    {
        std::ostringstream out;
        const double frames = std::max<double>(1.0, static_cast<double>(totals_.frames));
        out << "rt-check: summary frames=" << totals_.frames << " violations=" << totals_.violations
            << " meshes=" << meshes_.size() << " materials=" << materials_.size()
            << " instances/frame=" << static_cast<uint64_t>(static_cast<double>(totals_.instances) / frames)
            << " draws/frame=" << static_cast<uint64_t>(static_cast<double>(totals_.draws) / frames)
            << " drawn-meshes/frame=" << static_cast<uint64_t>(static_cast<double>(totals_.distinct_draws) / frames)
            << " adds=" << totals_.mesh_adds << " removes=" << totals_.mesh_removes
            << " updates=" << totals_.mesh_updates << " changes=" << totals_.mesh_changes
            << " static-changes=" << totals_.static_changes
            << " material-sets=" << totals_.material_sets << " material-removes=" << totals_.material_removes
            << " adopted=" << totals_.adopted;
        if (record_ != nullptr) {
            out << " last-frame: instances=" << record_->instances.size() << " lights=" << record_->lights.size()
                << " cameras=" << record_->cameras.size();
        }
        for (const auto& [kind, count] : kinds_) {
            out << "\n  rt-check: " << count << " x " << kind;
        }
        return out.str();
    }

    // The last begun frame's record with the registered meshes and materials, as JSON.
    [[nodiscard]] std::string dump() const
    {
        std::string out = R"({"record":)";
        out += record_ != nullptr ? record_json(*record_) : "null";
        out += R"(,"meshes":[)";
        bool first = true;
        std::vector<rt::MeshId> ids;
        ids.reserve(meshes_.size());
        for (const auto& [id, geometry] : meshes_) {
            ids.push_back(id);
        }
        std::ranges::sort(ids);
        for (const rt::MeshId id : ids) {
            const rt::Geometry& geometry = meshes_.at(id);
            out += first ? "{" : ",{";
            first = false;
            put_number(out, "id", id, true);
            put_number(out, "vertex_offset", geometry.vertex_offset);
            put_number(out, "vertex_stride", geometry.vertex_stride);
            put_number(out, "vertex_count", geometry.vertex_count);
            put_number(out, "position_offset", geometry.position_offset);
            put_flag(out, "indexed", geometry.indices != nullptr);
            put_number(out, "index_offset", geometry.index_offset);
            put_number(out, "index_count", geometry.index_count);
            put_flag(out, "indices_32bit", geometry.indices_32bit);
            put_flag(out, "strip", geometry.strip);
            put_flag(out, "dynamic", geometry.dynamic);
            put_number(out, "vertex_buffer_bytes", geometry.vertex_buffer_bytes);
            put_number(out, "index_buffer_bytes", geometry.index_buffer_bytes);
            put_number(out, "triangles",
                       rt::triangle_count(geometry.indices != nullptr ? geometry.index_count : geometry.vertex_count,
                                          geometry.strip));
            out += '}';
        }
        out += R"(],"materials":[)";
        first = true;
        std::vector<rt::MaterialId> material_ids;
        material_ids.reserve(materials_.size());
        for (const auto& [id, surface] : materials_) {
            material_ids.push_back(id);
        }
        std::ranges::sort(material_ids);
        for (const rt::MaterialId id : material_ids) {
            const rt::Material& surface = materials_.at(id);
            out += first ? "{" : ",{";
            first = false;
            put_number(out, "id", id, true);
            put_flag(out, "textured", surface.texture != nullptr);
            put_number(out, "uv_set", surface.uv_set);
            put_number(out, "blend", surface.blend);
            out += R"(,"alpha_ref":)";
            put_float(out, surface.alpha_ref);
            out += R"(,"colour":)";
            put_floats(out, surface.colour);
            put_number(out, "flags", surface.flags);
            put_number(out, "address_u", surface.address_u);
            put_number(out, "address_v", surface.address_v);
            put_number(out, "filter", surface.filter);
            out += '}';
        }
        out += "]}\n";
        return out;
    }

private:
    struct Totals {
        uint64_t frames = 0;
        uint64_t instances = 0;
        uint64_t draws = 0;
        uint64_t distinct_draws = 0;
        uint64_t mesh_adds = 0;
        uint64_t mesh_removes = 0;
        uint64_t mesh_updates = 0;
        uint64_t mesh_changes = 0;
        uint64_t static_changes = 0;
        uint64_t material_sets = 0;
        uint64_t material_removes = 0;
        uint64_t adopted = 0;
        uint64_t violations = 0;
    };

    // A mesh known: registered - or, after the checker was put in front of a backend late, one it may have had.
    bool known_mesh(rt::MeshId mesh)
    {
        if (meshes_.contains(mesh)) {
            return true;
        }
        if (adopt_ && mesh != 0 && !removed_meshes_.contains(mesh)) {
            ++totals_.adopted;
            meshes_[mesh] = {};
            return true;
        }
        return false;
    }
    bool known_material(rt::MaterialId material)
    {
        if (materials_.contains(material)) {
            return true;
        }
        if (adopt_ && material != 0 && !removed_materials_.contains(material)) {
            ++totals_.adopted;
            materials_[material] = {};
            return true;
        }
        return false;
    }

    [[nodiscard]] std::string mesh_absence(rt::MeshId mesh) const
    {
        return removed_meshes_.contains(mesh) ? ": used after removal" : ": never added";
    }

    void check_geometry(const char* call, rt::MeshId mesh, const rt::Geometry& geometry)
    {
        const std::string problem = rt::geometry_problem(geometry);
        if (!problem.empty()) {
            violation(std::string(call) + " " + std::to_string(mesh) + ": " + problem);
        }
    }

    void check_instances(const rt::FrameRecord& record)
    {
        transforms_.clear();
        for (const rt::Instance& instance : record.instances) {
            std::string at = "frame ";
            at += std::to_string(record.frame);
            at += ": instance of mesh ";
            at += std::to_string(instance.mesh);
            if (!known_mesh(instance.mesh)) {
                violation(at + mesh_absence(instance.mesh));
            }
            if (instance.material != 0 && !known_material(instance.material)) {
                violation(at + ": material " + std::to_string(instance.material) +
                          (removed_materials_.contains(instance.material) ? " used after removal" : " never given"));
            }
            const std::string flags = rt::flags_problem(instance.flags);
            if (!flags.empty()) {
                violation(std::string(at).append(": flags ").append(flags));
            }
            if (!rt::finite_rows(instance.object_to_world)) {
                violation(at + ": a transform not finite");
            }
            std::vector<rt::Rows>& seen = transforms_[instance.mesh];
            if (std::ranges::any_of(seen, [&](const rt::Rows& rows) { return rows == instance.object_to_world; })) {
                violation(at + ": recorded twice with the same transform");
            }
            seen.push_back(instance.object_to_world);
        }
    }

    // The level's lights, read here from the light manager: one ambient colour, each directional light (its
    // direction negated, normalised; its colour times its intensity), each dynamic light that is on.
    void check_lights(const rt::FrameRecord& record)
    {
        const render::LightingState& world = render::light_sets().world_lighting();
        const std::size_t directional =
            std::min({static_cast<std::size_t>(render::light_sets().light_count()), world.directions.size(),
                      world.colours.size(), render::light_sets().intensities().size(), kMaxSceneLights});
        std::size_t points = 0;
        const auto dynamic = static_cast<std::size_t>(std::max(0, render::light_sets().dynamic_light_count()));
        for (std::size_t i = 0; i < dynamic && i < render::light_sets().dynamic_lights().size(); ++i) {
            const render::DynamicLightBase* light = core::at(render::light_sets().dynamic_lights(), i);
            if (light != nullptr && light->intensity() > 0.0F && light->owner() != 0U) {
                ++points;
            }
        }
        std::size_t ambient_seen = 0;
        std::size_t directional_seen = 0;
        std::size_t points_seen = 0;
        const std::string at = "frame " + std::to_string(record.frame) + ": ";
        for (const rt::Light& light : record.lights) {
            if (!std::ranges::all_of(light.colour, [](float v) { return std::isfinite(v) && v >= 0.0F; })) {
                violation(at + light_kind(light.kind) + " light with a colour not finite or negative");
            }
            switch (light.kind) {
            case rt::Light::Kind::kAmbient:
                ++ambient_seen;
                break;
            case rt::Light::Kind::kDirectional: {
                const std::size_t index = directional_seen++;
                if (index >= directional) {
                    break;
                }
                const maths::Vec4& towards = core::at(world.directions, index);
                const float length = std::sqrt((towards.x * towards.x) + (towards.y * towards.y) + (towards.z * towards.z));
                if (length > 0.0F) {
                    const bool same =
                        near_value(core::at(light.direction, 0), -towards.x / length, kDirectionTolerance) &&
                        near_value(core::at(light.direction, 1), -towards.y / length, kDirectionTolerance) &&
                        near_value(core::at(light.direction, 2), -towards.z / length, kDirectionTolerance);
                    if (!same) {
                        violation(at + "directional light " + std::to_string(index) +
                                  " isn't the level's (its direction negated, normalised)");
                    }
                }
                const uint32_t rgba = core::at(world.colours, index);
                const float factor = core::at(render::light_sets().intensities(), index) * kPerByteLight;
                for (std::size_t k = 0; k < 3; ++k) {
                    const float expected = static_cast<float>((rgba >> (kBitsPerByte * k)) & kByteMask) * factor;
                    if (!near_value(light.colour.at(k), expected, kColourTolerance * std::max(1.0F, expected))) {
                        violation(at + "directional light " + std::to_string(index) +
                                  " has another colour than the level's");
                        break;
                    }
                }
                break;
            }
            case rt::Light::Kind::kPoint:
                ++points_seen;
                if (!(light.radius >= 0.0F) || !std::isfinite(light.radius)) {
                    violation(at + "a point light's radius is not a size");
                }
                break;
            }
        }
        if (ambient_seen != 1) {
            violation(at + std::to_string(ambient_seen) + " ambient lights (1 expected)");
        }
        if (directional_seen != directional) {
            violation(at + std::to_string(directional_seen) + " directional lights, the level has " +
                      std::to_string(directional));
        }
        if (points_seen != points) {
            violation(at + std::to_string(points_seen) + " point lights, " + std::to_string(points) + " are on");
        }
    }

    void check_cameras(const rt::FrameRecord& record)
    {
        if (record.cameras.empty()) {
            violation("frame " + std::to_string(record.frame) + ": no camera at begin_frame");
        }
        for (const rt::Camera& camera : record.cameras) {
            if (!finite_matrix(camera.view) || !finite_matrix(camera.projection) ||
                !std::ranges::all_of(camera.position, [](float v) { return std::isfinite(v); })) {
                violation("frame " + std::to_string(record.frame) + ": camera of viewport " +
                          std::to_string(camera.viewport) + " not finite");
            }
        }
    }

    void violation(const std::string& text)
    {
        ++totals_.violations;
        // (the kind: the text with each run of digits as #, up to a parenthesis)
        std::string kind;
        for (const char ch : text.substr(0, text.find(" ("))) {
            const bool digit = ch >= '0' && ch <= '9';
            if (!digit) {
                kind += ch;
            } else if (!kind.ends_with('#')) {
                kind += '#';
            }
        }
        ++kinds_[kind];
        if (totals_.violations <= kLoggedViolations) {
            get_log("rt-check: VIOLATION %s", text.c_str());
        }
    }

    rt::RayTracing* inner_ = nullptr;
    bool adopt_ = false;
    bool begun_ = false;
    uint64_t frame_ = 0;
    const rt::FrameRecord* record_ = nullptr;
    std::unordered_map<rt::MeshId, rt::Geometry> meshes_;
    std::unordered_set<rt::MeshId> removed_meshes_;
    std::unordered_map<rt::MaterialId, rt::Material> materials_;
    std::unordered_set<rt::MaterialId> removed_materials_;
    std::unordered_map<rt::MeshId, std::vector<rt::Rows>> transforms_; // the frame's instances by mesh
    std::unordered_set<rt::MeshId> drawn_;                             // the meshes drawn this frame
    std::map<std::string, uint64_t> kinds_;
    Totals totals_;
};

Checker& checker()
{
    static core::NoDestroy<Checker> made;
    return made.get();
}

// Installed in front of whatever is installed now (again when that changed).
void keep_installed()
{
    rt::RayTracing* now = rt::ray_tracing();
    if (now == &checker()) {
        return;
    }
    const bool first = checker().inner() == nullptr && now == nullptr;
    checker().set_inner(now, now != nullptr);
    rt::set_ray_tracing(&checker());
    if (first) {
        get_log("rt-check: the contract check is on ([debug] rt_contract_check=1) - no ray-tracing backend behind it");
    } else {
        get_log("rt-check: the contract check put in front of the installed ray-tracing backend (its earlier "
                "registrations taken on trust)");
    }
}

class CheckExtension final : public ext::Extension {
public:
    void before_draw_world() override
    {
        if (asked()) {
            keep_installed();
        }
    }
    void scene_loaded(const char* name, bool /*is_net*/) override
    {
        if (asked() && rt::ray_tracing() == &checker()) {
            get_log("rt-check: level %s loaded - %s", name != nullptr ? name : "?", checker().summary().c_str());
        }
    }
};

std::string write_dump(const std::string& file)
{
    if (rt::ray_tracing() != &checker()) {
        return "no record: the contract check isn't on ([debug] rt_contract_check=1)";
    }
    const std::string path = game_folder_path(file.empty() ? "thug2-decomp-rt-frame.json" : file.c_str());
    const std::string text = checker().dump();
    const fs::OwnedFile out(fs::disk().open(path.c_str(), {.access = fs::Access::kWrite,
                                                           .sharing = fs::Sharing::kReadWriteDelete,
                                                           .disposition = fs::Disposition::kCreateAlways,
                                                           .hint = fs::Hint::kNormal}));
    if (!out) {
        return "can't write " + path;
    }
    uint32_t written = 0;
    fs::disk().write(out.get(), text.data(), static_cast<uint32_t>(text.size()), &written);
    get_log("rt-check: frame record written to %s (%u bytes)", path.c_str(), written);
    return "written: " + path;
}

} // namespace

bool asked()
{
    static const bool kOn = config_int("debug", "rt_contract_check", 0) != 0;
    return kOn;
}

std::string console_command(const std::string& arguments)
{
    std::istringstream in(arguments);
    std::string verb;
    std::string file;
    in >> verb >> file;
    if (verb == "dump") {
        return write_dump(file);
    }
    if (verb == "agree") {
        // (the baked sun's agreement at x z: rt_sun.h)
        const float x = std::strtof(file.c_str(), nullptr);
        float z = 0.0F;
        in >> z;
        const rt::BakedSun& sun = rt::baked_sun();
        const std::string text = "agreement at " + std::to_string(x) + " " + std::to_string(z) + ": " +
                                 std::to_string(rt::agreement_at(sun, {x, 0.0F, z})) + " (fit " + std::to_string(sun.scene) +
                                 ", " + std::to_string(sun.columns) + "x" + std::to_string(sun.rows) + ")";
        get_log("rt: %s", text.c_str());
        return text;
    }
    if (verb == "sun") {
        // (the shadow pass's sun set by hand: toward it; `rt sun off` gives it back to the pass)
        if (file == "off") {
            rt::set_sun_override(nullptr);
            get_log("rt: sun override off");
            return "sun: the pass's own";
        }
        std::array<float, 3> toward{};
        std::get<0>(toward) = std::strtof(file.c_str(), nullptr);
        in >> std::get<1>(toward) >> std::get<2>(toward);
        if (file.empty() || in.fail()) {
            std::array<float, 3> now{};
            return rt::sun_override(&now) ? "sun: " + std::to_string(std::get<0>(now)) + " " +
                                                std::to_string(std::get<1>(now)) + " " + std::to_string(std::get<2>(now))
                                          : "sun: the pass's own (rt sun <x> <y> <z> | rt sun off)";
        }
        rt::set_sun_override(&toward);
        get_log("rt: sun override %.4f %.4f %.4f", static_cast<double>(std::get<0>(toward)),
                static_cast<double>(std::get<1>(toward)), static_cast<double>(std::get<2>(toward)));
        return "sun set";
    }
    if (verb == "relit") {
        // (the relit mode, engine/gfx/rt_relit.h: on / off, its strength, the residual; no word: its state)
        rt::RelitSettings settings = rt::relit_settings();
        if (file == "on" || file == "off") {
            settings.on = file == "on";
        } else if (file == "strength") {
            float strength = settings.strength;
            in >> strength;
            settings.strength = strength;
        } else if (file == "residual") {
            int residual = settings.residual ? 1 : 0;
            in >> residual;
            settings.residual = residual != 0;
        } else if (!file.empty()) {
            return "rt relit [on | off | strength <0-1> | residual <0|1>]";
        }
        if (!file.empty()) {
            rt::set_relit_settings(settings);
            get_log("rt: relit %s", file.c_str());
        }
        const std::string text = rt::relit_status();
        get_log("rt: %s", text.c_str());
        return text;
    }
    if (verb == "summary" || verb.empty()) {
        if (rt::ray_tracing() != &checker()) {
            return "the contract check isn't on ([debug] rt_contract_check=1)";
        }
        const std::string text = checker().summary();
        get_log("%s", text.c_str());
        return text;
    }
    return "rt summary | rt dump [file] | rt sun [<x> <y> <z> | off] | rt agree <x> <z> | rt relit [on | off | strength <v> | residual <0|1>]";
}

std::string record_json(const rt::FrameRecord& record)
{
    std::string out = "{";
    put_number(out, "frame", record.frame, true);
    out += R"(,"instances":[)";
    for (std::size_t i = 0; i < record.instances.size(); ++i) {
        const rt::Instance& instance = record.instances.at(i);
        out += i > 0 ? ",{" : "{";
        put_number(out, "mesh", instance.mesh, true);
        put_number(out, "material", instance.material);
        put_number(out, "flags", instance.flags);
        out += R"(,"object_to_world":)";
        put_matrix(out, instance.object_to_world);
        out += '}';
    }
    out += R"(],"lights":[)";
    for (std::size_t i = 0; i < record.lights.size(); ++i) {
        const rt::Light& light = record.lights.at(i);
        out += i > 0 ? "," : "";
        out += R"({"kind":")";
        out += light_kind(light.kind);
        out += R"(","colour":)";
        put_floats(out, light.colour);
        out += R"(,"direction":)";
        put_floats(out, light.direction);
        out += R"(,"position":)";
        put_floats(out, light.position);
        out += R"(,"radius":)";
        put_float(out, light.radius);
        out += '}';
    }
    out += R"(],"cameras":[)";
    for (std::size_t i = 0; i < record.cameras.size(); ++i) {
        const rt::Camera& camera = record.cameras.at(i);
        out += i > 0 ? ",{" : "{";
        put_number(out, "viewport", camera.viewport, true);
        out += R"(,"view":)";
        put_matrix(out, camera.view);
        out += R"(,"projection":)";
        put_matrix(out, camera.projection);
        out += R"(,"position":)";
        put_floats(out, camera.position);
        out += '}';
    }
    out += "]}";
    return out;
}

ext::Extension& extension()
{
    static CheckExtension made;
    return made;
}

} // namespace devtools::rt_check
