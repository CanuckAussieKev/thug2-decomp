// The animation cache (0x00480590-0x0048137B): see anim_cache.h. From the disassembly.

#include "engine/gfx/anim_cache.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <utility>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "platform/stopwatch.h"
#include "engine/script/script_startup.h"
#include "engine/core/raw_view.h"

namespace gfx {

memory::PoolPair g_anim_slot_pools; // (CAnimCachePredictor; script_startup.h)

namespace {

constexpr uint32_t kAnimHeap = 0x8F21A798; // AnimHeap
constexpr int kEntries = 50;
constexpr int kSlots = 18;

ClipStore* g_clip_store;

ClipStore*& get_cache() { return g_clip_store; }
memory::PoolPair& slot_pools() { return g_anim_slot_pools; }

memory::Hub* hub() { return memory::Hub::get_pull(); }

// The AnimHeap made current (allocations and frees in between go to it).
void push_anim_heap() { hub()->enter_heap_scope(hub()->fetch_heap_by_key(kAnimHeap, 1)); }
void pop_heap() { hub()->leave_heap_scope(); }

// The sizes of a rotation and a translation key for a cache type.
void key_sizes(int type, uint8_t& rotation, uint8_t& translation)
{
    switch (type) {
    case 2:
        rotation = translation = 8;
        break;
    case 3:
        rotation = translation = 16;
        break;
    case 4:
        rotation = 20;
        translation = 16;
        break;
    default:
        rotation = translation = 0;
        break;
    }
}

// The flags with the user count changed by `delta` (13 bits, wrapping).
uint16_t with_users(uint16_t flags, int delta)
{
    return static_cast<uint16_t>((flags & ~AnimCacheEntry::kUserMask) | ((flags + delta) & AnimCacheEntry::kUserMask));
}

// Every slot cleared and given back (Release and the destructor).
void free_slots(AnimCacheEntry* entry)
{
    for (AnimCacheSlot*& slot : entry->slots_ptr_) {
        if (slot) {
            slot->get_clear();
            if (slot)
                AnimCacheSlot::dealloc(slot);
            slot = nullptr;
        }
    }
}

// The fields Release and the destructor empty.
void vacant(AnimCacheEntry* entry)
{
    entry->rotation_keys_ptr_ = nullptr;
    entry->translation_keys_ptr_ = nullptr;
    entry->lookup_key_ = 0;
    entry->bones_ = 0;
    entry->rotation_key_size_ = 0;
    entry->translation_key_size_ = 0;
    entry->extent_ = 0;
    entry->allocated_ = 0;
}

// The original's REP MOVSD / MOVSB copy: dwords forwards, then the odd bytes - kept for overlapping blocks (an entry
// whose Allocate failed keeps its old, smaller block with the new size: its copy runs into what follows).
void copy_forwards(uint8_t* to, const uint8_t* from, uint32_t size)
{
    __movsd(raw::as<unsigned long>(to), raw::as<const unsigned long>(from), size / 4);
    __movsb(to + (size & ~3u), from + (size & ~3u), size & 3);
}

void free_data(AnimCacheEntry* entry)
{
    if (entry->payload_) {
        push_anim_heap();
        hub()->dealloc(entry->payload_);
        pop_heap();
        entry->payload_ = nullptr;
    }
}

} // namespace

// --- the slots ---

// 0x00480A70
void AnimCacheSlot::dealloc(AnimCacheSlot* slot) { slot_pools().dealloc(slot); }

void AnimCacheSlot::get_clear()
{
    std::memset(rotation_keys_.data(), 0, sizeof(rotation_keys_));
    std::memset(translation_keys_.data(), 0, sizeof(translation_keys_));
    clock_ = 0x8000;
}

// --- an entry ---

AnimCacheEntry::AnimCacheEntry() { flag_bits_ &= 0xA000; }

// 0x00480810
uint32_t AnimCacheEntry::carve(uint32_t key, int16_t bones, int rotation_keys, int translation_keys, int type)
{
    flag_bits_ |= kReleasing;
    bones_ = bones;
    kind_tag_ = type;
    key_sizes(type, rotation_key_size_, translation_key_size_);
    const uint32_t granularity = get_cache()->granularity_;
    extent_ = rotation_key_size_ * rotation_keys + translation_key_size_ * translation_keys;
    allocated_ = (extent_ / granularity + 1) * granularity;
    push_anim_heap();
    if (hub()->obtainable() < allocated_) {
        kind_tag_ = 0;
        pop_heap();
        return 0;
    }
    lookup_key_ = key;
    payload_ = static_cast<uint8_t*>(memory::alloc(allocated_));
    std::memset(payload_, 0, allocated_);
    rotation_keys_ptr_ = payload_;
    translation_keys_ptr_ = payload_ + rotation_key_size_ * rotation_keys;
    pop_heap();
    flag_bits_ &= ~(kBusy | kReleasing);
    last_used_ = 0;
    return allocated_;
}

// 0x00480AD0
uint32_t AnimCacheEntry::get_release()
{
    // (a busy entry: forever, as the original - the flags were read once. Volatile: an empty loop that never ends
    // is undefined behaviour in C++, a compiler may drop it.)
    const volatile uint16_t flags = flag_bits_;
    while (flags & kBusy) {
    }
    flag_bits_ = flags | kReleasing;
    if (release_callback_) {
        release_callback_(release_arg_);
        release_callback_ = nullptr;
    }
    const uint32_t allocated = allocated_;
    flag_bits_ &= kReleasing;
    vacant(this);
    free_data(this);
    free_slots(this);
    kind_tag_ = 0;
    return allocated;
}

// 0x00481200
void AnimCacheEntry::destroy_body()
{
    flag_bits_ &= ~(kKeep | kBusy);
    release_callback_ = nullptr;
    vacant(this);
    free_data(this);
    if (release_callback_) { // (never: cleared above)
        release_callback_(release_arg_);
        release_callback_ = nullptr;
    }
    free_slots(this);
    kind_tag_ = 0;
}

// 0x00480BF0
int AnimCacheEntry::new_slot()
{
    for (int i = 0; i < kSlots; ++i) {
        if (slots_ptr_[i])
            continue;
        auto* slot = slot_pools().allocate<AnimCacheSlot>();
        if (slot)
            slot->clock_ = 0x8000;
        slots_ptr_[i] = slot;
        slot->clock_ &= 0x7FFF; // (null: a crash, as the original)
        std::memset(slots_ptr_[i]->rotation_keys_.data(), 0, sizeof(slot->rotation_keys_));
        std::memset(slots_ptr_[i]->translation_keys_.data(), 0, sizeof(slot->translation_keys_));
        return i;
    }
    return -1;
}

// 0x00480C90
void AnimCacheEntry::free_slot(int index)
{
    if (!(flag_bits_ & kUserMask))
        return;
    // (the original reads slots_ptr_[index] first - with -1, a user that never had a slot, the field before it - and
    // then ignores it unless index > 0)
    AnimCacheSlot* slot = index > 0 && std::cmp_less(index, slots_ptr_.size()) ? slots_ptr_[index] : nullptr;
    if (slot) {
        slot->get_clear();
        if (slots_ptr_[index])
            AnimCacheSlot::dealloc(slots_ptr_[index]);
        slots_ptr_[index] = nullptr;
    }
    flag_bits_ = with_users(flag_bits_, -1);
}

// 0x00480590
int16_t AnimCacheEntry::set_slot_time(int index, int16_t time)
{
    AnimCacheSlot* slot = slots_ptr_[index];
    if (!slot)
        return 0;
    const uint16_t old = slot->clock_;
    const int last = static_cast<int16_t>(old << 1) >> 1; // (15 bits, signed)
    slot->clock_ = static_cast<uint16_t>((old & 0x8000) | (static_cast<uint16_t>(time) & 0x7FFF));
    return static_cast<int16_t>(time - last);
}

// 0x004805E0
uint8_t AnimCacheEntry::get_rotation_key_size() { return rotation_key_size_; }

// 0x004805F0
uint8_t AnimCacheEntry::get_translation_key_size() { return translation_key_size_; }

// 0x00480600
uint8_t* AnimCacheEntry::get_rotation_keys(int /*unused*/) { return rotation_keys_ptr_; }

// 0x00480610
uint8_t* AnimCacheEntry::get_translation_keys(int /*unused*/) { return translation_keys_ptr_; }

// 0x00480620
uint16_t* AnimCacheEntry::get_rotation_offsets() { return rotation_offsets_.data(); }

// 0x00480630
uint16_t* AnimCacheEntry::get_translation_offsets() { return translation_offsets_.data(); }

// 0x00480640
void AnimCacheEntry::set_release_callback(void (*callback)(void*), void* arg)
{
    release_callback_ = callback;
    release_arg_ = arg;
}

// --- the cache ---

// 0x00480660
ClipStore* ClipStore::get_pull() { return get_cache(); }

// 0x00480D10
void ClipStore::fabricate(uint32_t heap_size, uint32_t granularity, int type)
{
    if (get_cache())
        return;
    hub()->create_labeled_zone(kAnimHeap, heap_size, "AnimHeap", 0); // "AnimHeap"
    debug::printf("Initializing CAnimCacheManager at %dk\n",
                  heap_size >> 10); // "Initializing CAnimCacheManager at %dk\n"
    auto* cache = static_cast<ClipStore*>(memory::alloc_zeroed(sizeof(ClipStore)));
    if (cache) {
        std::memset(static_cast<void*>(cache->slot_array_.data()), 0, sizeof(cache->slot_array_));
        cache->taken_count_ = 0;
        cache->granularity_ = 0;
        cache->kind_tag_ = 0;
        cache->unknownd8_ = 0;
        cache->num_ = 0;
        cache->evict_unused_ = false;
    }
    get_cache() = cache;
    cache->prepare_all(granularity, type);
}

// 0x00481340
void ClipStore::teardown()
{
    if (ClipStore* cache = get_cache()) {
        cache->destroy_body();
        memory::dealloc(cache);
    }
    get_cache() = nullptr;
    hub()->release_labeled_zone(kAnimHeap, 1);
}

// 0x00480940
uint32_t ClipStore::heap_available()
{
    push_anim_heap();
    const uint32_t available = hub()->obtainable();
    pop_heap();
    return available;
}

// 0x004809F0
void ClipStore::prepare_all(uint32_t granularity, int type)
{
    for (AnimCacheEntry*& entry : slot_array_) {
        entry = static_cast<AnimCacheEntry*>(memory::alloc_zeroed(sizeof(AnimCacheEntry)));
        if (entry)
            new (entry) gfx::AnimCacheEntry;
    }
    granularity_ = granularity;
    kind_tag_ = type;
}

// 0x004812F0
void ClipStore::destroy_body()
{
    for (AnimCacheEntry*& entry : slot_array_) {
        if (entry) {
            entry->destroy_body();
            memory::dealloc(entry);
            entry = nullptr;
        }
    }
    taken_count_ = 0;
    granularity_ = 0;
    kind_tag_ = 0;
}

// 0x00480800
int ClipStore::fetch_kind() { return kind_tag_; }

// 0x00480670
uint32_t ClipStore::fetch_record_bytes(uint32_t /*key*/, int rotation_keys, int translation_keys, int type)
{
    uint8_t rotation = 0, translation = 0;
    key_sizes(type, rotation, translation);
    const uint32_t size = rotation * rotation_keys + translation * translation_keys;
    return (size / granularity_ + 1) * granularity_;
}

// 0x00480980
AnimCacheEntry* ClipStore::add_row(uint32_t key, int16_t bones, int rotation_keys, int translation_keys, int type)
{
    int i = 0;
    for (;; ++i) {
        const AnimCacheEntry* entry = slot_array_[i];
        if (entry->lookup_key_ == key)
            return nullptr;
        if (entry->kind_tag_ == 0)
            break;
        if (i + 1 >= kEntries)
            return nullptr;
    }
    const uint32_t allocated = slot_array_[i]->carve(key, bones, rotation_keys, translation_keys, type);
    taken_count_ += allocated;
    if (!allocated)
        return nullptr;
    ++num_;
    return slot_array_[i];
}

// 0x004806C0
uint32_t ClipStore::find_oldest(uint32_t size, uint32_t* users)
{
    uint32_t key = 0;
    uint32_t oldest = frame_clock::get_time();
    for (const AnimCacheEntry* entry : slot_array_) {
        if (entry->allocated_ >= size && entry->last_used_ < oldest && !(entry->flag_bits_ & AnimCacheEntry::kKeep)) {
            *users = entry->flag_bits_ & AnimCacheEntry::kUserMask;
            oldest = entry->last_used_;
            key = entry->lookup_key_;
        }
    }
    return key;
}

// 0x00480ED0
bool ClipStore::free_oldest(uint32_t max_users, uint32_t size)
{
    uint32_t users = 999;
    const uint32_t key = find_oldest(size, &users);
    if (max_users > users && key) {
        drop_out(key);
        return true;
    }
    return false;
}

// 0x00480DB0
void ClipStore::drain(bool unused_only)
{
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->kind_tag_ && (!unused_only || !(entry->flag_bits_ & AnimCacheEntry::kUserMask)))
            taken_count_ -= entry->get_release();
    }
}

