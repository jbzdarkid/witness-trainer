#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

Trainer::Trainer(std::shared_ptr<Memory> memory) : _memory(memory) {
    _memory->AddSigScan("80 BD 00 01 00 00 00", [this](__int64 offset, int index, const std::vector<byte>& data) {
        __int64 getGameWorld = Memory::ReadStaticInt(offset, index + 11, data);
        _gameWorldPtr = _memory->ReadData<int>({getGameWorld + 1}, 1)[0];
    });

    _memory->AddSigScan("83 EC 1C 56 6A 2C", [this](__int64 offset, int index, const std::vector<byte>& data) {
        __int64 getGlobalSettings = Memory::ReadStaticInt(offset, index + 7, data);
        _globalSettingsPtr = _memory->ReadData<int>({getGlobalSettings + 1}, 1)[0];
    });

    _memory->AddSigScan("F3 0F 10 9B BC010000", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _cameraPos = (int)(offset + index - 0x1A);
        _cameraPosFunc = (int)Memory::ReadStaticInt(offset, index - 0x1A, data);
        _cameraOri = (int)(offset + index + 0x168);
        _cameraOriFunc = (int)Memory::ReadStaticInt(offset, index + 0x168, data);
    });
}

void Trainer::StartHeartbeat(HWND window, UINT message) {
    if (_threadActive) return;
    _threadActive = true;
    _thread = std::thread([sharedThis = shared_from_this(), window, message]{
        SetCurrentThreadName(L"Heartbeat");

        do {
            ProcStatus status = sharedThis->Heartbeat();
            sharedThis->_firstHeartbeat = false;

            PostMessage(window, message, status, NULL);
            std::this_thread::sleep_for(s_heartbeat);
        } while (sharedThis->_threadActive);
        });
    _thread.detach();
}

ProcStatus Trainer::Heartbeat() {
    ProcStatus memoryStatus = _memory->TryAttachToProcess();
    if (memoryStatus == ProcStatus::NotRunning) return ProcStatus::NotRunning;
    if (memoryStatus == ProcStatus::Stopped) {
        _previousCombatStats = 0; // Used to detect if the game just started
        // Wait for the process to fully close; otherwise we might accidentally re-attach to it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return ProcStatus::Stopped;
    }

    // Note that sigscans are idempotent -- each should only "succeed" once.
    // Ergo, it is OK to have sigscans which are not available until after a few game actions are taken.
    size_t failedScans = _memory->ExecuteSigScans();
    if (failedScans > 0) return ProcStatus::NotRunning;

    uintptr_t playerCombatStats = _memory->ResolvePointerPath({_gameWorldPtr, 0x50, 0xA8, 0x140});
    // Game hasn't loaded yet, we're still sitting on the launcher
    if (playerCombatStats == 0) return ProcStatus::NotRunning;

    // At this point, we think the game is running... now we have to figure out what, exactly, to say.

    // If this is the first heartbeat we're sending, we just started (and the game was already running).
    // We set _firstHeartbeat = false in the caller after we return.
    if (_firstHeartbeat) {
        _previousCombatStats = playerCombatStats;
        std::thread([sharedThis = shared_from_this()] {
            sharedThis->OnGameStart();
            }).detach();
        return ProcStatus::AlreadyRunning;
    }

    // If this is the first heartbeat where the Entity_Manager is allocated, the game just started
    if (_previousCombatStats == 0) {
        _previousCombatStats = playerCombatStats;
        std::thread([sharedThis = shared_from_this()] {
            Sleep(0x1000); // Slight delay since (apparently) the loading status is 0 even though the game isn't quite ready.
            sharedThis->OnGameStart();
        }).detach();
        return ProcStatus::Started;
    }

    // If the Entity_Manager just changed, then this was a reload -- clear our memory addresses.
    if (playerCombatStats != _previousCombatStats) {
        _previousCombatStats = playerCombatStats;
        _memory->ClearAllComputedAddresses();
        return ProcStatus::Reload;
    }

    // Otherwise, business as usual.
    return ProcStatus::Running;
}

void Trainer::OnGameStart() {
    SetMoney(999999);
    AddCameraHooks();
}

