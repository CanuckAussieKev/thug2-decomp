#pragma once

// NOT ORIGINAL - thug2_maths_check's corpus: the 32-bit build writes every checked function's inputs and results, bit
// for bit, into a file; the 64-bit build reads the same inputs back, works the functions out again and compares its
// results with the 32-bit build's (docs/design/x64-port.md "Floating point"). One sequence of values, written and
// read in the same order by the same code: a case draws an input (`in`: written, or read back in place of the drawn
// one) and hands over a result (`out`: written, or compared). Sections mark each case's start, so a corpus from other
// code stops the comparison instead of misreading it.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace tests {

// The distance in ulps between two doubles / floats (NaNs equal to each other; a NaN against a number: UINT64_MAX).
uint64_t ulps(double a, double b);
uint64_t ulps(float a, float b);

// One function's comparison: how many results differed from the corpus, by how much (ulps of the stored type, or the
// difference of integers, or 1 for a disagreeing truth value) and where.
class CorpusTally {
public:
    CorpusTally(std::string name, char unit) : name_(std::move(name)), unit_(unit) {}
    struct Sample {
        uint64_t distance;
        double difference;
        double input;
    };
    void add(Sample sample);
    void print() const;
    [[nodiscard]] uint64_t differing() const { return differing_; }

private:
    std::string name_;
    char unit_; // 'f' float ulps, 'd' double ulps, 'b' disagreeing truth values, 'u' integer difference
    uint64_t runs_ = 0;
    uint64_t differing_ = 0;
    uint64_t unmatched_ = 0; // a NaN against a number
    uint64_t max_ulps_ = 0;
    double sum_ulps_ = 0.0;
    double max_abs_ = 0.0;
    double worst_input_ = 0.0;
};

// The values a corpus holds.
template <typename Value>
concept CorpusValue = std::same_as<Value, float> || std::same_as<Value, double> || std::same_as<Value, bool> ||
                      std::same_as<Value, int32_t> || std::same_as<Value, uint32_t> || std::same_as<Value, int64_t> ||
                      std::same_as<Value, uint64_t>;

class Corpus {
public:
    enum class Mode : uint8_t { kWrite, kCompare };
    // What the corpus was written with (its header).
    struct Header {
        uint32_t count;
        uint64_t seed;
        uint32_t precision_bits; // the x87 precision the 32-bit build ran at: 53 or 24
    };

    // A corpus to write (the 32-bit build).
    static Corpus to_write(const Header& header);
    // The corpus in `bytes` to compare with; false (and a message) when it isn't one.
    static bool to_compare(std::vector<uint8_t> bytes, Corpus* out);
    // The written corpus, to save.
    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return bytes_; }
    [[nodiscard]] const Header& header() const { return header_; }
    [[nodiscard]] bool comparing() const { return mode_ == Mode::kCompare; }
    // Whether the comparison read the whole corpus in step (no misplaced section, nothing short or left over).
    [[nodiscard]] bool in_step() const { return in_step_ && (!comparing() || at_ == bytes_.size()); }
    // Inputs drawn here that weren't the stored ones (the two builds' random draws differ: 0 expected).
    [[nodiscard]] uint64_t drifted_inputs() const { return drifted_; }

    // A case's start: its name's hash written, or checked.
    void section(std::string_view name);

    // An input: `drawn` written and returned; when comparing, the stored input returned (`drawn` only counted when
    // it differs).
    template <CorpusValue Value>
    Value in(Value drawn)
    {
        if (!comparing()) {
            put(drawn);
            return drawn;
        }
        const auto stored = take<Value>();
        if (bytes_of(stored) != bytes_of(drawn)) {
            ++drifted_;
        }
        return stored;
    }
    // A result: written, or compared with the stored one into `tally` (`input` names the case in the report).
    template <CorpusValue Value>
    void out(CorpusTally& tally, Value value, double input)
    {
        if (!comparing()) {
            put(value);
            return;
        }
        const auto stored = take<Value>();
        tally.add(sample(value, stored, input));
    }
    template <CorpusValue Value>
    void outs(CorpusTally& tally, std::span<const Value> values, double input)
    {
        for (const Value value : values) {
            out(tally, value, input);
        }
    }

