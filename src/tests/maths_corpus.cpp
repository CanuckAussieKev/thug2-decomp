#include "tests/maths_corpus.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/format.h"

namespace tests {

namespace {

// The file: "THUG2MC1", the header's count, seed and precision, then the values in the order the cases hand them over.
constexpr std::array<uint8_t, 8> kMagic = {{'T', 'H', 'U', 'G', '2', 'M', 'C', '1'}};
constexpr size_t kHeaderBytes = kMagic.size() + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t);
constexpr size_t kDoubleBytes = 8;
constexpr size_t kFloatBytes = 4;
// FNV-1a, 32 bits: a section's mark.
constexpr uint32_t kFnvBasis = 2166136261U;
constexpr uint32_t kFnvPrime = 16777619U;

uint32_t name_hash(std::string_view name)
{
    uint32_t hash = kFnvBasis;
    for (const char letter : name) {
        hash = (hash ^ static_cast<uint8_t>(letter)) * kFnvPrime;
    }
    return hash;
}

} // namespace

uint64_t ulps(double a, double b)
{
    if (std::isnan(a) || std::isnan(b)) {
        return std::isnan(a) && std::isnan(b) ? 0 : UINT64_MAX;
    }
    int64_t ia = 0;
    int64_t ib = 0;
    std::memcpy(&ia, &a, kDoubleBytes);
    std::memcpy(&ib, &b, kDoubleBytes);
    if (ia < 0) {
        ia = INT64_MIN - ia;
    }
    if (ib < 0) {
        ib = INT64_MIN - ib;
    }
    return ia > ib ? static_cast<uint64_t>(ia) - static_cast<uint64_t>(ib)
                   : static_cast<uint64_t>(ib) - static_cast<uint64_t>(ia);
}

uint64_t ulps(float a, float b)
{
    if (std::isnan(a) || std::isnan(b)) {
        return std::isnan(a) && std::isnan(b) ? 0 : UINT64_MAX;
    }
    int32_t ia = 0;
    int32_t ib = 0;
    std::memcpy(&ia, &a, kFloatBytes);
    std::memcpy(&ib, &b, kFloatBytes);
    if (ia < 0) {
        ia = INT32_MIN - ia;
    }
    if (ib < 0) {
        ib = INT32_MIN - ib;
    }
    return static_cast<uint64_t>(ia > ib ? static_cast<int64_t>(ia) - ib : static_cast<int64_t>(ib) - ia);
}

void CorpusTally::add(Sample sample)
{
    ++runs_;
    if (sample.distance == 0U) {
        return;
    }
    ++differing_;
    if (sample.distance == UINT64_MAX) {
        ++unmatched_;
    } else {
        sum_ulps_ += static_cast<double>(sample.distance);
        max_abs_ = std::max(max_abs_, sample.difference);
    }
    if (sample.distance > max_ulps_) {
        max_ulps_ = sample.distance;
        worst_input_ = sample.input;
    }
}

void CorpusTally::print() const
{
    const double percent = runs_ != 0U ? 100.0 * static_cast<double>(differing_) / static_cast<double>(runs_) : 0.0;
    // (the mean over every result, the NaN-against-number ones left out)
    const double mean = runs_ != unmatched_ ? sum_ulps_ / static_cast<double>(runs_ - unmatched_) : 0.0;
    text::print(
        "  %-36s %8llu runs %8llu differ (%7.3f%%)  max %c%llu  mean %.3g  |diff| %.3g  nan/number %llu  (at %.9g)\n",
        name_.c_str(), static_cast<unsigned long long>(runs_), static_cast<unsigned long long>(differing_), percent,
        unit_, static_cast<unsigned long long>(max_ulps_), mean, max_abs_, static_cast<unsigned long long>(unmatched_),
        worst_input_);
}

Corpus Corpus::to_write(const Header& header)
{
    Corpus corpus(Mode::kWrite, header);
    corpus.bytes_.assign(kMagic.begin(), kMagic.end());
    corpus.put(header.count);
    corpus.put(header.seed);
    corpus.put(header.precision_bits);
    return corpus;
}

bool Corpus::to_compare(std::vector<uint8_t> bytes, Corpus* out)
{
    if (bytes.size() < kHeaderBytes || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        text::print_to(stderr, "thug2_maths_check: not a maths corpus\n");
        return false;
    }
    Corpus corpus(Mode::kCompare, Header{});
    corpus.bytes_ = std::move(bytes);
    corpus.at_ = kMagic.size();
    corpus.header_.count = corpus.take<uint32_t>();
    corpus.header_.seed = corpus.take<uint64_t>();
    corpus.header_.precision_bits = corpus.take<uint32_t>();
    *out = std::move(corpus);
    return true;
}

void Corpus::section(std::string_view name)
{
    const uint32_t hash = name_hash(name);
    if (!comparing()) {
        put(hash);
        return;
    }
    if (take<uint32_t>() != hash && in_step_) {
        text::print_to(stderr, "thug2_maths_check: the corpus is out of step at \"%s\" (written by other code)\n",
                       std::string(name).c_str());
        in_step_ = false;
    }
}

CorpusGroup::~CorpusGroup()
{
    if (!corpus_->comparing()) {
        return;
    }
    text::print("%s\n", heading_.c_str());
    for (const CorpusTally& tally : tallies_) {
        tally.print();
    }
}

bool load_corpus_file(const char* path, std::vector<uint8_t>* bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        text::print_to(stderr, "thug2_maths_check: can't open %s\n", path);
        return false;
    }
    bytes->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool save_corpus_file(const char* path, std::span<const uint8_t> bytes)
{
    std::ofstream file(path, std::ios::binary);
    const std::string characters(bytes.begin(), bytes.end()); // (the bytes as the stream's characters)
    file.write(characters.data(), static_cast<std::streamsize>(characters.size()));
    file.close();
    if (!file) {
        text::print_to(stderr, "thug2_maths_check: can't write %s\n", path);
        return false;
    }
    return true;
}

} // namespace tests
