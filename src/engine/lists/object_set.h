#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>
#include "engine/core/checked_index.h"

// NOT ORIGINAL - a set of objects known by their address. The original kept these in a lists::KeyMap keyed by each
// object's own address (the flashes, spawned polygons, splat sets and recent splats, shatters, a lamp's sectors):
// a set in all but name. Here the keys are the pointers themselves (exact - no 32-bit address cells, nothing hashed),
// in the order the objects were added. The tables' order was the hash buckets' (the addresses' low bits), which
// depends on where the heap put each object - no game behaviour rests on it.
namespace lists {

template <typename T>
class ObjectSet {
public:
    // Added (nothing if it is in already: the original's key could only be there once per object).
    void insert(T* object)
    {
        if (!contains(object)) {
            objects_.push_back(object);
        }
    }
    // Taken out (nothing if it isn't in).
    void erase(const T* object)
    {
        const auto found = std::find(objects_.begin(), objects_.end(), object);
        if (found != objects_.end()) {
            objects_.erase(found);
        }
    }
    [[nodiscard]] bool contains(const T* object) const
    {
        return std::find(objects_.begin(), objects_.end(), object) != objects_.end();
    }
    void clear() { objects_.clear(); }
    [[nodiscard]] bool empty() const { return objects_.empty(); }
    [[nodiscard]] size_t size() const { return objects_.size(); }

    // The objects in order, for a walk that changes no membership (a range-for).
    [[nodiscard]] auto begin() const { return objects_.begin(); }
    [[nodiscard]] auto end() const { return objects_.end(); }

    // `visit(object)` for each object; a visit may take its own object out of the set (the original's walks fetched
    // the next item before dropping the current one).
    template <typename Visit>
    void for_each(const Visit& visit)
    {
        for (size_t i = 0; i < objects_.size();) {
            const T* const visited = core::at(objects_, i);
            visit(core::at(objects_, i));
            if (i < objects_.size() && core::at(objects_, i) == visited) {
                ++i;
            }
        }
    }
    // The first object `match(object)` is true for, or null.
    template <typename Match>
    [[nodiscard]] T* find_if(const Match& match) const
    {
        const auto found = std::find_if(objects_.begin(), objects_.end(), match);
        return found == objects_.end() ? nullptr : *found;
    }

private:
    std::vector<T*> objects_;
};

} // namespace lists
