#pragma once

// The animation cache (CAnimCacheManager, the object at 0x006F92B0): animations' keys decompressed into the
// AnimHeap (0x8F21A798) - 50 entries, each holding one animation's rotation and translation keys and the key
// positions of up to 18 users (slots) playing it. Entries are given back oldest first when the heap runs short
// (the animation code at 0x00485270 does that) and, with evict_unused_, as soon as nobody uses them.

#include <cstddef>
#include <cstdint>
#include <array>

namespace gfx {

// A user's place in a cached animation (0x1E4 bytes, from the item pools 0x006F92B8): a key position per bone for
// the rotations and the translations, and its time.
struct AnimCacheSlot {
    std::array<uint8_t*, 60> rotation_keys_;    // +0x000 (the key each bone's search last stopped at; null: none yet)
    std::array<uint8_t*, 60> translation_keys_; // +0x0F0
    uint16_t clock_;                // +0x1E0 (15 bits, signed; bit 15 kept)
    uint16_t pad1e2_;

    // 0x00480A70 (cdecl): back to whichever item pool holds it, or freed.
    static void dealloc(AnimCacheSlot* slot);
};

// Its key positions cleared and its time 0x8000 (the code that frees a slot does this first).
void get_clear(AnimCacheSlot& slot);
constexpr std::size_t kAnimCacheSlotClockOffset = 0x1E0;
constexpr std::size_t kAnimCacheSlotBytes = 0x1E4;

static_assert(offsetof(AnimCacheSlot, clock_) == kAnimCacheSlotClockOffset && sizeof(AnimCacheSlot) == kAnimCacheSlotBytes,
              "layout must match the original");

constexpr std::size_t kAnimCacheEntrySlotsPtrOffset = 0x28;
constexpr std::size_t kAnimCacheEntryRotationKeySizeOffset = 0x70;
constexpr std::size_t kAnimCacheEntryTranslationOffsetsOffset = 0xEA;
constexpr std::size_t kAnimCacheEntryFlagBitsOffset = 0x164;
constexpr std::size_t kAnimCacheEntryBytes = 0x168;

// One cached animation (0x168 bytes).
struct AnimCacheEntry {
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): bit flags, or-ed together
    enum Flags : uint16_t {
        kUserMask = 0x1FFF, // how many play it
        kKeep = 0x2000,     // never given back as the oldest
        kBusy = 0x4000,     // Release waits (forever) while it's set
        kReleasing = 0x8000,
    };

    // The constructor (inlined into 0x004809F0): empty, the flags' 0xA000 bits kept.
    AnimCacheEntry();
    // A clip's numbers of rotation and translation keys.
    struct KeyCounts {
        int rotation{};
        int translation{};
    };

    // 0x00480810: room for `rotation_keys` rotation and `translation_keys` translation keys of the cache's `type` (2: 8
    // bytes each, 3: 16 each, 4: 20 / 16, others nothing), rounded up past the cache's granularity, allocated on
    // the AnimHeap (zeroed); the animation `key`, `bones`. The size allocated, or 0 (not in use) when the heap
    // hasn't room.
    uint32_t carve(const void* key, int16_t bones, KeyCounts key_counts, int type);
    // 0x00480AD0: given back: the release callback called, the keys freed, every slot cleared and freed; the size
    // it had. Waits forever while it's busy, as the original.
    uint32_t get_release();
    // 0x00481200: the destructor body - the same without the wait and the callback (cleared before its check).
    void destroy_body();
    // 0x00480BF0: a new slot in the first free place, its index (-1 with none free; a failed allocation crashes, as
    // in the original).
    int new_slot();
    // 0x00480C90: a user gone (nothing with none): slot `index` cleared and freed (never slot 0).
    void free_slot(int index);
    // A cache slot and the time given to it.
    struct SlotTime {
        int index{};
        int16_t time{};
    };

    // 0x00480590: slot `index`'s time set to `time` (15 bits); the difference from its last time (0 without the
    // slot).
    int16_t set_slot_time(SlotTime slot_time);
    // 0x004805E0 / 0x004805F0: the size of a rotation / translation key.
    uint8_t get_rotation_key_size();
    uint8_t get_translation_key_size();
    // 0x00480600 / 0x00480610 (the argument isn't used): the rotation / translation keys.
    uint8_t* get_rotation_keys(int unused);
    uint8_t* get_translation_keys(int unused);
    // 0x00480620 / 0x00480630: where each bone's rotation / translation keys start.
    uint16_t* get_rotation_offsets();
    uint16_t* get_translation_offsets();
    // 0x00480640: called with `arg` when the entry is given back.
    void set_release_callback(void (*callback)(void*), void* arg);

