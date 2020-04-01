#pragma once

#define MEMORY_TRY \
try { \
    Memory::__canThrow = true;

#define MEMORY_THROW(...) \
    if (Memory::__canThrow) { \
        throw MemoryException(__func__, __LINE__, ##__VA_ARGS__); \
    } else { \
        MemoryException::HandleException(MemoryException(__func__, __LINE__, ##__VA_ARGS__)); \
    }

#define MEMORY_CATCH(action) \
    Memory::__canThrow = false; \
} catch (MemoryException exc) { \
    MemoryException::HandleException(exc); \
    action; \
}

class MemoryException : public std::exception {
public:
    inline MemoryException(const char* func, int32_t line, const char* message) noexcept
        : MemoryException(func, line, message, {}, 0) {}
    inline MemoryException(const char* func, int32_t line, const char* message, const std::vector<__int64>& offsets) noexcept
        : MemoryException(func, line, message, offsets, 0) {}
    inline MemoryException(const char* func, int32_t line, const char* message, const std::vector<__int64>& offsets, size_t numItems) noexcept
        : _func(func), _line(line), _message(message), _offsets(offsets), _numItems(numItems) {}

    ~MemoryException() = default;
    inline const char* what() const noexcept {
        return _message;
    }

    static void HandleException(const MemoryException& exc) noexcept {
        std::string msg = "MemoryException thrown in function ";
        msg += exc._func;
        msg += " on line " + std::to_string(exc._line) + ":\n" + exc._message + "\nOffsets:";
        for (__int64 offset : exc._offsets) {
            msg += " " + std::to_string(offset);
        }
        msg += "\n";
        if (exc._numItems != 0) {
            msg += "Num Items: " + std::to_string(exc._numItems) + "\n";
        }
        OutputDebugStringA(msg.c_str());
    }

private:
    const char* _func;
    int32_t _line;
    const char* _message;
    const std::vector<__int64> _offsets;
    size_t _numItems = 0;
};

