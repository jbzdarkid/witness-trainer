#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::make_shared<Trainer>();
    trainer->_memory = memory;

    memory->AddSigScan({0x80, 0xBD, 0x00, 0x01, 0x00, 0x00, 0x00}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        __int64 getGameWorld = Memory::ReadStaticInt(offset, index + 11, data);
        trainer->_gameWorldPtr = memory->ReadData<int>({getGameWorld + 1}, 1)[0];
    });

    memory->AddSigScan({0x83, 0xEC, 0x1C, 0x56, 0x6A, 0x2C}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        __int64 getGlobalSettings = Memory::ReadStaticInt(offset, index + 7, data);
        trainer->_globalSettingsPtr = memory->ReadData<int>({getGlobalSettings + 1}, 1)[0];
    });

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    return trainer;
}

bool Trainer::GetNoclip() {
    return false; // TODO: Read from some sigscann'd address
}

std::vector<float> Trainer::GetPlayerPos() {
    return _memory->ReadData<float>({_gameWorldPtr, 0x50, 0x78, 0x74}, 3, true);
}

std::vector<float> Trainer::GetPlayerAngle() {
    return _memory->ReadData<float>({_gameWorldPtr, 0x50, 0x78, 0x34}, 4, true);
}

int Trainer::GetHealth() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x94}, 1, true)[0] / 25;
}

int Trainer::GetMaxHealth() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x98}, 1, true)[0] / 25;
}

int Trainer::GetCharge() {
    return (int)(_memory->ReadData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD0}, 1, true)[0] / 20.0f);
}

int Trainer::GetMaxCharge() {
    return (int)(_memory->ReadData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD4}, 1, true)[0] / 20.0f);
}

bool Trainer::GetGodMode() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x154}, 1, true)[0] == 1;
}

bool Trainer::GetShowCollision() { 
    return _memory->ReadData<int>({_globalSettingsPtr, 0x4, 0x3D*4}, 1, true)[0] == 44;
}

std::string Trainer::GetLevelName() {
    _memory->ClearComputedAddress({_gameWorldPtr, 0x4C}, true); // Level pointer cannot be cached
    return _memory->ReadString({_gameWorldPtr, 0x4C, 0xD4, 0}, true);
}

void Trainer::SetNoclip(bool enable) {
    // TODO: Some sigscan here to defy gravity
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0x78, 0x74}, pos, true);
}

void Trainer::SetPlayerAngle(const std::vector<float>& angle) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0x78, 0x34}, angle, true);
}

void Trainer::SetHealth(int health) {
    _memory->WriteData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x94}, { health * 25 }, true);
}

void Trainer::SetMaxHealth(int maxHealth) {
    _memory->WriteData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x98}, { maxHealth * 25 }, true);
}

void Trainer::SetCharge(int charge) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD0}, { charge * 20.0f }, true);
}

void Trainer::SetMaxCharge(int maxCharge) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD4}, { maxCharge * 20.0f }, true);
}

void Trainer::SetGodMode(bool enable) {
    return _memory->WriteData<int>({_gameWorldPtr, 0x50, 0xA8, 0x154}, { enable ? 1 : 0 }, true);
}

void Trainer::SetShowCollision(bool enable) {
    _memory->WriteData<int>({_globalSettingsPtr, 0x4, 0x3D*4}, { enable ? 44 : 0 }, true);
}