    // Which bone's keys are wanted and at which frame.
    struct KeyQuery {
        int bone{};
        float frame{};
    };

    // The key searches over the decompressed keys (boned_anim_frame_data.cpp, with the keys' readers): the keys of
    // `bone` around `frame`: `first` / `second` (the same key when there's only one, or past the last), starting
    // from where slot `slot`'s search stopped when it still fits (-1: no slot) and leaving it there; -2 while the
    // entry is being filled (kReleasing). Returns `slot`. Each has its own quirks, kept as the original.
    // 0x00482FB0 / 0x00483210: rotation / translation keys, searching forwards.
    int find_quat_keys(KeyQuery query, uint8_t** first, uint8_t** second, int slot);
    int find_trans_keys(AnimCacheEntry::KeyQuery query, uint8_t** first, uint8_t** second, int slot);
    // 0x00483470 / 0x004836C0: the same when the animation runs backwards (a search can step back one key).
    int find_quat_keys_backwards(AnimCacheEntry::KeyQuery query, uint8_t** first, uint8_t** second, int slot);
    int find_trans_keys_backwards(int bone, float frame, uint8_t** first, uint8_t** second, int slot);

    // The fields other code reads and sets.
    const void*& lookup_key() { return lookup_key_; }
    [[nodiscard]] const void* lookup_key() const { return lookup_key_; }
    void set_lookup_key(const void* value) { lookup_key_ = value; }
    [[nodiscard]] uint32_t last_used() const { return last_used_; }
    void set_last_used(uint32_t value) { last_used_ = value; }
    [[nodiscard]] int kind_tag() const { return kind_tag_; }
    [[nodiscard]] uint8_t* payload() const { return payload_; }
    void set_payload(uint8_t* value) { payload_ = value; }
    [[nodiscard]] uint32_t allocated() const { return allocated_; }
    void set_allocated(uint32_t value) { allocated_ = value; }
    void set_extent(uint32_t value) { extent_ = value; }
    [[nodiscard]] uint8_t* rotation_keys_ptr() const { return rotation_keys_ptr_; }
    void set_rotation_keys_ptr(uint8_t* value) { rotation_keys_ptr_ = value; }
    [[nodiscard]] uint8_t* translation_keys_ptr() const { return translation_keys_ptr_; }
    void set_translation_keys_ptr(uint8_t* value) { translation_keys_ptr_ = value; }
    std::array<AnimCacheSlot*, 18>& slots_ptr() { return slots_ptr_; }
    [[nodiscard]] const std::array<AnimCacheSlot*, 18>& slots_ptr() const { return slots_ptr_; }
    void set_rotation_key_size(uint8_t value) { rotation_key_size_ = value; }
    void set_translation_key_size(uint8_t value) { translation_key_size_ = value; }
    std::array<uint16_t, 60>& rotation_offsets() { return rotation_offsets_; }
    [[nodiscard]] const std::array<uint16_t, 60>& rotation_offsets() const { return rotation_offsets_; }
    std::array<uint16_t, 60>& translation_offsets() { return translation_offsets_; }
    [[nodiscard]] const std::array<uint16_t, 60>& translation_offsets() const { return translation_offsets_; }
    void set_bones(int16_t value) { bones_ = value; }
    uint16_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint16_t flag_bits() const { return flag_bits_; }

private:
    const void* lookup_key_{nullptr};  // +0x000 the animation's frame data (null: free)
    uint32_t last_used_{};             // +0x004
    int kind_tag_{0};                  // +0x008 (0: not in use)
    uint8_t* payload_{nullptr};        // +0x00C
    uint32_t allocated_{0};            // +0x010
    uint32_t extent_{0};               // +0x014
    uint8_t* rotation_keys_ptr_{nullptr};    // +0x018
    uint8_t* translation_keys_ptr_{nullptr}; // +0x01C
    void (*release_callback_)(void*){}; // +0x020
    void* release_arg_{};               // +0x024
    std::array<AnimCacheSlot*, 18> slots_ptr_{};    // +0x028
    uint8_t rotation_key_size_{0};      // +0x070
    uint8_t translation_key_size_{0};   // +0x071
    std::array<uint16_t, 60> rotation_offsets_{};  // +0x072
    std::array<uint16_t, 60> translation_offsets_{}; // +0x0EA
    int16_t bones_{0};                   // +0x162
    uint16_t flag_bits_{};             // +0x164
    uint16_t pad166_{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(AnimCacheEntry, slots_ptr_) == kAnimCacheEntrySlotsPtrOffset && offsetof(AnimCacheEntry, rotation_key_size_) == kAnimCacheEntryRotationKeySizeOffset &&
                          offsetof(AnimCacheEntry, translation_offsets_) == kAnimCacheEntryTranslationOffsetsOffset &&
                          offsetof(AnimCacheEntry, flag_bits_) == kAnimCacheEntryFlagBitsOffset && sizeof(AnimCacheEntry) == kAnimCacheEntryBytes,
                      "layout must match the original");
    }
};

