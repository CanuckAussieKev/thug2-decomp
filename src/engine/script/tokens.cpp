#include "engine/script/tokens.h"

#include <cstddef>
#include <iterator>
#include <map>
#include <utility>

#include <gsl/assert>

#include "platform/run_log.h"

namespace script {

namespace {

// The blocks by their first byte: their sizes.
using CodeBlocks = std::map<const uint8_t*, std::size_t>;
CodeBlocks& code_blocks()
{
    static CodeBlocks blocks;
    return blocks;
}

// The block found last (the interpreter takes up the same script's code over and over).
struct LastBlock {
    const uint8_t* begin = nullptr;
    std::size_t size = 0;
};
LastBlock g_last_block;

// Whether `place` is in the block of `size` bytes at `begin` (its end included).
bool holds(const uint8_t* begin, std::size_t size, const uint8_t* place)
{
    if (!begin || place < begin)
        return false;
    return std::cmp_less_equal(place - begin, size);
}

} // namespace

void note_code_block(std::span<const uint8_t> block)
{
    if (block.empty() || !block.data())
        return;
    CodeBlocks& blocks = code_blocks();
    const uint8_t* begin = block.data();
    // (blocks freed without being forgotten that lay where this one is now)
    auto it = blocks.lower_bound(begin);
    if (it != blocks.begin()) {
        const auto before = std::prev(it);
        if (std::cmp_less(begin - before->first, before->second))
            blocks.erase(before);
    }
    while (it != blocks.end() && std::cmp_less(it->first - begin, block.size()))
        it = blocks.erase(it);
    blocks[begin] = block.size();
    g_last_block = {};
}

void forget_code_block(const void* block)
{
    if (!block)
        return;
    code_blocks().erase(static_cast<const uint8_t*>(block));
    g_last_block = {};
}

Tokens tokens_at(const uint8_t* place)
{
    if (!place)
        return {};
    if (!holds(g_last_block.begin, g_last_block.size, place)) {
        const CodeBlocks& blocks = code_blocks();
        auto it = blocks.upper_bound(place);
        const bool found = it != blocks.begin() && holds(std::prev(it)->first, std::prev(it)->second, place);
        if (!found)
            get_log("script code at %p is in no block of code noted (tokens_at) - %u blocks noted",
                    static_cast<const void*>(place), static_cast<unsigned>(blocks.size()));
        Expects(found);
        --it;
        g_last_block = {it->first, it->second};
    }
    return Tokens(std::span<const uint8_t>(g_last_block.begin, g_last_block.size)).moved_to(place);
}

} // namespace script
