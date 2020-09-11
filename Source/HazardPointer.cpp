#include "pch.h"
#include "HazardPointer.h"
#include <set>
#include <vector>

using namespace std;

atomic<HazardPointer::Node*> HazardPointer::s_head;

thread_local std::vector<void*> _retiredList;

void HazardPointer::Retire(void* pOld) {
    _retiredList.push_back(pOld);
    Scan();
}

void HazardPointer::Scan() {
    // Stage 1: Scan the hazard pointers list, collecting all nonnull ptrs
    std::set<void*> hazardPointers;
    for (auto* node = s_head.load(); node != nullptr; node = node->next) {
        if (node->ptr) hazardPointers.insert(node->ptr);
    }

    // Stage 2 (sort the list) isn't required when using std::set.

    // Stage 3: Search for them!
    for (auto it = _retiredList.begin(); it != _retiredList.end(); ) {
        if (hazardPointers.find(*it) != hazardPointers.end()) {
            delete *it;
            it = _retiredList.erase(it);
        } else {
            ++it;
        }
    }
}

HazardPointer::HazardPointer() {
    // Try to reuse a retired node
    for (_node = s_head.load(); _node != nullptr; _node = _node->next) {
        bool expected = false;
        if (!_node->active.compare_exchange_strong(expected, true)) continue;
        return;
    }

    // Else, there are no free nodes, create a new one
    _node = new HazardPointer::Node();
    HazardPointer::Node* old;
    do {
        old = s_head.load();
        _node->next = old;
    } while (!s_head.compare_exchange_strong(old, _node));
}

HazardPointer::HazardPointer(HazardPointer&& other) noexcept {
    _node = other._node;
    other._node = nullptr;
}

HazardPointer::~HazardPointer() {
    if (_node) {
        _node->ptr = nullptr;
        _node->active = false;
    }
}

HazardPointer& HazardPointer::operator=(HazardPointer&& other) noexcept {
    _node = other._node;
    other._node = nullptr;
    return *this;
}
