#pragma once
#include "ThreadSafeAddressMap.h"

#include <functional>
#include <map>
#include <memory>

enum ProcStatus : WPARAM {
    // Emitted continuously while the randomizer is running and the game is not
    NotRunning,
    // Emitted continuously while the randomizer is running and the game is also running
    Running,
    // Emitted continuously while the randomizer is running and the game is loading
    Loading,

    // Emitted exactly once if game starts while the randomizer is running
    Started,
    // Emitted exactly once if randomizer starts while the game is running (and not in the middle of a load)
    AlreadyRunning,

    // Emitted exactly once if we detect that a save was loaded after ProcStats::Loading
    LoadSave,
    // Emitted exactly once if we detect that a new game was started after ProcStats::Loading
    NewGame,
    // Emitted exactly once if the game stops while the randomizer is running
    Stopped,
};

using byte = unsigned char;

class Memory final : public std::enable_shared_from_this<Memory> {
public:
    Memory(const std::wstring& processName);
    ~Memory();
    void StartHeartbeat(HWND window, UINT message);
    void StopHeartbeat();
    void BringToFront();
    bool IsForeground();

    // Do not attempt to copy this object. Instead, use shared_ptr<Memory>
    Memory(const Memory& memory) = delete;
    Memory& operator=(const Memory& other) = delete;

    // bytesToEOL is the number of bytes from the given index to the end of the opcode.
    // Usually, the target address is last 4 bytes, since it's the destination of the call.
    static __int64 ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t bytesToEOL = 4);
    using ScanFunc = std::function<void(__int64 offset, int index, const std::vector<byte>& data)>;
    using ScanFunc2 = std::function<bool(__int64 offset, int index, const std::vector<byte>& data)>;
    void AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc);
    void AddSigScan2(const std::vector<byte>& scanBytes, const ScanFunc2& scanFunc);
    [[nodiscard]] size_t ExecuteSigScans();

    template<class T>
    inline std::vector<T> ReadData(const std::vector<__int64>& offsets, size_t numItems) {
        std::vector<T> data(numItems);
        if (!_handle) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets), numItems * sizeof(T));
        return data;
    }
    template<class T>
    inline std::vector<T> ReadAbsoluteData(const std::vector<__int64>& offsets, size_t numItems) {
        std::vector<T> data(numItems);
        if (!_handle) return data;
        if (numItems == 0) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets, true), numItems * sizeof(T));
        return data;
    }
    std::string ReadString(std::vector<__int64> offsets);

    template <class T>
    inline void WriteData(const std::vector<__int64>& offsets, const std::vector<T>& data) {
        if (!_handle) return;
        if (data.size() == 0) return;
        WriteDataInternal(&data[0], offsets, sizeof(T) * data.size());
    }

private:
    ProcStatus Heartbeat();
    void Initialize();
    static HWND GetProcessHwnd(DWORD pid);
    static void SetCurrentThreadName(const wchar_t* name);

    void ReadDataInternal(void* buffer, const uintptr_t computedOffset, size_t bufferSize);
    void WriteDataInternal(const void* buffer, const std::vector<__int64>& offsets, size_t bufferSize);
    uintptr_t ComputeOffset(std::vector<__int64> offsets, bool absolute = false);

    // Parts of the constructor / StartHeartbeat
    std::wstring _processName;
    bool _threadActive = false;
    std::thread _thread;

    // Parts of Initialize / heartbeat
    HANDLE _handle = nullptr;
    DWORD _pid = 0;
    HWND _hwnd = NULL;
    uintptr_t _baseAddress = 0;
    uintptr_t _endOfModule = 0;

    // These variables are all used to track game state
    __int64 _globals = 0;
    int _loadCountOffset = 0;
    __int64 _coreTimeInfo = 0;
    __int64 _previousEntityManager = 0;
    int _previousLoadCount = 0;
    double _previousTime = 0.0;
    bool _gameHasStarted = false;
    bool _trainerHasStarted = false;
    bool _wasLoading = true; // By default, assume the game in case we start in the middle of a load.

#ifdef NDEBUG
    static constexpr std::chrono::milliseconds s_heartbeat = std::chrono::milliseconds(100);
#else // Induce more stress in debug, to catch errors more easily.
    static constexpr std::chrono::milliseconds s_heartbeat = std::chrono::milliseconds(10);
#endif

    // Parts of Read / Write / Sigscan
    ThreadSafeAddressMap _computedAddresses;

    struct SigScan {
        bool found = false;
        ScanFunc2 scanFunc;
    };
    std::map<std::vector<byte>, SigScan> _sigScans;
};
