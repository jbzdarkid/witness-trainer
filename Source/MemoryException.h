#pragma once

#define MEMORY_TRY \
try { \
    Memory::__canThrow = true;

#define MEMORY_THROW(...) \
    if (Memory::__canThrow) { \
        throw MemoryException(__func__, __LINE__, ##__VA_ARGS__); \
    } else { \
        auto exc = MemoryException(__func__, __LINE__, ##__VA_ARGS__); \
        void DebugPrint(std::string text); \
        DebugPrint(exc.ToString()); \
    }

#define MEMORY_CATCH(action) \
    Memory::__canThrow = false; \
} catch (MemoryException exc) { \
    void DebugPrint(std::string text); \
    DebugPrint(exc.ToString()); \
    action; \
}

class MemoryException {
public:
    inline MemoryException(const char* func, int32_t line, const char* message) noexcept
        : MemoryException(func, line, message, {}, 0) {}
    inline MemoryException(const char* func, int32_t line, const char* message, const std::vector<__int64>& offsets) noexcept
        : MemoryException(func, line, message, offsets, 0) {}
    inline MemoryException(const char* func, int32_t line, const char* message, const std::vector<__int64>& offsets, size_t numItems) noexcept
        : _func(func), _line(line), _message(message), _offsets(offsets), _numItems(numItems) {}

    ~MemoryException() = default;

    std::string ToString() noexcept {
        std::stringstream msg;
        msg << "MemoryException thrown in function ";
        msg << _func;
        msg << " on line " << _line << ":\n";
        msg << _message << "\n";
        if (_offsets.size() > 0) {
            msg << "\nOffsets:";
            for (__int64 offset : _offsets) {
                msg << " " << std::showbase << std::hex << offset;
            }
            msg << "\n";
        }
        if (_numItems != 0) {
            msg << "Num Items: " << _numItems << "\n";
        }
        return msg.str();
    }

private:
    const char* _func;
    int32_t _line;
    const char* _message;
    const std::vector<__int64> _offsets;
    size_t _numItems = 0;
};

