#pragma once
#include <mutex>
#include <unordered_map>

class ThreadSafeAddressMap final {
public:
    void Clear();
    uintptr_t Find(uintptr_t key);
    void Set(uintptr_t key, uintptr_t value);

private:
    std::mutex _mutex;
    std::unordered_map<uintptr_t, uintptr_t> _map;
};
