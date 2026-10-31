// NOT ORIGINAL - typed bridge for the optional QuickJS script runtime.
#pragma once
#include "thug2/js_value.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thug2::js {
// Initial bridge subset: retain QB type and raw bits, including float32 -0.
struct Value { std::uint32_t type, bits; bool operator==(const Value&) const = default; };
struct Field { std::uint32_t name; Value value; bool operator==(const Field&) const = default; };
using Parameters = std::vector<Field>;
struct HostResult { bool condition=true; Parameters outputs; std::optional<std::uint32_t> frames; };
using Host = std::function<HostResult(const Parameters&, const Parameters&)>;
struct Snapshot { std::string status; Parameters parameters; std::string message; };
struct RichHostResult { bool condition=true; RichParameters outputs; std::optional<std::uint32_t> frames; };
using RichHost = std::function<RichHostResult(const RichParameters&, const RichParameters&)>;
struct RichSnapshot { std::string status; RichParameters parameters; std::string message; };
std::uint32_t checksum(std::string_view name);
class Runtime {
public:
    explicit Runtime(std::size_t memory_limit=16*1024*1024, std::size_t interrupt_budget=1000);
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    void bind(std::uint32_t name, Host host);
    // One binding per CRC: latest scalar/rich registration replaces the old
    // one. A native task can invoke only hosts of its own transport mode.
    void bindRich(std::uint32_t name, RichHost host);
    // Only Wait <Integer> gameframe(s)/game; other wait modes reject.
    void install_frame_wait();
    // Replaces Wait with the rich-mode equivalent; choose one mode per CRC.
    void installRichFrameWait();
    void load(std::string_view javascript, std::string_view filename);
    std::uint32_t start(std::uint32_t script, const Parameters& parameters={}, std::uint32_t fuel=10000);
    Snapshot update(std::uint32_t task, std::uint32_t frame);
    std::uint32_t startRich(std::uint32_t script, const RichParameters& parameters={}, std::uint32_t fuel=10000);
    RichSnapshot updateRich(std::uint32_t task, std::uint32_t frame);
    bool cancel(std::uint32_t task);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace thug2::js
