#include "HazardPointer.h"
#include <mutex>
#include <map>

class ThreadSafeAddressMap {
public:
    void Set(uintptr_t key, uintptr_t value) {
        std::lock_guard<std::mutex> l(_mutex);
        _map[key] = value;
    }

    uintptr_t Find(uintptr_t key) const {
        std::lock_guard<std::mutex> l(_mutex);
        auto search = _map.find(key);
        return (search == std::end(_map) ? 0 : search->second);
    }

    size_t Size() const {
        std::lock_guard<std::mutex> l(_mutex);
        return _map.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> l(_mutex);
        _map.clear();
    }

private:
    mutable std::mutex _mutex;
    std::unordered_map<uintptr_t, uintptr_t> _map;
};