// 0x00480DF0
void ClipStore::drop_out(uint32_t key)
{
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->lookup_key_ == key) {
            --num_;
            taken_count_ -= entry->get_release();
        }
    }
}

// 0x00480E30
int ClipStore::add_user(uint32_t key)
{
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->lookup_key_ == key) {
            const int slot = entry->new_slot();
            if (slot == -1)
                return -1;
            entry->flag_bits_ = with_users(entry->flag_bits_, 1);
            return slot;
        }
    }
    return -1;
}

// 0x00480E80
void ClipStore::remove_user(uint32_t key, int slot)
{
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->lookup_key_ == key) {
            entry->free_slot(slot);
            if (!(entry->flag_bits_ & AnimCacheEntry::kUserMask) && evict_unused_)
                drop_out(key);
        }
    }
}

// 0x00480F20
int ClipStore::defragment()
{
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->kind_tag_ && !(entry->flag_bits_ & AnimCacheEntry::kUserMask))
            taken_count_ -= entry->get_release();
    }
    uint32_t total = 0;
    int with_data = 0;
    for (const AnimCacheEntry* entry : slot_array_) {
        if (entry->payload_) {
            total += entry->allocated_;
            ++with_data;
        }
    }
    auto* copy = static_cast<uint8_t*>(memory::alloc(total));
    auto* old_data = static_cast<uint8_t**>(memory::alloc(with_data * 4));
    if (!copy || !old_data) {
        if (copy)
            memory::dealloc(copy);
        for (AnimCacheEntry* entry : slot_array_) {
            if (entry->kind_tag_)
                taken_count_ -= entry->get_release();
        }
        return 0;
    }
    // The keys copied out, in order...
    uint8_t* to = copy;
    uint8_t** old = old_data;
    for (const AnimCacheEntry* entry : slot_array_) {
        if (entry->payload_) {
            copy_forwards(to, entry->payload_, entry->allocated_);
            to += entry->allocated_;
            *old = entry->payload_;
            ++old;
        }
    }
    // ...every entry's freed and its slots cleared, then allocated again (lowest first)...
    uint32_t offset = 0;
    push_anim_heap();
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->kind_tag_) {
            hub()->dealloc(entry->payload_);
            entry->payload_ = nullptr;
            for (AnimCacheSlot* slot : entry->slots_ptr_) {
                if (slot)
                    slot->get_clear();
            }
        }
    }
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->kind_tag_)
            entry->payload_ = static_cast<uint8_t*>(memory::alloc(entry->allocated_));
    }
    pop_heap();
    // ...and the keys copied back, the pointers into them moved with them.
    old = old_data;
    for (AnimCacheEntry* entry : slot_array_) {
        if (entry->kind_tag_) {
            copy_forwards(entry->payload_, copy + offset, entry->allocated_);
            offset += entry->allocated_;
            const int moved = static_cast<int>(entry->payload_ - *old++);
            entry->rotation_keys_ptr_ += moved;
            entry->translation_keys_ptr_ += moved;
        }
    }
    memory::dealloc(copy);
    memory::dealloc(static_cast<void*>(old_data));
    return 0;
}

} // namespace gfx