void Trainer::AddCameraHooks() {
    _cameraBuffer = (int)_memory->AllocateArray(1024);
    constexpr int CAMERA_BUFFER_SIZE = 32;

    std::vector<byte> cameraPosInstructions = {
        IF_EQ(0x83, 0x3D, INT_TO_BYTES(_cameraBuffer), 0x00),   // cmp [_cameraBuffer], 0           ; Check if we are currently overwriting the camera
        THEN(                                                   //                                  ;
            0xB8, INT_TO_BYTES(_cameraPosFunc),                 // mov eax, _cameraPosFunc          ; If not, then jump to the original function
            0xFF, 0xE0                                          // jmp eax                          ;
        ),                                                      //                                  ;
        0x56,                                                   // push esi                         ; (scratch space)
        0x8B, 0x44, 0x24, 0x08,                                 // mov eax, [esp+08]                ; Load the return address (it's an outparam on the stack)
        0x8B, 0x35, INT_TO_BYTES(_cameraBuffer + 4),            // mov esi, [_cameraPosFunc + 4]    ; Copy our camera X position
        0x89, 0x30,                                             // mov [eax], esi                   ; Save it to the return address
        0x8B, 0x35, INT_TO_BYTES(_cameraBuffer + 8),            // mov esi, [_cameraPosFunc + 8]    ; Ditto camera Y
        0x89, 0x70, 0x04,                                       // mov [eax + 4], esi               ;
        0x8B, 0x35, INT_TO_BYTES(_cameraBuffer + 12),           // mov esi, [_cameraPosFunc + 12]   ; Ditto camera Z
        0x89, 0x70, 0x08,                                       // mov [eax + 8], esi               ;
        0x5E,                                                   // pop esi                          ; (free our scratch space)
        0xC2, 0x08, 0x00,                                       // ret 8                            ;
    };

    int cameraOriFunc = _memory->ReadData<int>({_cameraOri}, 1)[0];
    std::vector<byte> cameraOriInstructions = {
        // First function: Setting camera position
        // if (!overwrite) {
        //   goto original function
        // }
        // (else)
    };

    assert(CAMERA_BUFFER_SIZE + cameraPosInstructions.size() + cameraOriInstructions.size() < 1024, "Camera hook size not big enough");

    _memory->WriteData<byte>({_cameraBuffer + CAMERA_BUFFER_SIZE}, cameraPosInstructions);
    int relativeJump = (_cameraBuffer + CAMERA_BUFFER_SIZE) - (_cameraPos + 4);
    _memory->WriteData<int>({_cameraPos}, {relativeJump});

    // _memory->WriteData<byte>({_cameraBuffer + CAMERA_BUFFER_SIZE + (int)cameraPosInstructions.size()}, cameraOriInstructions);
    // _memory->WriteData<int>({_cameraOri}, {_cameraBuffer + CAMERA_BUFFER_SIZE + (int)cameraPosInstructions.size()});
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
    SetNoclip(false);
    SetHealth(3);
    SetMaxHealth(3);
    SetCharge(1);
    SetMaxCharge(1);
    SetGodMode(false);
    SetShowCollision(false);

    StopHeartbeat();
    if (_thread.joinable()) _thread.join();
}

bool Trainer::GetNoclip() {
    return false; // TODO: Read from some sigscann'd address
}

std::vector<float> Trainer::GetPlayerPos() {
    return _memory->ReadData<float>({_gameWorldPtr, 0x50, 0x78, 0x74}, 3);
}

std::vector<float> Trainer::GetPlayerAngle() {
    return _memory->ReadData<float>({_gameWorldPtr, 0x50, 0x78, 0x34}, 4);
}

int Trainer::GetHealth() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x94}, 1)[0] / 25;
}

int Trainer::GetMaxHealth() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x98}, 1)[0] / 25;
}

int Trainer::GetCharge() {
    return (int)(_memory->ReadData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD0}, 1)[0] / 20.0f);
}

int Trainer::GetMaxCharge() {
    return (int)(_memory->ReadData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD4}, 1)[0] / 20.0f);
}

bool Trainer::GetGodMode() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x154}, 1)[0] == 1;
}

bool Trainer::GetShowCollision() { 
    return _memory->ReadData<int>({_globalSettingsPtr, 0x4, 0x3D*4}, 1)[0] == 44;
}

std::string Trainer::GetLevelName() {
    return _memory->ReadString({_gameWorldPtr, 0x4C, 0xD4});
}

std::vector<float> Trainer::GetGrapplePos() {
    int grapplePoint = _memory->ReadData<int>({_gameWorldPtr, 0x50, 0xA8, 0x120, 0x14, 0x4, 0x10, 0x510}, 1)[0];
    if (grapplePoint == 0) return {0, 0, 0};
    int vtable = _memory->ReadData<int>({grapplePoint}, 1)[0];
    if (vtable == 0x03434DEC) return {0, 0, 0};
    return _memory->ReadData<float>({grapplePoint + 0x78, 0x74}, 3);
}

int Trainer::GetMoney() {
    return _memory->ReadData<int>({_gameWorldPtr, 0x50, 0x4CC}, 1)[0];
}

void Trainer::SetNoclip(bool enable) {
    // TODO: Some sigscan here to defy gravity
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0x78, 0x74}, pos);
}

void Trainer::SetPlayerAngle(const std::vector<float>& angle) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0x78, 0x34}, angle);
}

void Trainer::SetHealth(int health) {
    _memory->WriteData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x94}, { health * 25 });
}

void Trainer::SetMaxHealth(int maxHealth) {
    _memory->WriteData<int>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0x98}, { maxHealth * 25 });
}

void Trainer::SetCharge(int charge) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD0}, { charge * 20.0f });
}

void Trainer::SetMaxCharge(int maxCharge) {
    _memory->WriteData<float>({_gameWorldPtr, 0x50, 0xA8, 0x140, 0xD4}, { maxCharge * 20.0f });
}

void Trainer::SetGodMode(bool enable) {
    return _memory->WriteData<int>({_gameWorldPtr, 0x50, 0xA8, 0x154}, { enable ? 1 : 0 });
}

void Trainer::SetShowCollision(bool enable) {
    _memory->WriteData<int>({_globalSettingsPtr, 0x4, 0x3D*4}, { enable ? 44 : 0 });
}

void Trainer::SetMoney(int money) {
    _memory->WriteData<int>({_gameWorldPtr, 0x50, 0x4CC}, { money });
}

void Trainer::SetCameraOverride(bool enable) {
    _memory->WriteData<int>({_cameraBuffer}, { enable ? 1 : 0 });
}

void Trainer::SetCameraPosition(float x, float y, float z) {
    _memory->WriteData<float>({_cameraBuffer + 4}, {x, y, z});
}
