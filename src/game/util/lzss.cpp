#include "game/util/lzss.h"

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Lzss {

namespace {

constexpr int N = 4096;      // window size
constexpr int F = 18;        // longest match
constexpr int THRESHOLD = 2; // matches this short or shorter are stored as literals
constexpr int NIL = N;       // "no node" in the search tree

GAME_GLOBAL(uint8_t[4096], g_decodeWindow, 0x0068FF68);
GAME_GLOBAL(int*, g_treeParent, 0x00690F7C);
GAME_GLOBAL(int*, g_treeRight, 0x00690F80);
GAME_GLOBAL(int*, g_treeLeft, 0x00690F84);
GAME_GLOBAL(uint8_t*, g_window, 0x00690F88);
GAME_GLOBAL(int, g_matchLength, 0x00690F8C);
GAME_GLOBAL(int, g_matchPosition, 0x00690F90);
GAME_GLOBAL(uint32_t, g_bytesIn, 0x00690F94);
GAME_GLOBAL(uint32_t, g_bytesOut, 0x00690F98);
GAME_GLOBAL(uint32_t, g_printCount, 0x00690F9C);

// Encoder state, kept in the exe's globals.
uint8_t*& Window() { return g_window; }  // N + F - 1 bytes
int*& Left() { return g_treeLeft; }            // N + 1 entries
int*& Right() { return g_treeRight; }           // N + 257 entries (256 tree roots)
int*& Parent() { return g_treeParent; }          // N + 1 entries
int& MatchLength() { return g_matchLength; }
int& MatchPosition() { return g_matchPosition; }
uint32_t& BytesIn() { return g_bytesIn; }
uint32_t& BytesOut() { return g_bytesOut; }
uint32_t& PrintCount() { return g_printCount; }

// The decoder's window is a fixed array in the exe.
uint8_t* DecodeWindow() { return g_decodeWindow; }

void FreeTree()
{
    Mem::Free(Parent());
    Mem::Free(Right());
    Mem::Free(Left());
    Mem::Free(Window());
}

} // namespace

// 0x00401600
void InitTree()
{
    Mem::PushContext(Mem::TopDownHeap());
    Window() = static_cast<uint8_t*>(Mem::New(N + F - 1));
    Left() = static_cast<int*>(Mem::New((N + 1) * sizeof(int)));
    Right() = static_cast<int*>(Mem::New((N + 257) * sizeof(int)));
    Parent() = static_cast<int*>(Mem::New((N + 1) * sizeof(int)));
    Mem::PopContext();

    for (int i = N + 1; i <= N + 256; ++i)
        Right()[i] = NIL; // tree roots, one per first byte
    for (int i = 0; i < N; ++i)
        Parent()[i] = NIL;
}

// 0x00401290
// Insert the string at window position r into the tree, and record the longest match found on the way.
void InsertNode(int r)
{
    uint8_t* window = Window();
    int* left = Left();
    int* right = Right();
    int* parent = Parent();
    const uint8_t* key = &window[r];

    int cmp = 1;
    int p = N + 1 + key[0];
    right[r] = left[r] = NIL;
    MatchLength() = 0;
    for (;;) {
        if (cmp >= 0) {
            if (right[p] == NIL) {
                right[p] = r;
                parent[r] = p;
                return;
            }
            p = right[p];
        } else {
            if (left[p] == NIL) {
                left[p] = r;
                parent[r] = p;
                return;
            }
            p = left[p];
        }

        int i = 1;
        for (; i < F; ++i) {
            cmp = key[i] - window[p + i];
            if (cmp != 0)
                break;
        }
        if (i > MatchLength()) {
            MatchPosition() = p;
            MatchLength() = i;
            if (i >= F)
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
    parent[p] = NIL;
}

// 0x004013E0
// Remove node p from the tree.
void DeleteNode(int p)
{
    int* left = Left();
    int* right = Right();
    int* parent = Parent();
    if (parent[p] == NIL)
        return;

    int q;
    if (right[p] == NIL) {
        q = left[p];
    } else if (left[p] == NIL) {
        q = right[p];
    } else {
        q = left[p];
        if (right[q] != NIL) {
            do {
                q = right[q];
            } while (right[q] != NIL);
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
    parent[p] = NIL;
}

// 0x00401700
int Encode(const uint8_t* in, uint8_t* out, int size, bool print_stats)
{
    BytesIn() = 0;
    BytesOut() = 0;
    PrintCount() = 0;
    InitTree();
    uint8_t* window = Window();

    // code[0] holds the flag bits for up to 8 items; each item takes 1 byte (literal) or 2 (reference).
    uint8_t code[17];
    code[0] = 0;
    int code_size = 1;
    uint8_t mask = 1;
    int s = 0;
    int r = N - F;
    std::memset(window, ' ', r);

    int length = 0;
    for (; length < F && size != 0; ++length, --size)
        window[r + length] = *in++;
    BytesIn() = length;
    if (length == 0) {
        FreeTree();
        return 0;
    }

    for (int i = 1; i <= F; ++i)
        InsertNode(r - i);
    InsertNode(r);

    do {
        if (MatchLength() > length)
            MatchLength() = length;
        if (MatchLength() <= THRESHOLD) {
            MatchLength() = 1;
            code[0] |= mask;
            code[code_size++] = window[r];
        } else {
            code[code_size++] = static_cast<uint8_t>(MatchPosition());
            code[code_size++] =
                static_cast<uint8_t>(((MatchPosition() >> 4) & 0xF0) | (MatchLength() - (THRESHOLD + 1)));
        }

        bool group_full = (mask & 0x80) != 0;
        mask = static_cast<uint8_t>(mask << 1);
        if (group_full) {
            std::memcpy(out, code, code_size);
            out += code_size;
            BytesOut() += code_size;
            code[0] = 0;
            code_size = 1;
            mask = 1;
        }

        int last_match = MatchLength();
        int i = 0;
        for (; i < last_match && size != 0; ++i, --size) {
            uint8_t c = *in++;
            DeleteNode(s);
            window[s] = c;
            if (s < F - 1)
                window[s + N] = c; // mirror the start so matches can run past the end
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            InsertNode(r);
        }
        BytesIn() += i;
        for (; i < last_match; ++i) {
            DeleteNode(s);
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            if (--length != 0)
                InsertNode(r);
        }
    } while (length > 0);

    if (code_size > 1) {
        std::memcpy(out, code, code_size);
        BytesOut() += code_size;
    }

    if (print_stats) {
        Dbg::Printf("     In : %ld bytes\n", BytesIn());
        Dbg::Printf("     Out: %ld bytes\n", BytesOut());
        Dbg::Printf("     Out/In: %.3f\n", static_cast<double>(BytesOut()) / static_cast<double>(BytesIn()));
    }

    FreeTree();
    return static_cast<int>(BytesOut());
}

// 0x004014F0
// Ghidra shows this as returning nothing, but the original keeps the output pointer in EAX throughout, and
// its callers rely on getting it back (one computes the decompressed size from it, the file loader pads the
// data after it). Returning nothing here left garbage in EAX and corrupted memory while loading files.
uint8_t* Decode(const uint8_t* in, uint8_t* out, int size)
{
    uint8_t* window = DecodeWindow();
    std::memset(window, ' ', N - F);
    int r = N - F;
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
            r = (r + 1) & (N - 1);
        } else {
            if (size == 0)
                return out;
            int position = in[0];
            int info = in[1];
            in += 2;
            size -= 2;
            position |= (info & 0xF0) << 4;
            int last = (info & 0x0F) + THRESHOLD;
            for (int k = 0; k <= last; ++k) {
                uint8_t c = window[(position + k) & (N - 1)];
                *out++ = c;
                window[r] = c;
                r = (r + 1) & (N - 1);
            }
        }
    }
}

} // namespace Lzss

// All cdecl, as in the original.
REPLACE(0x00401290, Lzss::InsertNode);
REPLACE(0x004013E0, Lzss::DeleteNode);
REPLACE(0x004014F0, Lzss::Decode);
REPLACE(0x00401600, Lzss::InitTree);
REPLACE(0x00401700, Lzss::Encode);
