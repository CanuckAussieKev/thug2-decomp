#include "engine/util/lzss.h"

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"

namespace lzss {

namespace {

constexpr int kN = 4096;      // window size
constexpr int kF = 18;        // longest match
constexpr int kLiteralRunLimit = 2; // matches this short or shorter are stored as literals
constexpr int kNil = kN;       // "no node" in the search tree

uint8_t g_decode_window[4096];
int* g_tree_parent;
int* g_tree_right;
int* g_tree_left;
uint8_t* g_window;
int g_longest_run;
int g_dict_hit_offset;
uint32_t g_bytes_in;
uint32_t g_bytes_out;
uint32_t g_emit_counter;

// Encoder state, kept in the exe's globals.
uint8_t*& pane() { return g_window; }  // N + F - 1 bytes
int*& get_left() { return g_tree_left; }            // N + 1 entries
int*& get_right() { return g_tree_right; }           // N + 257 entries (256 tree roots)
int*& current_parent() { return g_tree_parent; }          // N + 1 entries
int& longest_run_ref() { return g_longest_run; }
int& window_match_offset() { return g_dict_hit_offset; }
uint32_t& bytes_in() { return g_bytes_in; }
uint32_t& bytes_out() { return g_bytes_out; }
uint32_t& emit_counter() { return g_emit_counter; }

// The decoder's window is a fixed array in the exe.
uint8_t* decode_window() { return g_decode_window; }

void free_tree()
{
    memory::dealloc(current_parent());
    memory::dealloc(get_right());
    memory::dealloc(get_left());
    memory::dealloc(pane());
}

} // namespace

// 0x00401600
void reset_match_tree()
{
    memory::enter_heap_scope(memory::top_down_heap());
    pane() = static_cast<uint8_t*>(memory::alloc(kN + kF - 1));
    get_left() = static_cast<int*>(memory::alloc((kN + 1) * sizeof(int)));
    get_right() = static_cast<int*>(memory::alloc((kN + 257) * sizeof(int)));
    current_parent() = static_cast<int*>(memory::alloc((kN + 1) * sizeof(int)));
    memory::leave_heap_scope();

    for (int i = kN + 1; i <= kN + 256; ++i)
        get_right()[i] = kNil; // tree roots, one per first byte
    for (int i = 0; i < kN; ++i)
        current_parent()[i] = kNil;
}

// 0x00401290
// Insert the string at window position r into the tree, and record the longest match found on the way.
void add_match_node(int r)
{
    uint8_t* window = pane();
    int* left = get_left();
    int* right = get_right();
    int* parent = current_parent();
    const uint8_t* key = &window[r];

    int cmp = 1;
    int p = kN + 1 + key[0];
    right[r] = left[r] = kNil;
    longest_run_ref() = 0;
    for (;;) {
        if (cmp >= 0) {
            if (right[p] == kNil) {
                right[p] = r;
                parent[r] = p;
                return;
            }
            p = right[p];
        } else {
            if (left[p] == kNil) {
                left[p] = r;
                parent[r] = p;
                return;
            }
            p = left[p];
        }

        int i = 1;
        for (; i < kF; ++i) {
            cmp = key[i] - window[p + i];
            if (cmp != 0)
                break;
        }
        if (i > longest_run_ref()) {
            window_match_offset() = p;
            longest_run_ref() = i;
            if (i >= kF)
                break;
        }
    }

    // A full-length match: replace node p with r.
    parent[r] = parent[p];
    left[r] = left[p];
    right[r] = right[p];
    parent[left[p]] = r;
    parent[right[p]] = r;
    if (right[parent[p]] == p)
        right[parent[p]] = r;
    else
        left[parent[p]] = r;
    parent[p] = kNil;
}

// 0x004013E0
// Remove node p from the tree.
void unlink_list_node(int p)
{
    int* left = get_left();
    int* right = get_right();
    int* parent = current_parent();
    if (parent[p] == kNil)
        return;

    int q;
    if (right[p] == kNil) {
        q = left[p];
    } else if (left[p] == kNil) {
        q = right[p];
    } else {
        q = left[p];
        if (right[q] != kNil) {
            do {
                q = right[q];
            } while (right[q] != kNil);
            right[parent[q]] = left[q];
            parent[left[q]] = parent[q];
            left[q] = left[p];
            parent[left[p]] = q;
        }
        right[q] = right[p];
        parent[right[p]] = q;
    }
    parent[q] = parent[p];
    if (right[parent[p]] == p)
        right[parent[p]] = q;
    else
        left[parent[p]] = q;
    parent[p] = kNil;
}

// 0x00401700
int pack_up(const uint8_t* in, uint8_t* out, int size, bool print_stats)
{
    bytes_in() = 0;
    bytes_out() = 0;
    emit_counter() = 0;
    reset_match_tree();
    uint8_t* window = pane();

    // code[0] holds the flag bits for up to 8 items; each item takes 1 byte (literal) or 2 (reference).
    uint8_t code[17];
    code[0] = 0;
    int code_size = 1;
    uint8_t mask = 1;
    int s = 0;
    int r = kN - kF;
    std::memset(window, ' ', r);

    int length = 0;
    for (; length < kF && size != 0; ++length, --size)
        window[r + length] = *in++;
    bytes_in() = length;
    if (length == 0) {
        free_tree();
        return 0;
    }

    for (int i = 1; i <= kF; ++i)
        add_match_node(r - i);
    add_match_node(r);

    do {
        if (longest_run_ref() > length)
            longest_run_ref() = length;
        if (longest_run_ref() <= kLiteralRunLimit) {
            longest_run_ref() = 1;
            code[0] |= mask;
            code[code_size++] = window[r];
        } else {
            code[code_size++] = static_cast<uint8_t>(window_match_offset());
            code[code_size++] =
                static_cast<uint8_t>(((window_match_offset() >> 4) & 0xF0) | (longest_run_ref() - (kLiteralRunLimit + 1)));
        }

        bool group_full = (mask & 0x80) != 0;
        mask = static_cast<uint8_t>(mask << 1);
        if (group_full) {
            std::memcpy(out, code, code_size);
            out += code_size;
            bytes_out() += code_size;
            code[0] = 0;
            code_size = 1;
            mask = 1;
        }

        int last_match = longest_run_ref();
        int i = 0;
        for (; i < last_match && size != 0; ++i, --size) {
            uint8_t c = *in++;
            unlink_list_node(s);
            window[s] = c;
            if (s < kF - 1)
                window[s + kN] = c; // mirror the start so matches can run past the end
            s = (s + 1) & (kN - 1);
            r = (r + 1) & (kN - 1);
            add_match_node(r);
        }
        bytes_in() += i;
        for (; i < last_match; ++i) {
            unlink_list_node(s);
            s = (s + 1) & (kN - 1);
            r = (r + 1) & (kN - 1);
            if (--length != 0)
                add_match_node(r);
        }
    } while (length > 0);

    if (code_size > 1) {
        std::memcpy(out, code, code_size);
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

// 0x004014F0
// Ghidra shows this as returning nothing, but the original keeps the output pointer in EAX throughout, and
// its callers rely on getting it back (one computes the decompressed size from it, the file loader pads the
// data after it). Returning nothing here left garbage in EAX and corrupted memory while loading files.
uint8_t* decode(const uint8_t* in, uint8_t* out, int size)
{
    uint8_t* window = decode_window();
    std::memset(window, ' ', kN - kF);
    int r = kN - kF;
    unsigned int flags = 0;

    for (;;) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (size == 0)
                return out;
            flags = *in++ | 0xFF00; // the high byte counts the 8 flags off
            --size;
        }
        if (flags & 1) {
            if (size == 0)
                return out;
            --size;
            uint8_t c = *in++;
            *out++ = c;
            window[r] = c;
            r = (r + 1) & (kN - 1);
        } else {
            if (size == 0)
                return out;
            int position = in[0];
            int info = in[1];
            in += 2;
            size -= 2;
            position |= (info & 0xF0) << 4;
            int last = (info & 0x0F) + kLiteralRunLimit;
            for (int k = 0; k <= last; ++k) {
                uint8_t c = window[(position + k) & (kN - 1)];
                *out++ = c;
                window[r] = c;
                r = (r + 1) & (kN - 1);
            }
        }
    }
}

} // namespace Lzss
