#include "pch.h"
#include "Trainer.h"

int Trainer::ReadStaticInt(int offset, int index, const std::vector<byte>& data) {
    return offset + index + 0x4 + *(int*)&data[index]; // (address of next line) + (index interpreted as 4byte int)
}

Trainer::Trainer(const std::shared_ptr<Memory>& memory) : _memory(memory){
    _memory->AddSigScan({0x84, 0xC0, 0x75, 0x59, 0xBA, 0x20, 0x00, 0x00, 0x00}, [&](int offset, int index, const std::vector<byte>& data){
        // This int is actually desired_movement_direction, which immediately preceeds camera_position
        _cameraPos = ReadStaticInt(offset, index + 0x19, data) + 0x10;

        // This doesn't have a consistent offset from the scan, so search until we find "mov eax, [addr]"
        while (true) {
            index++;
            if (data[index-2] == 0x8B && data[index-1] == 0x05) break;
        }
        _noclipEnabled = ReadStaticInt(offset, index, data);
    });

    _memory->AddSigScan({0xC7, 0x45, 0x77, 0x00, 0x00, 0x80, 0x3F, 0xC7, 0x45, 0x7F, 0x00, 0x00, 0x80, 0x3F}, [&](int offset, int index, const std::vector<byte>& data){
        _cameraAng = ReadStaticInt(offset, index + 0x17, data);
    });

    _memory->AddSigScan({0x0F, 0x29, 0x7C, 0x24, 0x70, 0x44, 0x0F, 0x29, 0x54, 0x24, 0x60}, [&](int offset, int index, const std::vector<byte>& data){
        _noclipSpeed = ReadStaticInt(offset, index + 0x4F, data);
    });

    _memory->AddSigScan({0x76, 0x09, 0xF3, 0x0F, 0x11, 0x05}, [&](int offset, int index, const std::vector<byte>& data){
        _fovCurrent = ReadStaticInt(offset, index + 0x0F, data);
    });

    _memory->AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [&](int offset, int index, const std::vector<byte>& data){
        _globals = ReadStaticInt(offset, index + 0x14, data);
    });

    _memory->AddSigScan({0x84, 0xC0, 0x74, 0x19, 0x0F, 0x2F, 0xB7}, [&](int offset, int index, const std::vector<byte>& data) {
        _doorOpen = offset + index + 0x0B;
        _solvedTargetOffset = *(int*)&data[index + 0x07];
    });

    // This one is if you haven't run doors injection
    _memory->AddSigScan({0x84, 0xC0, 0x74, 0x11, 0x0F, 0x2F, 0xBF}, [&](int offset, int index, const std::vector<byte>& data) {
        _doorClose = offset + index + 0x04;
    });

    // And this one is if you have run doors injection
    _memory->AddSigScan({0x84, 0xC0, 0x74, 0x11, 0x3A, 0x87}, [&](int offset, int index, const std::vector<byte>& data) {
        _doorClose = offset + index + 0x04;
    });

    // Improve this? Something awkward is happening if you solve while the doors are closing.
    _memory->AddSigScan({0x76, 0x18, 0x48, 0x8B, 0xCF, 0x89, 0x9F}, [&](int offset, int index, const std::vector<byte>& data) {
        _powerOn = offset + index + 0x37;
    });

    _memory->AddSigScan({0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x48, 0x8B, 0x3D}, [&](int offset, int index, const std::vector<byte>& data) {
        _campaignState = ReadStaticInt(offset, index + 0x27, data);
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

bool Trainer::CanSave() {
    if (_campaignState == 0) return true;
    // bool do_not_save;
    return _memory->ReadData<byte>({_campaignState, 0x50}, 1)[0] == 0x00;
}

bool Trainer::GetRandomDoorsPractice() {
    if (_doorOpen == 0) return false;
    return _memory->ReadData<byte>({_doorOpen}, 1)[0] == 0xEB;
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

void Trainer::SetCanSave(bool canSave) {
    if (_campaignState == 0) return;
    _memory->WriteData<byte>({_campaignState, 0x50}, {canSave ? (byte)0x00 : (byte)0x01});
}

void Trainer::SetRandomDoorsPractice(bool enable) {
    if (_globals == 0 || _solvedTargetOffset == 0 || _doorOpen == 0 || _doorClose == 0 || _powerOn == 0) return;

    int hasEverBeenSolvedOffset = _solvedTargetOffset + 0x04;
    int idToPowerOffset = _solvedTargetOffset + 0x20;

    if (enable) {
        // When the panel opens, regardless of whether the panel is solved, reset it and power it on.
        _memory->WriteData<byte>({_doorOpen}, {0xEB, 0x08});
        // When the panel closes, if the puzzle is solved, turn it off
        _memory->WriteData<byte>({_doorClose}, {
            0x3A, 0x87, 0x00, 0x00, 0x00, 0x00, // cmp esi, dword ptr ds:[rdi + <offset>]
            0x90, // nop
            0x77 // ja
        });
        _memory->WriteData<int>({_doorClose + 0x02}, {hasEverBeenSolvedOffset});
        // When the panel is solved, power nothing.
        _memory->WriteData<int>({_globals, 0x18, 0x1983*8, idToPowerOffset}, {0x00000000});
        _memory->WriteData<int>({_globals, 0x18, 0x1987*8, idToPowerOffset}, {0x00000000});
        // When the panel powers on, mark it as having never been solved
        _memory->WriteData<byte>({_powerOn}, {
            0x48, 0x63, 0x00, // movsxd rax, dword ptr [rax]
            0xC6, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, // mov byte ptr ds:[rdi + <offset>], 0x00
            0x66, 0x90, // nop
        });
        _memory->WriteData<int>({_powerOn + 0x05}, {hasEverBeenSolvedOffset});
    } else {
        // When the panel opens, if it is not solved, reset it and power it on
        _memory->WriteData<byte>({_doorOpen}, {0x76, 0x08});
        // When the panel closes, if the puzzle has been solved, turn it off
        _memory->WriteData<byte>({_doorClose}, {
            0x0F, 0x2F, 0xBF, 0x00, 0x00, 0x00, 0x00, // comiss xmm7, dword ptr ds:[rdi + <offset>]
            0x76 // jbe
        });
        _memory->WriteData<int>({_doorClose + 0x03}, {_solvedTargetOffset});
        // When the panel is solved, power the double doors.
        _memory->WriteData<int>({_globals, 0x18, 0x1983*8, idToPowerOffset}, {0x00017C68});
        _memory->WriteData<int>({_globals, 0x18, 0x1987*8, idToPowerOffset}, {0x00017C68});
        // When the panel powers on, do not modify its previously-solved state
        _memory->WriteData<byte>({_powerOn}, {
            0x48, 0x85, 0xC0, // test rax, rax
            0x74, 0x5C, // je +0x5C
            0x48, 0x63, 0x00, // movsxd rax, dword ptr [rax]
            0x85, 0xC0, // test eax, eax
            0x74, 0x55, // je +0x55
        });
    }
}
