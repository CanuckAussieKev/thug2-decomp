#include "engine/script/tokens.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <span>
#include <utility>

#include <gsl/assert>

#include "engine/script/token_reader.h"
#include "platform/run_log.h"
#include "base/core/no_destroy.h"

namespace script {

namespace {

// The blocks by their first byte: their sizes.
using CodeBlocks = std::map<const uint8_t*, std::size_t>;
CodeBlocks& code_blocks()
{
    static CodeBlocks blocks;
    return blocks;
}

// NOT ORIGINAL: private copies of code (a running script's copy of a script value): their first byte -> the code
// they were copied from (note_code_copy).
std::map<const uint8_t*, const uint8_t*>& code_copies()
{
    static std::map<const uint8_t*, const uint8_t*> copies;
    return copies;
}

// The block found last (the interpreter takes up the same script's code over and over).
struct LastBlock {
    const uint8_t* begin = nullptr;
    std::size_t size = 0;
};
LastBlock& last_block()
{
    static core::NoDestroy<LastBlock> last_block;
    return last_block.get();
}
// Counted up whenever a block is noted or forgotten.
uint32_t& generation()
{
    static uint32_t generation{};
    return generation;
}

// Whether `place` is in the block of `size` bytes at `begin` (its end included).
bool holds(const uint8_t* begin, std::size_t size, const uint8_t* place)
{
    if ((begin == nullptr) || std::less<>()(place, begin)) {
        return false;
    }
    return std::cmp_less_equal(place - begin, size);
}

} // namespace

void note_code_block(std::span<const uint8_t> block)
{
    if (block.empty() || !block.data()) {
        return;
    }
    CodeBlocks& blocks = code_blocks();
    const uint8_t* begin = block.data();
    // (blocks freed without being forgotten that lay where this one is now)
    auto it = blocks.lower_bound(begin);
    if (it != blocks.begin()) {
        const auto before = std::prev(it);
        if (std::cmp_less(begin - before->first, before->second)) {
            blocks.erase(before);
        }
    }
    while (it != blocks.end() && std::cmp_less(it->first - begin, block.size())) {
        it = blocks.erase(it);
    }
    blocks[begin] = block.size();
    last_block() = {};
    ++generation();
}

void forget_code_block(const void* block)
{
    if (!block) {
        return;
    }
    code_blocks().erase(static_cast<const uint8_t*>(block));
    code_copies().erase(static_cast<const uint8_t*>(block));
    last_block() = {};
    ++generation();
}

void forget_script_code(const void* block)
{
    if (const auto found = code_blocks().find(static_cast<const uint8_t*>(block)); found != code_blocks().end()) {
        forget_stored_randoms(found->first, found->second);
    }
    forget_code_block(block);
}

void note_code_copy(const uint8_t* copy, const uint8_t* source) { code_copies()[copy] = source; }

const uint8_t* code_original(const uint8_t* place)
{
    const auto& copies = code_copies();
    auto it = copies.upper_bound(place);
    if (it == copies.begin()) {
        return place;
    }
    --it;
    const auto block = code_blocks().find(it->first);
    if (block == code_blocks().end() || !holds(it->first, block->second, place) || place == it->first + block->second) {
        return place;
    }
    return it->second + (place - it->first);
}

void park_script_code(const void* block, uint32_t name)
{
    if (const auto found = code_blocks().find(static_cast<const uint8_t*>(block)); found != code_blocks().end()) {
        park_stored_randoms(found->first, found->second, name);
    }
    forget_code_block(block);
}

Tokens tokens_at(const uint8_t* place)
{
    if (!place) {
        return {};
    }
    if (!holds(last_block().begin, last_block().size, place)) {
        const CodeBlocks& blocks = code_blocks();
        auto it = blocks.upper_bound(place);
        const bool found = it != blocks.begin() && holds(std::prev(it)->first, std::prev(it)->second, place);
        if (!found) {
            get_log("script code at %p is in no block of code noted (tokens_at) - %u blocks noted",
                    static_cast<const void*>(place), static_cast<unsigned>(blocks.size()));
        }
        Expects(found);
        --it;
        last_block() = {.begin = it->first, .size = it->second};
    }
    return Tokens(std::span<const uint8_t>(last_block().begin, last_block().size)).moved_to(place);
}

Tokens tokens_at(Tokens near, const uint8_t* place)
{
    const std::span<const uint8_t> run = near.run();
    return holds(run.data(), run.size(), place) ? near.moved_to(place) : tokens_at(place);
}

std::span<const uint8_t> code_between(Tokens from, Tokens to)
{
    Expects(to >= from);
    return from.rest().first(static_cast<std::size_t>(to - from));
}

uint32_t code_blocks_generation() { return generation(); }

} // namespace script
