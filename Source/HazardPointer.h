#pragma once
#include <atomic>

class HazardPointer final {
public:
    HazardPointer();
    HazardPointer(const HazardPointer& other) = delete; // Copy Constructor
    HazardPointer(HazardPointer&& other) noexcept; // Move Constructor

    ~HazardPointer();
    HazardPointer& operator=(const HazardPointer& other) = delete; // Copy Assignment
    HazardPointer& operator=(HazardPointer&& other) noexcept; // Move Assignment

    static void Retire(void* pOld);

    template<typename T>
    operator T() {
        return (T)_node->ptr;
    }

    template<typename T>
    bool operator==(const T& other) const {
        return (T)_node->ptr == other;
    }

    void operator=(void* value) {
        _node->ptr = value;
    }

private:
    static void Scan();

    struct Node final {
        void* ptr = nullptr;
        std::atomic<bool> active = true;
        Node* next = nullptr;
    };
    static std::atomic<Node*> s_head;
    Node* _node;
};

template<typename T>
bool operator!=(const T& lhs, const HazardPointer& rhs)
{
    return !(rhs == lhs);
}

template<typename T>
bool operator==(const T& lhs, const HazardPointer& rhs)
{
    return rhs == lhs;
}
