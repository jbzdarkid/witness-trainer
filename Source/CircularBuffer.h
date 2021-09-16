#pragma once
#include <typeinfo>
#include <cassert>

template <typename T>
class CircularBuffer {
public:
    CircularBuffer(size_t size) : _size(size) {
        _data = new T[size];

    }

    ~CircularBuffer() {
        delete[] _data;
    }

    size_t Size() const {
        return _size;
    }

    T Get(size_t index) const {
        if (index >= _size) index = _size - 1; // You cannot ask for an element beyond the size of the array
        if (index > _index) index = _index - 1; // You cannot ask for an element that is not yet filled

        if (_index < _size) return _data[index]; // If the array is not yet filled. Idk.

        // data = [0, 1, 2, 3, _] (size = 5, index = 4)
        // Get(0) = 0 -> data[(4 + 0) % 5] = data[4] !!!
        // Get(4) = 3 -> data[3]
        // data = [8, 1, 2, 3, 4] (size = 5, index = 6)
        // Get(0) = 1 -> data[(6 + 0) % 5] = data[1]
        // Get(4) = 8 -> data[(6 + 4) % 5] = data[0]
        // data = [8, 9, 2, 3, 4] (size = 5, index = 7)
        // Get(0) = 2 -> data[(7 + 0) % 5] = data[2]
        // Get(3) = 8 -> data[(7 + 3) % 5] = data[0]
        // data = [8, 9, 10, 11, 4] (size = 5, index = 9)
        // Get(0) = 4 -> data[(9 + 0) % 5] = data[4]
        // Get(1) = 8 -> data[(9 + 1) % 5] = data[0]
        // Get(4) = 11 -> data[(9 + 4) % 5] = data[3]
        return _data[(_index + index) % _size];
    }

    void Add(T value) {
        _data[(_index++) % _size] = value;
    }

    // RO5
    CircularBuffer(const CircularBuffer& other) = delete;
    CircularBuffer(CircularBuffer&& other) = delete;
    CircularBuffer& operator=(const CircularBuffer& other) = delete;
    CircularBuffer& operator=(CircularBuffer&& other) = delete;

private:
    T* _data = 0;
    size_t _index = 0;
    size_t _size = 0;
};