private:
    Corpus(Mode mode, const Header& header) : mode_(mode), header_(header) {}
    // A value's bytes (as the processor stores it: both builds are little-endian).
    template <CorpusValue Value>
    static std::array<uint8_t, sizeof(Value)> bytes_of(Value value)
    {
        return std::bit_cast<std::array<uint8_t, sizeof(Value)>>(value);
    }
    template <CorpusValue Value>
    void put(Value value)
    {
        const auto raw = bytes_of(value);
        bytes_.insert(bytes_.end(), raw.begin(), raw.end());
    }
    template <CorpusValue Value>
    Value take()
    {
        if (!in_step_ || bytes_.size() - at_ < sizeof(Value)) {
            in_step_ = false;
            return Value{};
        }
        std::array<uint8_t, sizeof(Value)> raw{};
        const auto from = std::span<const uint8_t>(bytes_).subspan(at_, sizeof(Value));
        std::ranges::copy(from, raw.begin());
        at_ += sizeof(Value);
        return std::bit_cast<Value>(raw);
    }
    template <CorpusValue Value>
    static CorpusTally::Sample sample(Value value, Value stored, double input)
    {
        if constexpr (std::is_floating_point_v<Value>) {
            const uint64_t distance = ulps(value, stored);
            return {.distance = distance,
                    .difference = distance == UINT64_MAX ? INFINITY : std::fabs(static_cast<double>(value) - stored),
                    .input = input};
        } else if constexpr (std::same_as<Value, bool>) {
            return {.distance = static_cast<uint64_t>(value != stored),
                    .difference = static_cast<double>(value != stored),
                    .input = input};
        } else {
            const uint64_t distance = value > stored ? static_cast<uint64_t>(value) - static_cast<uint64_t>(stored)
                                                     : static_cast<uint64_t>(stored) - static_cast<uint64_t>(value);
            return {.distance = distance, .difference = static_cast<double>(distance), .input = input};
        }
    }

    Mode mode_;
    Header header_;
    std::vector<uint8_t> bytes_;
    size_t at_ = 0;
    bool in_step_ = true;
    uint64_t drifted_ = 0;
};

// A case's tallies, printed under its heading when it ends (when comparing).
class CorpusGroup {
public:
    CorpusGroup(Corpus& corpus, std::string_view heading) : corpus_(&corpus), heading_(heading) {}
    CorpusGroup(const CorpusGroup&) = delete;
    CorpusGroup& operator=(const CorpusGroup&) = delete;
    CorpusGroup(CorpusGroup&&) = delete;
    CorpusGroup& operator=(CorpusGroup&&) = delete;
    ~CorpusGroup();
    CorpusTally& tally(std::string name, char unit) { return tallies_.emplace_back(std::move(name), unit); }

private:
    Corpus* corpus_;
    std::string heading_;
    std::deque<CorpusTally> tallies_;
};

// A corpus file read whole / written (false, and a message on stderr, when it can't be).
bool load_corpus_file(const char* path, std::vector<uint8_t>* bytes);
bool save_corpus_file(const char* path, std::span<const uint8_t> bytes);

// How many inputs each case draws, and the seed they are drawn from.
struct CorpusDraws {
    int count;
    uint64_t seed;
};

// The corpus's cases outside maths_check.cpp: the C runtime and base/maths (the game's own maths), and D3DX9 (the DLL
// against its other build) - maths_corpus_cases.cpp.
void corpus_base_maths(Corpus& corpus, CorpusDraws draws);
void corpus_d3dx(Corpus& corpus, CorpusDraws draws);

} // namespace tests
