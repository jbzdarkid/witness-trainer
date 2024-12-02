#pragma once
#include "SaveData.h"

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);
    ~Trainer();

    enum Mode : byte {
        Reading = 0x1,
        Writing = 0x2,
    };
    void SetWrite(bool enabled);

    SaveData GetBuffer();
    void SetBuffer(const SaveData& saveData);

private:
    std::shared_ptr<Memory> _memory;

    void HookWriteSaveFile();

    int32_t _writeSaveFile = 0;
    int32_t _buffer = 0;
};
