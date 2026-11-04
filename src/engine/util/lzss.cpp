#include "engine/util/lzss.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <span>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/no_destroy.h"

namespace lzss {

namespace {

constexpr int kN = 4096;            // window size
constexpr int kF = 18;              // longest match
constexpr int kLiteralRunLimit = 2; // matches this short or shorter are stored as literals
constexpr int kNil = kN;            // "no node" in the search tree
constexpr int kFirstByteRoots = 256; // one tree root per value of a match's first byte
constexpr size_t kCodeGroupSize = 17; // the flag byte and 8 items of at most 2 bytes
constexpr int kOffsetHighNibbleShift = 4;
constexpr int kOffsetHighNibbleMask = 0xF0; // the match offset's bits 8-11 sit in the high half of the length byte

// NOT ORIGINAL grouping - the compressor's search tree, window, match and counts while it runs. Made on first use,
// never destroyed (lzss_state()).
struct LzssState {
    std::span<int> tree_parent_;
    std::span<int> tree_right_;
    std::span<int> tree_left_;
    std::span<uint8_t> window_;
    int longest_run_{};
    int dict_hit_offset_{};
    uint32_t bytes_in_{};
    uint32_t bytes_out_{};
    uint32_t emit_counter_{};
};

LzssState& lzss_state()
{
    static core::NoDestroy<LzssState> state;
    return state.get();
}

// Encoder state, kept in the exe's globals.
std::span<uint8_t>& pane() { return lzss_state().window_; }            // N + F - 1 bytes
std::span<int>& get_left() { return lzss_state().tree_left_; }         // N + 1 entries
std::span<int>& get_right() { return lzss_state().tree_right_; }       // N + 257 entries (256 tree roots)
std::span<int>& current_parent() { return lzss_state().tree_parent_; } // N + 1 entries
int& longest_run_ref() { return lzss_state().longest_run_; }
int& window_match_offset() { return lzss_state().dict_hit_offset_; }
uint32_t& bytes_in() { return lzss_state().bytes_in_; }
uint32_t& bytes_out() { return lzss_state().bytes_out_; }
uint32_t& emit_counter() { return lzss_state().emit_counter_; }

void free_tree()
{
    memory::dealloc(current_parent().data());
    memory::dealloc(get_right().data());
    memory::dealloc(get_left().data());
    memory::dealloc(pane().data());
}

// `count` elements of the heap's memory.
template <typename T>
std::span<T> allocate_run(std::size_t count)
{
    return {static_cast<T*>(memory::alloc(count * sizeof(T))), count};
}

} // namespace

// 0x00401600
void reset_match_tree()
{
    memory::enter_heap_scope(memory::top_down_heap());
    pane() = allocate_run<uint8_t>(kN + kF - 1);
    get_left() = allocate_run<int>(kN + 1);
    get_right() = allocate_run<int>(kN + 1 + kFirstByteRoots);
    current_parent() = allocate_run<int>(kN + 1);
    memory::leave_heap_scope();

    for (int i = kN + 1; i <= kN + kFirstByteRoots; ++i) {
        core::at(get_right(), i) = kNil; // tree roots, one per first byte
    }
    for (int i = 0; i < kN; ++i) {
        core::at(current_parent(), i) = kNil;
    }
}

// 0x00401290
// Insert the string at window position r into the tree, and record the longest match found on the way.
void add_match_node(int position)
{
    const std::span<const uint8_t> window = pane();
    const std::span<int> left = get_left();
    const std::span<int> right = get_right();
    const std::span<int> parent = current_parent();
    const std::span<const uint8_t> key = window.subspan(static_cast<std::size_t>(position));

    int cmp = 1;
    int node = kN + 1 + core::at(key, 0);
    core::at(right, position) = core::at(left, position) = kNil;
    longest_run_ref() = 0;
    for (;;) {
        if (cmp >= 0) {
            if (core::at(right, node) == kNil) {
                core::at(right, node) = position;
                core::at(parent, position) = node;
                return;
            }
            node = core::at(right, node);
        } else {
            if (core::at(left, node) == kNil) {
                core::at(left, node) = position;
                core::at(parent, position) = node;
                return;
            }
            node = core::at(left, node);
        }

        int matched = 1;
        for (; matched < kF; ++matched) {
            cmp = core::at(key, matched) - core::at(window, node + matched);
            if (cmp != 0) {
                break;
            }
        }
        if (matched > longest_run_ref()) {
            window_match_offset() = node;
            longest_run_ref() = matched;
            if (matched >= kF) {
                break;
            }
        }
    }

    // A full-length match: replace node p with r.
    core::at(parent, position) = core::at(parent, node);
    core::at(left, position) = core::at(left, node);
    core::at(right, position) = core::at(right, node);
    core::at(parent, core::at(left, node)) = position;
    core::at(parent, core::at(right, node)) = position;
    if (core::at(right, core::at(parent, node)) == node) {
        core::at(right, core::at(parent, node)) = position;
    } else {
        core::at(left, core::at(parent, node)) = position;
    }
    core::at(parent, node) = kNil;
}

// 0x004013E0
// Remove node p from the tree.
void unlink_list_node(int node)
{
    const std::span<int> left = get_left();
    const std::span<int> right = get_right();
    const std::span<int> parent = current_parent();
    if (core::at(parent, node) == kNil) {
        return;
    }

    int replacement = 0;
    if (core::at(right, node) == kNil) {
        replacement = core::at(left, node);
    } else if (core::at(left, node) == kNil) {
        replacement = core::at(right, node);
    } else {
        replacement = core::at(left, node);
        if (core::at(right, replacement) != kNil) {
            for (bool again = true; again; again = core::at(right, replacement) != kNil) {
                replacement = core::at(right, replacement);
            }
            core::at(right, core::at(parent, replacement)) = core::at(left, replacement);
            core::at(parent, core::at(left, replacement)) = core::at(parent, replacement);
            core::at(left, replacement) = core::at(left, node);
            core::at(parent, core::at(left, node)) = replacement;
        }
        core::at(right, replacement) = core::at(right, node);
        core::at(parent, core::at(right, node)) = replacement;
    }
    core::at(parent, replacement) = core::at(parent, node);
    if (core::at(right, core::at(parent, node)) == node) {
        core::at(right, core::at(parent, node)) = replacement;
    } else {
        core::at(left, core::at(parent, node)) = replacement;
    }
    core::at(parent, node) = kNil;
}

// 0x00401700
int pack_up(std::span<const uint8_t> input, std::span<uint8_t> output, bool print_stats)
{
    core::Cursor<const uint8_t> in(input);
    core::Cursor<uint8_t> out(output);
    int size = static_cast<int>(input.size());
    bytes_in() = 0;
    bytes_out() = 0;
    emit_counter() = 0;
    reset_match_tree();
    const std::span<uint8_t> window = pane();

    // code[0] holds the flag bits for up to 8 items; each item takes 1 byte (literal) or 2 (reference).
    std::array<uint8_t, kCodeGroupSize> code{};
    core::at(code, 0) = 0;
    int code_size = 1;
    uint8_t mask = 1;
    int replace_position = 0;
    int position = kN - kF;
    std::fill_n(window.begin(), position, ' ');

    int length = 0;
    for (; length < kF && size != 0; ++length, --size) {
        core::at(window, position + length) = *in++;
    }
    bytes_in() = length;
    if (length == 0) {
        free_tree();
        return 0;
    }

    for (int i = 1; i <= kF; ++i) {
        add_match_node(position - i);
    }
    add_match_node(position);

    for (bool again = true; again; again = length > 0) {
        longest_run_ref() = std::min(longest_run_ref(), length);
        if (longest_run_ref() <= kLiteralRunLimit) {
            longest_run_ref() = 1;
            core::at(code, 0) |= mask;
            core::at(code, code_size++) = core::at(window, position);
        } else {
            core::at(code, code_size++) = static_cast<uint8_t>(window_match_offset());
            core::at(code, code_size++) = static_cast<uint8_t>(((window_match_offset() >> kOffsetHighNibbleShift) & kOffsetHighNibbleMask) |
                                                     (longest_run_ref() - (kLiteralRunLimit + 1)));
        }

        const bool group_full = (mask & 0x80) != 0;
        mask = static_cast<uint8_t>(mask << 1);
        if (group_full) {
            std::copy_n(code.begin(), code_size, out.rest().begin());
            out += code_size;
            bytes_out() += code_size;
            core::at(code, 0) = 0;
            code_size = 1;
            mask = 1;
        }

        const int last_match = longest_run_ref();
        int matched = 0;
        for (; matched < last_match && size != 0; ++matched, --size) {
            const uint8_t byte = *in++;
            unlink_list_node(replace_position);
            core::at(window, replace_position) = byte;
            if (replace_position < kF - 1) {
                core::at(window, replace_position + kN) = byte; // mirror the start so matches can run past the end
            }
            replace_position = (replace_position + 1) & (kN - 1);
            position = (position + 1) & (kN - 1);
            add_match_node(position);
        }
        bytes_in() += matched;
        for (; matched < last_match; ++matched) {
            unlink_list_node(replace_position);
            replace_position = (replace_position + 1) & (kN - 1);
            position = (position + 1) & (kN - 1);
            if (--length != 0) {
                add_match_node(position);
            }
        }
    }

    if (code_size > 1) {
        std::copy_n(code.begin(), code_size, out.rest().begin());
        bytes_out() += code_size;
    }

    if (print_stats) {
        debug::printf("     In : %ld bytes\n", bytes_in());
        debug::printf("     Out: %ld bytes\n", bytes_out());
        debug::printf("     Out/In: %.3f\n", static_cast<double>(bytes_out()) / static_cast<double>(bytes_in()));
    }

    free_tree();
    return static_cast<int>(bytes_out());
}

} // namespace lzss