constexpr std::size_t kClipStoreNumOffset = 0xC8;
constexpr std::size_t kClipStoreEvictUnusedOffset = 0xDC;
constexpr std::size_t kClipStoreBytes = 0xE0;

class ClipStore {
public:
    // How the clip store's heap is cut: the block size and the store's type.
    struct ClipLayout {
        uint32_t granularity{};
        int type{};
    };

    // 0x00480660: the cache (0x006F92B0).
    static ClipStore* get_pull();
    // 0x00480D10: made once - the AnimHeap of `heapSize` bytes ("Initializing CAnimCacheManager at %dk"), then
    // prepare_all(`clip_layout`) (on a null cache too: a crash, as in the original).
    static void fabricate(uint32_t heap_size, ClipLayout clip_layout);
    // 0x00481340: destroyed and freed, the AnimHeap deleted.
    static void teardown();
    // 0x00480940: what the AnimHeap has left.
    static uint32_t heap_available();

    // 0x004809F0: 50 new entries (null where out of memory), the granularity and type.
    void prepare_all(ClipLayout clip_layout);
    // 0x004812F0: each entry destroyed and freed; nothing used.
    void destroy_body();
    // 0x00480800
    int fetch_kind();
    // 0x00480670: what an entry for that many keys of `type` would take (`key` isn't used).
    uint32_t fetch_record_bytes(uint32_t key, AnimCacheEntry::KeyCounts key_counts, int type);
    // 0x00480980: a new entry for `key` in the first entry not in use (none if `key` comes first or none is free, or
    // the heap hasn't room).
    AnimCacheEntry* add_row(const void* key, int16_t bones, int rotation_keys, int translation_keys, int type);
    // 0x004806C0: the key of the least recently used entry (before frame_clock::GetTime) of at least `size` that may go,
    // its users in `users` (0 with none).
    const void* find_oldest(uint32_t size, uint32_t* users);
    // What free_oldest may free: entries with at most this many users, until this many bytes are free.
    struct FreeLimit {
        uint32_t max_users_{};
        uint32_t size{};
    };

    // 0x00480ED0: that entry given back if it has fewer than `max_users` users.
    bool free_oldest(FreeLimit free_limit);
    // 0x00480DB0: every entry in use given back (only those nobody uses with `unused_only`).
    void drain(bool unused_only);
    // 0x00480DF0: every entry whose key is `key` given back (the count taken down for each).
    void drop_out(const void* key);
    // 0x00480E30: a user of `key`'s entry: its new slot's index, or -1.
    int add_user(const void* key);
    // 0x00480E80: a user of `key`'s entries gone; an entry nobody uses then removed with evict_unused_.
    void remove_user(const void* key, int slot);
    // 0x00480F20: the unused entries given back and the rest moved down the AnimHeap (through a copy on the default
    // heap; everything given back when that can't be had). Always 0.
    int defragment();

    // The fields other code reads and sets.
    [[nodiscard]] int num() const { return num_; }
    [[nodiscard]] uint32_t granularity() const { return granularity_; }

private:
    std::array<AnimCacheEntry*, 50> slot_array_; // +0x00
    int num_;                        // +0xC8
    uint32_t taken_count_;           // +0xCC bytes
    uint32_t granularity_;           // +0xD0
    int kind_tag_;                   // +0xD4
    uint32_t unknownd8_;
    bool evict_unused_; // +0xDC
    std::array<uint8_t, 3> paddd_;
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ClipStore, num_) == kClipStoreNumOffset && offsetof(ClipStore, evict_unused_) == kClipStoreEvictUnusedOffset &&
                          sizeof(ClipStore) == kClipStoreBytes,
                      "layout must match the original");
    }
};

} // namespace gfx
