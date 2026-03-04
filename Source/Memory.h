#pragma once
#include "ThreadSafeAddressMap.h"

/* State graph. Entry states are NotRunning, Running, and Loading.
 * It is not recommended to act on the "Loading" state, except to gray out buttons.

                   Stopped
                      |
                      |
                      v
                  NotRunning
                      |
                      |
                      v
        +<-------- Started -------->+
        |             |             |
        |             |             |
        v             v             v
     Running <---> Loading <---> Reload <---> (Running)
        |             |             |
        |             |             |
        v             v             v
        +-------> (Stopped) <-------+

*/

enum ProcStatus : WPARAM {
    NotRunning,
    Started,
    Running,
    Loading,
    Reload,
    Stopped,
};

using byte = unsigned char;

// Note: Little endian
#define LONG_TO_BYTES(val) \
	static_cast<byte>((val & 0x00000000000000FF) >> 0x00), \
	static_cast<byte>((val & 0x000000000000FF00) >> 0x08), \
	static_cast<byte>((val & 0x0000000000FF0000) >> 0x10), \
	static_cast<byte>((val & 0x00000000FF000000) >> 0x18), \
	static_cast<byte>((val & 0x000000FF00000000) >> 0x20), \
	static_cast<byte>((val & 0x0000FF0000000000) >> 0x28), \
	static_cast<byte>((val & 0x00FF000000000000) >> 0x30), \
	static_cast<byte>((val & 0xFF00000000000000) >> 0x38)

// Note: Little endian
#define INT_TO_BYTES(val) \
	static_cast<byte>((val & 0x000000FF) >> 0x00), \
	static_cast<byte>((val & 0x0000FF00) >> 0x08), \
	static_cast<byte>((val & 0x00FF0000) >> 0x10), \
	static_cast<byte>((val & 0xFF000000) >> 0x18)

#define IF_GE(...) __VA_ARGS__, 0x72 // jb
#define IF_LT(...) __VA_ARGS__, 0x73 // jae
#define IF_NE(...) __VA_ARGS__, 0x74 // je
#define IF_NZ IF_NE
#define IF_EQ(...) __VA_ARGS__, 0x75 // jne
#define IF_Z IF_EQ
#define IF_GT(...) __VA_ARGS__, 0x76 // jbe
#define IF_LE(...) __VA_ARGS__, 0x77 // ja
#define THEN(...) ARGCOUNT(__VA_ARGS__), __VA_ARGS__
#define SKIP(...) 0xEB, ARGCOUNT(__VA_ARGS__), __VA_ARGS__ // jmp

#define ARGCOUNT(...) std::tuple_size<decltype(std::make_tuple(__VA_ARGS__))>::value
#define DO_WHILE_NONZERO(...) __VA_ARGS__, 0x75, static_cast<byte>(-2 - ARGCOUNT(__VA_ARGS__)) // Must end on a 'dec' instruction to set ZF correctly.

class Memory final : public std::enable_shared_from_this<Memory> {
public:
    Memory(const std::wstring& processName);
    ~Memory();
    void StartHeartbeat(HWND window, UINT message);
    void StopHeartbeat();
    void BringToFront();
    bool IsForeground();

    static HWND GetProcessHwnd(DWORD pid);

    Memory(const Memory& memory) = delete;
    Memory& operator=(const Memory& other) = delete;

    // bytesToEOL is the number of bytes from the given index to the end of the opcode. Usually, the target address is last 4 bytes, since it's the destination of the call.
    static __int64 ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t bytesToEOL = 4);
    using ScanFunc = std::function<void(__int64 offset, int index, const std::vector<byte>& data)>;
    using ScanFunc2 = std::function<bool(__int64 offset, int index, const std::vector<byte>& data)>;
    void AddSigScan(const std::string& scanHex, const ScanFunc& scanFunc);
    void AddSigScan2(const std::string& scanHex, const ScanFunc2& scanFunc);
    [[nodiscard]] size_t ExecuteSigScans();

    std::string ReadString(const std::vector<__int64>& offsets);

    template<class T>
    inline std::vector<T> ReadData(const std::vector<__int64>& offsets, size_t numItems) {
        std::vector<T> data(numItems);
        if (!_handle) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets), numItems * sizeof(T));
        return data;
    }

    template <class T>
    inline void WriteData(const std::vector<__int64>& offsets, const std::vector<T>& data) {
        WriteDataInternal(&data[0], ComputeOffset(offsets), sizeof(T) * data.size());
    }

    void ClearComputedAddress(const std::vector<__int64>& offsets);
    void Intercept(const std::string& name, __int64 firstLine, __int64 nextLine, const std::vector<byte>& data, bool writeOriginalCode = true);
    void Unintercept(const std::string& name);
    uintptr_t AllocateArray(__int64 size);

    // This is the fully typed function -- you mostly won't need to call this.
    int CallFunction(__int64 address,
        const __int64 rcx, const __int64 rdx, const __int64 r8, const __int64 r9,
        const float xmm0, const float xmm1, const float xmm2, const float xmm3);
    int CallFunction(__int64 address, __int64 rcx) { return CallFunction(address, rcx, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f); }
    int CallFunction(__int64 address, __int64 rcx, __int64 rdx, __int64 r8, __int64 r9) { return CallFunction(address, rcx, rdx, r8, r9, 0.0f, 0.0f, 0.0f, 0.0f); }
    int CallFunction(__int64 address, __int64 rcx, const float xmm1) { return CallFunction(address, rcx, 0, 0, 0, 0.0f, xmm1, 0.0f, 0.0f); }
    int CallFunction(__int64 address, const std::string& str, __int64 rdx);

private:
    void Heartbeat(HWND window, UINT message);
    HANDLE Initialize();

    void ReadDataInternal(void* buffer, const uintptr_t computedOffset, size_t bufferSize);
    void WriteDataInternal(const void* buffer, uintptr_t computedOffset, size_t bufferSize);
    uintptr_t ComputeOffset(const std::vector<__int64>& offsets);
    uintptr_t ResolvePointerPath(const std::vector<__int64>& offsets);

    // Parts of the constructor / StartHeartbeat
    std::wstring _processName;
    bool _threadActive = false;
    std::thread _thread;

    // Parts of Initialize / heartbeat
    HANDLE _handle = nullptr;
    DWORD _pid = 0;
    uintptr_t _baseAddress = 0;
    uintptr_t _endOfModule = 0;
    size_t _pointerSize = 0;
    HWND _hwnd = NULL;
    __int64 _globals = 0;
    int _loadCountOffset = 0;
    __int64 _previousEntityManager = 0;
    bool _firstHeartbeat = true;

#ifdef NDEBUG
    static constexpr std::chrono::milliseconds s_heartbeat = std::chrono::milliseconds(100);
#else // Induce more stress in debug, to catch errors more easily.
    static constexpr std::chrono::milliseconds s_heartbeat = std::chrono::milliseconds(10);
#endif


    // Parts of Read / Write / Sigscan / etc
    uintptr_t _functionPrimitive = 0;
    ThreadSafeAddressMap _computedAddresses;

    struct SigScan {
        bool found = false;
        std::vector<byte> bytes;
        ScanFunc2 scanFunc;

        static std::vector<byte> GetScanBytes(const std::string& scanHex);
    };
    std::vector<SigScan> _sigScans;

    struct Interception {
        std::string name;
        int64_t firstLine;
        std::vector<byte> replacedCode;
        uintptr_t addr;
    };
    std::vector<Interception> _interceptions;
};
