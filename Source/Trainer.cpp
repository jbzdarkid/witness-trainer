#include "pch.h"
#include "Trainer.h"

int Trainer::ReadStaticInt(int offset, int index, const std::vector<byte>& data) {
    return offset + index + 0x4 + *(int*)&data[index]; // (address of next line) + (index interpreted as 4byte int)
}

Trainer::Trainer(const std::shared_ptr<Memory>& memory) : _memory(memory){
    _memory->AddSigScan({0x84, 0xC0, 0x75, 0x59, 0xBA, 0x20, 0x00, 0x00, 0x00}, [&](int offset, int index, std::vector<byte>& data){
        // This int is actually desired_movement_direction, which immediately preceeds camera_position
        _cameraPos = ReadStaticInt(offset, index + 0x19, data) + 0x10;

        // This doesn't have a consistent offset from the scan, so search until we find "mov eax, [addr]"
        while (true) {
            index++;
            if (data[index-2] == 0x8B && data[index-1] == 0x05) break;
        }
        _noclipEnabled = ReadStaticInt(offset, index, data);
        return false; // Data was unmodified
    });

    _memory->AddSigScan({0xC7, 0x45, 0x77, 0x00, 0x00, 0x80, 0x3F, 0xC7, 0x45, 0x7F, 0x00, 0x00, 0x80, 0x3F}, [&](int offset, int index, std::vector<byte>& data){
        _cameraAng = ReadStaticInt(offset, index + 0x17, data);
        return false; // Data was unmodified
    });

    _memory->AddSigScan({0x0F, 0x29, 0x7C, 0x24, 0x70, 0x44, 0x0F, 0x29, 0x54, 0x24, 0x60}, [&](int offset, int index, std::vector<byte>& data){
        _noclipSpeed = ReadStaticInt(offset, index + 0x4F, data);
        return false; // Data was unmodified
    });

    _memory->AddSigScan({0x76, 0x09, 0xF3, 0x0F, 0x11, 0x05}, [&](int offset, int index, std::vector<byte>& data){
        _fovCurrent = ReadStaticInt(offset, index + 0x0F, data);
        return false; // Data was unmodified
    });

    _memory->ExecuteSigScans();
}

bool Trainer::GetNoclip() {
    if (_noclipEnabled == 0) return false;
    return (bool)_memory->ReadData<int>({_noclipEnabled}, 1)[0];
}

float Trainer::GetNoclipSpeed() {
    if (_noclipSpeed == 0) return 0.0f;
    return _memory->ReadData<float>({_noclipSpeed}, 1)[0];
}

std::vector<float> Trainer::GetCameraPos() {
    if (_cameraPos == 0) return {0.0f, 0.0f, 0.0f};
    return _memory->ReadData<float>({_cameraPos}, 3);
}

std::vector<float> Trainer::GetCameraAng() {
    if (_cameraAng == 0) return {0.0f, 0.0f};
    return _memory->ReadData<float>({_cameraAng}, 2);
}

float Trainer::GetFov() {
    if (_fovCurrent == 0) return 0.0f;
    return _memory->ReadData<float>({_fovCurrent}, 1)[0];
}

void Trainer::SetNoclip(bool enabled) {
    if (_noclipEnabled == 0) return;
    _memory->WriteData<byte>({_noclipEnabled}, {(byte)enabled});
}

void Trainer::SetNoclipSpeed(float speed) {
    if (_noclipSpeed == 0) return;
    _memory->WriteData<float>({_noclipSpeed}, {speed});
}

void Trainer::SetCameraPos(const std::vector<float>& pos) {
    if (_cameraPos == 0) return;
    _memory->WriteData<float>({_cameraPos}, pos);
}

void Trainer::SetCameraAng(const std::vector<float>& ang) {
    if (_cameraAng == 0) return;
    _memory->WriteData<float>({_cameraAng}, ang);
}

void Trainer::SetFov(float fov) {
    if (_fovCurrent == 0) return;
    _memory->WriteData<float>({_fovCurrent}, {fov});
}
