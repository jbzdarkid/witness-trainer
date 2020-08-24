#include "HazardPointer.h"
#include <atomic>
#include <map>

// Thread-safe map (from Andrei Alexandrescu and Maged Michael)
// http://erdani.com/publications/cuj-2004-12.pdf

class ThreadSafeAddressMap {
public:
    using Map = std::map<uintptr_t, uintptr_t>;

    void Set(uintptr_t key, uintptr_t value) {
        Map* newMap = nullptr;
        void* oldMap = _map.load();
        do {
            if (newMap) delete newMap;
            if (oldMap) {
                newMap = new Map(*(Map*)oldMap);
            } else {
                newMap = new Map();
            }
            (*newMap)[key] = value;
            // Note that std::atomic *cannot* guarantee the atomicity of this swap
            // with regards to the contents of the std::maps. All it can guarantee is that
            // the pointers are what expect -- which is why we have to cast the std::maps
            // down to void* before attemping the swap.
        } while (!_map.compare_exchange_strong(oldMap, (void*)newMap));
        HazardPointer::Retire(oldMap);
    }

    uintptr_t Find(uintptr_t key) const {
        HazardPointer hazardPointer = LoadMap();
        Map* map = (Map*)hazardPointer;
        if (map == nullptr) return false;

        auto search = map->find(key);
        return (search != map->end() ? search->second : 0);
    }

    size_t Size() const {
        HazardPointer hazardPointer = LoadMap();
        Map* map = (Map*)hazardPointer;
        return (map != nullptr ? map->size() : 0);
    }

    void Clear() {
        void* oldMap = _map.load();
        while (!_map.compare_exchange_strong(oldMap, (void*)nullptr));
        HazardPointer::Retire(oldMap);
    }

private:
    std::atomic<void*> _map = nullptr;

    // Note: The HazardPointer must be retained until we're done acting on it.
    // Thus, we return a HazardPointer rather than a Map*, to force the
    // enclosing scope to keep the object alive. That said, this isn't really
    // a guarantee. Callers should still be careful.
    HazardPointer LoadMap() const {
        HazardPointer hazardPointer;
        do {
            hazardPointer = _map.load();
            // Although it may seem extraneous to check the map again, we can
            // potentially get interrupted after calling load, but before
            // assigning to the hazard pointer. In this case, we might think
            // we've acquired pMap, but in fact the object we're looking at
            // has been overwritten and freed, and we would be taking a dead
            // reference.
        } while (_map.load() != hazardPointer);
        return std::move(hazardPointer);
    }
};
