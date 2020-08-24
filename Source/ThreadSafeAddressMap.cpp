#include "pch.h"
#include "ThreadSafeAddressMap.h"

void ThreadSafeAddressMap::Clear() {
    std::lock_guard<std::mutex> l(_mutex);
    _map.clear();
}

uintptr_t ThreadSafeAddressMap::Find(uintptr_t key) {
    std::lock_guard<std::mutex> l(_mutex);
    const auto search = _map.find(key);
    return search != std::end(_map) ? search->second : 0;
}

void ThreadSafeAddressMap::Set(uintptr_t key, uintptr_t value) {
    std::lock_guard<std::mutex> l(_mutex);
    _map[key] = value;
}
