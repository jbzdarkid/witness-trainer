#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::make_shared<Trainer>();

    /*
    memory->AddSigScan({0x84, 0xC0, 0x75, 0x59, 0xBA, 0x20, 0x00, 0x00, 0x00}, [trainer](__int64 offset, int index, const std::vector<byte>& data) {
        // This int is actually desired_movement_direction, which immediately preceeds camera_position
        trainer->_cameraPos = Memory::ReadStaticInt(offset, index + 0x19, data) + 0x10;

        // This doesn't have a consistent offset from the scan, so search until we find "mov eax, [addr]"
        for (; index < data.size(); index++) {
            if (data[index - 2] == 0x8B && data[index - 1] == 0x05) {
                trainer->_noclipEnabled = Memory::ReadStaticInt(offset, index, data);
                return true;
            }
        }
        return false;
    });
    */

    // We need to save _memory before we exit, otherwise we can't destroy properly.
    trainer->_memory = memory;

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    return trainer;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
    SetNoclip(false);
}

bool Trainer::GetNoclip() {
    return false; // TODO: Read from some sigscann'd address
}

std::vector<float> Trainer::GetPlayerPos() {
    return _memory->ReadData<float>({0x4A89FC0, 0x50, 0x78, 0x74}, 3);
}

std::vector<float> Trainer::GetPlayerAngle() {
    return _memory->ReadData<float>({0x4A89FC0, 0x50, 0x78, 0x34}, 4);
}

int Trainer::GetHealth() {
    return _memory->ReadData<int>({0x4A89FC0, 0x50, 0xA8, 0x140, 0x94}, 1)[0] / 25;
}

int Trainer::GetMaxHealth() {
    return _memory->ReadData<int>({0x4A89FC0, 0x50, 0xA8, 0x140, 0x98}, 1)[0] / 25;
}

int Trainer::GetCharge() {
    return (int)(_memory->ReadData<float>({0x4A89FC0, 0x50, 0xA8, 0x140, 0xD0}, 1)[0] / 20.0f);
}

int Trainer::GetMaxCharge() {
    return (int)(_memory->ReadData<float>({0x4A89FC0, 0x50, 0xA8, 0x140, 0xD4}, 1)[0] / 20.0f);
}

bool Trainer::GetGodMode() {
    return _memory->ReadData<int>({0x4A89FC0, 0x50, 0xA8, 0x154}, 1)[0] == 1;
}

void Trainer::SetNoclip(bool enable) {
    // TODO: Some sigscan here to defy gravity
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({0x4A89FC0, 0x50, 0x78, 0x74}, pos);
}

void Trainer::SetPlayerAngle(const std::vector<float>& angle) {
    _memory->WriteData<float>({0x4A89FC0, 0x50, 0x78, 0x34}, angle);
}

void Trainer::SetHealth(int health) {
    _memory->WriteData<int>({0x4A89FC0, 0x50, 0xA8, 0x140, 0x94}, { health * 25 });
}

void Trainer::SetMaxHealth(int maxHealth) {
    _memory->WriteData<int>({0x4A89FC0, 0x50, 0xA8, 0x140, 0x98}, { maxHealth * 25 });
}

void Trainer::SetCharge(int charge) {
    _memory->WriteData<float>({0x4A89FC0, 0x50, 0xA8, 0x140, 0xD0}, { charge * 20.0f });
}

void Trainer::SetMaxCharge(int maxCharge) {
    _memory->WriteData<float>({0x4A89FC0, 0x50, 0xA8, 0x140, 0xD4}, { maxCharge * 20.0f });
}

void Trainer::SetGodMode(bool enable) {
    return _memory->WriteData<int>({0x4A89FC0, 0x50, 0xA8, 0x154}, { enable ? 1 : 0 });
}
