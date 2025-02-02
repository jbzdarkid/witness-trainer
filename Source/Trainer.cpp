#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

const size_t BUFFER_SIZE = 0x100000;

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::make_shared<Trainer>();
    trainer->_memory = memory;

    memory->AddSigScan({0x83, 0xC0, 0x07, 0x83, 0xE0, 0xF8, 0x50}, [trainer](int64_t offset, int index, const std::vector<byte>& data) {
        uint64_t baseAddress = trainer->_memory->GetBaseAddress();
        trainer->_writeSaveFile = (int32_t)(baseAddress + offset + index); // shrug, x86 is built different
    });

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    trainer->HookWriteSaveFile();

    return trainer;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {}

void Trainer::HookWriteSaveFile() {
    int64_t buffer = _memory->AllocateArray(BUFFER_SIZE);
    assert(buffer <= 0xFFFFFFFF, "Failed to allocate read/write buffer in int32 space");
    _buffer = (int32_t)buffer;
    SetWrite(false);

    // [buffer] = TOTAL buffer size
    // [buffer + 4] = Read/Write mode
    // [buffer + 8] = Buffer Data
    _memory->Intercept("WriteSaveFile", _writeSaveFile, _writeSaveFile + 10, {
        // Buffer size is in eax
        // Buffer is at [ebp+8]
        0x53,                                                   // push ebx
        0x51,                                                   // push ecx
        0x52,                                                   // push edx
        0x8A, 0x1D, INT_TO_BYTES(buffer+4),                     // mov bl, [buffer+4]   ; bl = operation mode
        IF_EQ(0x80, 0xFB, Reading),                             // cmp bl, 0x01         ; If we're in read mode
        THEN(
            0xA3, INT_TO_BYTES(buffer),                         // mov [buffer], eax    ; Save BBT buffer size
            0xBB, INT_TO_BYTES(buffer+8),                       // mov ebx, buffer + 8  ; ebx = start of Trainer buffer
            0x8B, 0x4D, 0x08,                                   // mov ecx, [ebp+8]     ; ecx = start of BBT buffer
            DO_WHILE_NOT(
                0x8B, 0x11,                                     // mov edx, [ecx]       ; Read 4 bytes from BBT buffer
                0x89, 0x13,                                     // mov [ebx], edx       ; Write 4 bytes to Trainer buffer
                0x8D, 0x5B, 0x04,                               // lea ebx, [ebx + 4]   ; Increment Trainer buffer ptr by 4
                0x8D, 0x49, 0x04,                               // lea ecx, [ecx + 4]   ; Increment BBT buffer ptr by 4
                0x8D, 0x40, 0xFC,                               // lea eax, [eax - 4]   ; Decrement loop variable by 4
                IF_LE(0x83, 0xF8, 0x00)                         // cmp eax, 0x0         ; Check for end of BBT buffer
            ),
            0x8B, 0x05, INT_TO_BYTES(buffer)                    // mov eax, [buffer]    ; Restore BBT buffer size
        ),
        0x8A, 0x1D, INT_TO_BYTES(buffer+4),                     // mov bl, [buffer+4]   ; bl = operation mode
        IF_EQ(0x80, 0xFB, Writing),                             // cmp bl, 0x02         ; If we're in write mode
        THEN(
            0x8B, 0x05, INT_TO_BYTES(buffer),                   // mov eax, [buffer]    ; Load Trainer buffer size (as loop iterator)
            0xBB, INT_TO_BYTES(buffer+8),                       // mov ebx, buffer + 8  ; ebx = start of Trainer buffer
            0x8B, 0x4D, 0x08,                                   // mov ecx, [ebp+8]     ; ecx = start of BBT buffer
            DO_WHILE_NOT(
                0x8B, 0x13,                                     // mov edx, [ebx]       ; Read 4 bytes from Trainer buffer
                0x89, 0x11,                                     // mov [ecx], edx       ; Write 4 bytes to BBT buffer
                0x8D, 0x5B, 0x04,                               // lea ebx, [ebx + 4]   ; Increment Trainer buffer ptr by 4
                0x8D, 0x49, 0x04,                               // lea ecx, [ecx + 4]   ; Increment BBT buffer ptr by 4
                0x8D, 0x40, 0xFC,                               // lea eax, [eax - 4]   ; Decrement loop variable by 4
                IF_LE(0x83, 0xF8, 0x00)                         // cmp eax, 0x0         ; Check for end of Trainer buffer
            ),
            0x8B, 0x05, INT_TO_BYTES(buffer)                    // mov eax, [buffer]    ; Set BBT buffer size from Trainer size
        ),
        0x5A,                                                   // pop edx
        0x59,                                                   // pop ecx
        0x5B,                                                   // pop ebx
    });
}

void Trainer::SetWrite(bool enabled) {
    _memory->WriteAbsoluteData<Mode>({_buffer + 4}, {enabled ? Mode::Writing : Mode::Reading});
}

SaveData Trainer::GetBuffer() {
    int32_t bufferSize = _memory->ReadAbsoluteData<int32_t>({_buffer}, 1)[0];
    if (bufferSize == 0) return {}; // No savefile has been written yet
    std::vector<byte> raw = _memory->ReadAbsoluteData<byte>({_buffer + 8}, bufferSize);

    std::string buffer(32, '\0');
    for (int i = 0; i < raw.size(); i += 16) {
        for (int j = 0; j < 16 && i + j < raw.size(); j++) {
            byte b = raw[i + j];
            buffer[2 * j]     = "0123456789ABCDEF"[b / 16];
            buffer[2 * j + 1] = "0123456789ABCDEF"[b % 16];
        }
        DebugPrint(buffer);
    }

    if (_lastBuffer.size() > 0)
    {
        std::string buffer;
        std::string buffer2;
        for (int i = 0; i < raw.size(); i++) {
            if (raw[i] != _lastBuffer[i]) {
                buffer += "0123456789ABCDEF"[_lastBuffer[i] / 16];
                buffer += "0123456789ABCDEF"[_lastBuffer[i] % 16];
                buffer2 += "0123456789ABCDEF"[raw[i] / 16];
                buffer2 += "0123456789ABCDEF"[raw[i] % 16];
            } else if (buffer.size() > 0) {
                size_t startIndex = i - buffer.size() / 2;
                int endIndex = i;
                if (startIndex == 8 && endIndex == 9) continue;
                if (startIndex == 760) continue;
                if (startIndex == 764) continue;
                DebugPrint("From index [" + std::to_string(startIndex) + ", " + std::to_string(endIndex) + ")");
                DebugPrint(buffer + "->" + buffer2);

                buffer.clear();
                buffer2.clear();
            }
        }
    }
    _lastBuffer = raw;

    SaveData data = {};

    // *(uint32_t*)&raw[8] is 24 * number of level completions

    data.squareHeads = *(uint64_t*)&raw[40];
    
    data.headShape = raw[717];
    data.headId = raw[718];
    data.subA = (SubWeapon)raw[719];
    data.subB = (SubWeapon)raw[720];

    data.headId = raw[723];
    data.headShape = raw[729];
    data.headId = raw[730];

    data.gems = *(int*)&raw[760]; // 760-763
    data.yarn = *(int*)&raw[764]; // 764-767

    data.totalHeads = *(int*)&raw[1595];

    return data;
}

void Trainer::SetBuffer(const SaveData& saveData) {
    if (_lastBuffer.size() == 0) return;
    *(int*)&_lastBuffer[760] = saveData.gems; // 760-763
    *(int*)&_lastBuffer[764] = saveData.yarn; // 764-767

    _memory->WriteAbsoluteData<int32_t>({_buffer}, {(int32_t)_lastBuffer.size()});
    _memory->WriteAbsoluteData<byte>({_buffer + 8}, _lastBuffer);
}
