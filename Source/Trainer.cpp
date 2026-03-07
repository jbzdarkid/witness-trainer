#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

Trainer::Trainer(std::shared_ptr<Memory> memory) : _memory(memory) {
    _memory->AddSigScan("84 C0 75 59 BA 20 00 00 00", [this](__int64 offset, int index, const std::vector<byte>& data) {
        // This int is actually desired_movement_direction, which immediately preceeds camera_position
        _cameraPos = Memory::ReadStaticInt(offset, index + 0x19, data) + 0x10;

        // This doesn't have a consistent offset from the scan, so search until we find "mov eax, [addr]"
        for (; index < data.size(); index++) {
            if (data[index - 2] == 0x8B && data[index - 1] == 0x05) {
                _noclipEnabled = Memory::ReadStaticInt(offset, index, data);
                return true;
            }
        }
        return false;
    });

    _memory->AddSigScan("C7 45 77 00 00 80 3F C7 45 7F 00 00 80 3F", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _cameraAng = Memory::ReadStaticInt(offset, index + 0x17, data);
    });

    _memory->AddSigScan("0F 29 7C 24 70 44 0F 29 54 24 60", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _noclipSpeed = Memory::ReadStaticInt(offset, index + 0x4F, data);
    });

    _memory->AddSigScan("74 41 48 85 C0 74 04 48 8B 48 10", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _globals = Memory::ReadStaticInt(offset, index + 0x14, data);
    });

    _memory->AddSigScan("01 00 00 66 C7 87", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _loadCountOffset = *(int*)&data[index-1];
    });

    _memory->AddSigScan("84 C0 74 19 0F 2F B7", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _doorOpen = offset + index + 0x0B;
        _solvedTargetOffset = *(int*)&data[index + 0x07];
    });

    _memory->AddSigScan("84 C0 74 11 0F 2F BF", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _doorClose = offset + index + 0x0B;
    });

    // This sigscan is rather awkward, it's not pointing to the start of a line, because I modify that line.
    _memory->AddSigScan("18 48 8B CF 89 9F", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _powerOn = offset + index - 0x05;
    });

    _memory->AddSigScan("48 89 58 08 48 89 70 10 48 89 78 18 48 8B 3D", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _campaignState = Memory::ReadStaticInt(offset, index + 0x27, data);
    });

    _memory->AddSigScan2("F3 0F 59 FD F3 0F 5C C8", [this](__int64 offset, int index, const std::vector<byte>& data) {
        int found = 0;
        // This doesn't have a consistent offset from the scan, so search until we find "jmp +08"
        for (; index < data.size(); index++) {
            if (data[index - 2] == 0xEB && data[index - 1] == 0x08) {
                _walkAcceleration = Memory::ReadStaticInt(offset, index - 0x06, data);
                _walkDeceleration = Memory::ReadStaticInt(offset, index + 0x04, data);
                found++;
                break;
            }
        }

        // Once again, there's no consistent offset, so we read until "movss xmm1, [addr]"
        for (; index < data.size(); index++) {
            if (data[index - 4] == 0xF3 && data[index - 3] == 0x0F && data[index - 2] == 0x10 && data[index - 1] == 0x0D) {
                _runSpeed = Memory::ReadStaticInt(offset, index, data);
                found++;
                break;
            }
        }
        return (found == 2);
    });

    _memory->AddSigScan("00 00 00 05 00 00 00 E9 B3", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _recordPlayerUpdate = offset + index - 0x0C;
    });

    _memory->AddSigScan("F2 0F 58 C8 66 0F 5A C1 F2", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _activePanelOffsets.push_back(Memory::ReadStaticInt(offset, index + 0x36, data, 5));
        _activePanelOffsets.push_back(data[index + 0x5A]); // This is 0x10 in both versions I have, but who knows.
        _activePanelOffsets.push_back(*(int*)&data[index + 0x54]);
    });

    // This scan intentionally fails if the injection has been applied. It makes this a bit unstable for development, but adds safety in case another trainer is attached.
    _memory->AddSigScan2("41 B8 61 00 00 00 48 8B D3", [this](__int64 offset, int index, const std::vector<byte>& data) {
        for (; index > 0; index--) {
            if (data[index] == 0x44 && data[index + 8] == 0x74 && data[index + 9] == 0x10) {
                _mainMenuColor = offset + index;
                return true;
            }
        }
#if _DEBUG
        return true;
#else
        return false;
#endif
    });

    _memory->AddSigScan("0F 57 C0 0F 2F 80 B4 00 00 00 0F 92 C0 C3", [this](__int64 offset, int index, const std::vector<byte>& data) {
        auto console = Memory::ReadStaticInt(offset, index-4, data);
        _consoleWindowYB = {console, 0x4C};
        _consoleOpenTarget = {console, 0xB4};
    });

    _memory->AddSigScan("83 F8 03 7C 41 84 C9 74 1F", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _wantCampaignSave = Memory::ReadStaticInt(offset, index + 0x2A, data, 5);
    });

    _memory->AddSigScan("74 14 48 8B 95", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _epNameOffset = *(int*)&data[index + 0x05];
    });

    _memory->AddSigScan("74 0B 0F 28 D0", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _menuOpenTarget = Memory::ReadStaticInt(offset, index + 0x19, data);
    });

    // This scan will actually succeed on older patches, but _fovCurrent will still be 0.
    _memory->AddSigScan("48 85 C0 74 0A C7 80 28 03", [this](__int64 offset, int index, const std::vector<byte>& data) {
        for (; index > 0; index--) {
            if (data[index+4] == 0x33 && data[index+5] == 0xC9) {
                _fovCurrent = Memory::ReadStaticInt(offset, index, data);
                return;
            }
        }
    });

    _memory->AddSigScan("0F 84 38 06 00 00 48 89 58 F0", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _showPatternStatus = Memory::ReadStaticInt(offset, index - 5, data, 5);
    });

    _memory->AddSigScan("41 3B FC 41 0F 4F FC", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _drawPatternManager = offset + index + 0x16;
    });

    _memory->AddSigScan("83 F8 01 75 0E 33 C0", [this](__int64 offset, int index, const std::vector<byte>& data) {
        _debugMode = Memory::ReadStaticInt(offset, index - 4, data);
    });

    _memory->AddSigScan("F2 0F 10 41 24 48 89 68 10", [this](int64_t offset, int index, const std::vector<uint8_t>& data) {
        _doorOpenStart = offset + index - 15;
    });

#if _DEBUG
    _memory->AddSigScan("8B DD 8B F5 66 0F 1F 84 00 00 00 00 00", [this](int64_t offset, int index, const std::vector<uint8_t>& data) {
        __int64 allBinkInfos = Memory::ReadStaticInt(offset, index + 0x0F, data);
        _binkInfoData = allBinkInfos + 0x8;
    });

    _memory->AddSigScan("48 8B FA 48 8B D9 85 C0 74 08", [this](int64_t offset, int index, const std::vector<uint8_t>& data) {
        _unusedIdsOffset = *(int*)&data[index + 0x21];
    });
#endif
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

void Trainer::StopHeartbeat() {
    _threadActive = false;
}

ProcStatus Trainer::Heartbeat() {
    ProcStatus memoryStatus = _memory->TryAttachToProcess();
    if (memoryStatus == ProcStatus::NotRunning) return ProcStatus::NotRunning;
    if (memoryStatus == ProcStatus::Stopped) {
        // Wait for the process to fully close; otherwise we might accidentally re-attach to it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return ProcStatus::Stopped;
    }

    // Note that sigscans are idempotent -- each should only "succeed" once.
    // Ergo, it is OK to have sigscans which are not available until after a few game actions are taken.
    size_t failedScans = _memory->ExecuteSigScans();
    if (failedScans > 0) return ProcStatus::NotRunning;

    __int64 entityManager = _memory->ReadData<__int64>({_globals}, 1)[0];
    // Game hasn't loaded yet, we're still sitting on the launcher
    if (entityManager == 0) return ProcStatus::NotRunning;

    // If there are less than 400k total entities, a new game is still starting.
    int numEntities = _memory->ReadData<int>({_globals, 0x10}, 1)[0];
    if (numEntities < 400'000) return ProcStatus::Loading;

    // Saved game is currently loading, do not take any actions.
    byte isLoading = _memory->ReadData<byte>({entityManager, _loadCountOffset - 0x4}, 1)[0];
    if (isLoading == 0x01) return ProcStatus::Loading;

    // At this point, we think the game is running... now we have to figure out what, exactly, to say.

    // If this is the first heartbeat we're sending, we just started (and the game was already running).
    // We set _firstHeartbeat = false in the caller after we return.
    if (_firstHeartbeat) {
        SetMainMenuColor(true); // Recolor the menu
        SetEPOverlayMinSize(true); // Prevent the solvability overlay from getting sub-pixel
        return ProcStatus::AlreadyRunning;
    }

    // If this is the first heartbeat where the Entity_Manager is allocated, the game just started
    if (_previousEntityManager == 0) {
        _previousEntityManager = entityManager;
        SetMainMenuColor(true); // Recolor the menu
        SetEPOverlayMinSize(true); // Prevent the solvability overlay from getting sub-pixel
        return ProcStatus::Started;
    }

    // If the Entity_Manager just changed, then this was a reload -- clear our memory addresses.
    if (entityManager != _previousEntityManager) {
        _previousEntityManager = entityManager;
        _memory->ClearAllComputedAddresses();
        return ProcStatus::Reload;
    }

    // Otherwise, business as usual.
    return ProcStatus::Running;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
    SetNoclip(false);
    SetNoclipSpeed(4.0f);
    float fov = GetFov();
    SetFov(CLAMP(fov, 80.0f, 120.0f));
    SetCanSave(true);
    SetSprintSpeed(2.0f);
    SetInfiniteChallenge(false);
    SetConsoleOpen(false);
    SetMainMenuColor(false);
    SetRandomDoorsPractice(false);
    SetChallengePillarsPractice(false);
    SetEPOverlay(false);
    SetEPOverlayMinSize(false);
    ClampAimingPhi(true);

    StopHeartbeat();
    if (_thread.joinable()) _thread.join();
}

int Trainer::GetActivePanel() {
    return _memory->ReadData<int>(_activePanelOffsets, 1)[0] - 1;
}

std::shared_ptr<Trainer::EntityData> Trainer::GetEntityData(int id) {
    if (id == -1) return nullptr;
    int64_t entity = _memory->ReadData<int64_t>({_globals, 0x18, id * 8}, 1)[0];
    if (entity == 0) return nullptr; // Entity is not defined, manager is likely being re-allocated due to load.
    int readId = _memory->ReadData<int>({_globals, 0x18, id * 8, 0x10}, 1)[0];
    if (id != (readId - 1)) return nullptr; // Entity is no longer a valid object (or is not the entity we expected to read)

    std::string typeName = _memory->ReadString({_globals, 0x18, id * 8, 0x08, 0x08});
    auto entityData = std::make_shared<EntityData>();
    entityData->id = id;
    entityData->type = typeName;
    entityData->entity = entity;
    entityData->position = _memory->ReadData<float>({_globals, 0x18, id * 8, 0x24}, 3);

    // Look up extra data for some types
    if (typeName == "Machine_Panel")      GetPanelData(entityData);
    else if (typeName == "Pattern_Point") GetEPData(entityData);
    else if (typeName == "Door")          GetDoorData(entityData);
    return entityData;
}

struct Traced_Edge final {
    int32_t index_a;
    int32_t index_b;
    int32_t id_a;
    int32_t id_b;
    float t;
    float t_highest;
    float position_a[3];
    float position_b[3];
    bool water_reflect_a;
    bool water_reflect_b;
    bool hide_edge;
    bool padding;
};

void Trainer::GetPanelData(const std::shared_ptr<Trainer::EntityData>& data) {
    int nameOffset = _solvedTargetOffset - 0x7C;
    int tracedEdgesOffset = _solvedTargetOffset - 0x6C;
    int stateOffset = _solvedTargetOffset - 0x14;
    int hasEverBeenSolvedOffset = _solvedTargetOffset + 0x04;

    data->name = _memory->ReadString({_globals, 0x18, data->id * 8, nameOffset});
    int state = _memory->ReadData<int>({_globals, 0x18, data->id * 8, stateOffset}, 1)[0];
    int hasEverBeenSolved = _memory->ReadData<int>({_globals, 0x18, data->id * 8, hasEverBeenSolvedOffset}, 1)[0];
    if (hasEverBeenSolved) {
        data->solved = true;
    } else {
        float solvedTarget = _memory->ReadData<float>({_globals, 0x18, data->id * 8, _solvedTargetOffset}, 1)[0];
        data->solved = (solvedTarget == 1.0f);
    }
    if (state == 0 && hasEverBeenSolved == 0) data->state = "Has never been solved";
    else if (state == 0 && hasEverBeenSolved == 1) data->state = "Was previously solved";
    else if (state == 1) data->state = "Solved";
    else if (state == 2) data->state = "Failed";
    else if (state == 3) data->state = "Exited";
    else if (state == 4) data->state = "Negation pending";
    else data->state = "Unknown";

    auto numEdges = _memory->ReadData<int>({_globals, 0x18, data->id * 8, tracedEdgesOffset}, 1)[0];
    if (numEdges > 0) {
        // Explicitly computing this as an intermediate, since the edges array might have re-allocated.
        auto edgeDataPtr = _memory->ReadData<__int64>({_globals, 0x18, data->id * 8, tracedEdgesOffset + 8}, 1)[0];
        if (edgeDataPtr != 0) {
            static_assert(sizeof(Traced_Edge) == 0x34);
            // However, we only need the first edge -- since all we care about is the startpoint.
            std::vector<Traced_Edge> edgeData = _memory->ReadData<Traced_Edge>({edgeDataPtr}, 1);
            data->startPoint = {
                edgeData[0].position_a[0],
                edgeData[0].position_a[1],
                edgeData[0].position_a[2],
            };
        }
    }
}

void Trainer::GetEPData(const std::shared_ptr<Trainer::EntityData>& data) {
    data->name = _memory->ReadString({_globals, 0x18, data->id * 8, _epNameOffset});
    data->startPoint = _memory->ReadData<float>({_globals, 0x18, data->id * 8, 0x24}, 3);
}

void Trainer::GetDoorData(const std::shared_ptr<Trainer::EntityData>& data) {
    if (_globals != 0x5B28C0) return; // I'm lazy and don't care to find real sigscans
    // data->name = _memory->ReadString({_globals, 0x18, data->id * 8, 0x58}); // entity_name
    data->name = _memory->ReadString({_globals, 0x18, data->id * 8, 0x168}); // start_opening_sound
}

Trainer::VideoData Trainer::GetVideoData() {
    if (_binkInfoData == 0) return {};

    VideoData videoData;
    videoData.fileName = _memory->ReadString({_binkInfoData, 0x0, 0x18});
    if (!videoData.fileName.empty()) {
        videoData.totalFrames = _memory->ReadData<int>({_binkInfoData, 0x0, 0x0, 0x8}, 1)[0];
        videoData.currentFrame = _memory->ReadData<int>({_binkInfoData, 0x0, 0x0, 0xC}, 1)[0];
    }

    videoData.numUnusedIds = _memory->ReadData<int>({_globals, _unusedIdsOffset + 0x8}, 1)[0];
    videoData.nextUnusedIdIdx = _memory->ReadData<int>({_globals, _unusedIdsOffset + 0xC}, 1)[0];
    std::vector<int> unusedIds = _memory->ReadData<int>({_globals, _unusedIdsOffset, 0x0}, videoData.numUnusedIds);

    videoData.videoDrySoundId = _memory->ReadData<int>({_binkInfoData, 0x0, 0x48}, 1)[0];

    videoData.videoDrySoundIdIdx = -1;
    for (int i = 0; i < unusedIds.size(); i++) {
        if (unusedIds[i] == videoData.videoDrySoundId) {
            videoData.videoDrySoundIdIdx = i;
            break;
        }
    }

    return videoData;
}

void Trainer::ShowMissingPanels() {
    std::vector<std::string> missingPanels;
    for (const auto& [id, panelName] : PANELS) {
        std::shared_ptr<EntityData> data = GetEntityData(id);
        // Treat non-existent panels as unsolved until we know better.
        if (!data || !data->solved) missingPanels.push_back(panelName);
    }
    if (missingPanels.empty()) {
        MessageBoxA(NULL, "You solved all the panels!", "", MB_OK);
        return;
    }

    std::string message;
    for (const auto& missingPanel : missingPanels) {
        if (message.size() == 0) {
            message += missingPanel;
        } else {
            message += ", " + missingPanel;
        }
        if (message.size() > 1000) {
            message += ", ...";
            break;
        }
    }
    std::string title = std::to_string(missingPanels.size()) + " unsolved, counted panels";
    MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK);
}

std::vector<std::shared_ptr<Trainer::EntityData>> Trainer::GetNearbyEntities(float distance) {
    float maxNorm = distance * distance;
    auto basePos = GetCameraPos();
    std::vector<std::shared_ptr<EntityData>> nearbyEntities = {};

    int32_t maxId = _memory->ReadData<int>({_globals, 0x14}, 1)[0];
    for (int32_t id = 0; id < maxId; id++) {
        if (id == 0x1E465) continue; // Skip over ourselves (Entity_Human)
        std::shared_ptr<EntityData> entityData = GetEntityData(id);
        if (entityData == nullptr) continue;
        const std::vector<float>& pos = entityData->position;

        double norm = std::pow(basePos[0] - pos[0], 2) + std::pow(basePos[1] - pos[1], 2) + std::pow(basePos[2] - pos[2], 2);
        if (norm <= maxNorm) nearbyEntities.push_back(entityData);
    }

    return nearbyEntities;
}

void Trainer::ShowNearbyEntities() {
    auto basePos = GetCameraPos();
    auto nearbyEntities = GetNearbyEntities();

    DebugPrint("Entity ID\tDistance\t     X\t     Y\t     Z\tType");
    for (const auto& entityData : nearbyEntities) {
        std::vector<float> pos = entityData->position;
        std::string typeName = entityData->type;

        std::stringstream message;
        message << "0x" << std::hex << std::setfill('0') << std::setw(5) << entityData->id << '\t';
        double distance = std::sqrt(std::pow(basePos[0] - pos[0], 2) + std::pow(basePos[1] - pos[1], 2) + std::pow(basePos[2] - pos[2], 2));
        message << distance << '\t';
        message << pos[0] << '\t' << pos[1] << '\t' << pos[2] << '\t' << typeName;
        DebugPrint(message.str());
    }
}

void Trainer::ExportEntities() {
    int32_t maxId = _memory->ReadData<int>({_globals, 0x14}, 1)[0];

    DebugPrint("Entity ID\tType\tName\t     X\t     Y\t     Z");
    for (int32_t id = 1; id < maxId; id++) {
        int32_t entity = _memory->ReadData<int>({_globals, 0x18, id * 8}, 1)[0];
        if (entity == 0) continue;
        std::string typeName = _memory->ReadString({_globals, 0x18, id * 8, 0x08, 0x08});
        std::string entityName = _memory->ReadString({_globals, 0x18, id * 8, 0x58});
        std::vector<float> pos = _memory->ReadData<float>({_globals, 0x18, id * 8, 0x24}, 3);

        if (typeName != "Power_Cable") continue;

        std::vector<int> ids = _memory->ReadData<int>({_globals, 0x18, id * 8, 0xD4}, 4);
        std::string textureName = _memory->ReadString({_globals, 0x18, id * 8, 0x140});
        std::string materialName = _memory->ReadString({_globals, 0x18, id * 8, 0x148});
        std::vector<float> colorData = _memory->ReadData<float>({_globals, 0x18, id * 8, 0x150}, 8);
        std::string meshName = _memory->ReadString({_globals, 0x18, id * 8, 0x188});
        std::string poweredOnTexture = _memory->ReadString({_globals, 0x18, id * 8, 0x190});
        std::string powered_on_sound_name = _memory->ReadString({_globals, 0x18, id * 8, 0x198});
        std::string powered_off_sound_name = _memory->ReadString({_globals, 0x18, id * 8, 0x1A0});
        std::string ambient_sound_name = _memory->ReadString({_globals, 0x18, id * 8, 0x1A8});
        std::string powered_on_texture_name = _memory->ReadString({_globals, 0x18, id * 8, 0x1B0});

        std::stringstream message;
        message << "0x" << std::hex << std::setfill('0') << std::setw(5) << id << '\t';
        message << typeName << '\t';
        message << entityName << '\t';
        message << pos[0] << '\t' << pos[1] << '\t' << pos[2] << '\t';
        for (int i : ids) message << "0x" << std::hex << std::setfill('0') << std::setw(5) << i << '\t';
        message << textureName << '\t';
        message << materialName << '\t';
        for (float c : colorData) message << c << '\t';
        message << meshName << '\t';
        message << poweredOnTexture << '\t';
        message << powered_on_sound_name << '\t';
        message << powered_off_sound_name << '\t';
        message << ambient_sound_name << '\t';
        message << powered_on_texture_name << '\t';
        DebugPrint(message.str());
    }
    DebugPrint("------ Done ------");
}

void Trainer::SnapToPoint(const std::vector<float>& point) {
    auto cameraPos = GetCameraPos();

    float Ax = cameraPos[0];
    float Ay = cameraPos[1];
    float Az = cameraPos[2];
    float Bx = point[0];
    float By = point[1];
    float Bz = point[2];

    float hypotenuse2 = sqrt((Ax - Bx) * (Ax - Bx) + (Ay - By) * (Ay - By));
    float hypotenuse3 = sqrt((Ax - Bx) * (Ax - Bx) + (Ay - By) * (Ay - By) + (Az - Bz) * (Az - Bz));

    std::vector<float> cameraAng = {
        asin((By - Ay) / hypotenuse2),
        acos(hypotenuse2 / hypotenuse3),
    };
    if (Ax > Bx) cameraAng[0] = 3.141592654f - cameraAng[0];
    if (Az > Bz) cameraAng[1] = -cameraAng[1];

    SetCameraAng(cameraAng);
}

void Trainer::DisableDistanceGating() {
    int32_t maxId = _memory->ReadData<int>({_globals, 0x14}, 1)[0];

    for (int32_t id = 1; id < maxId; id++) {
        std::shared_ptr<EntityData> entityData = GetEntityData(id);
        if (!entityData || entityData->type != "Machine_Panel") continue;

        // TODO: Audit this, it's probably wrong now that we're not relative ptr.
        assert(_globals == 0x62D0A0, "DisableDistanceGating is only supported on the latest version");
        float distanceGated = _memory->ReadData<float>({entityData->entity, 0x3BC}, 1)[0];
        if (distanceGated != 0.0f) _memory->WriteData<float>({_globals, 0x18, id * 8, 0x3BC}, {0.0f});
    }
}

void Trainer::OpenNearbyDoors() {
    auto nearbyEntities = GetNearbyEntities();
    for (const auto& entityData : nearbyEntities) {
        if (entityData && entityData->type == "Door") {
            _memory->CallFunction(_doorOpenStart, entityData->entity, 1.0);
        }
    }
}

bool Trainer::GetNoclip() {
    return (bool) _memory->ReadData<int>({_noclipEnabled}, 1)[0];
}

float Trainer::GetNoclipSpeed() {
    return _memory->ReadData<float>({_noclipSpeed}, 1)[0];
}

std::vector<float> Trainer::GetPlayerPos() {
    return _memory->ReadData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, 3);
}

std::vector<float> Trainer::GetCameraPos() {
    return _memory->ReadData<float>({_cameraPos}, 3);
}

std::vector<float> Trainer::GetCameraAng() {
    return _memory->ReadData<float>({_cameraAng}, 2);
}

float Trainer::GetFov() {
    if (_fovCurrent == 0) return 0.0f; // FOV is not available on some old patches

    // This is the inverse of fov_vertical_from_horizontal.
    double fov = _memory->ReadData<float>({_fovCurrent}, 1)[0];
    fov *= 0.00872664625997165;
    fov = tan(fov);
    fov *= 1.7777777777777778;
    fov = atan(fov);
    fov *= 114.5915590261646;
    return (float)fov;
}

bool Trainer::CanSave() {
    return _memory->ReadData<byte>({_campaignState, 0x50}, 1)[0] == 0x00;
}

float Trainer::GetSprintSpeed() {
    return _memory->ReadData<float>({_runSpeed}, 1)[0];
}

bool Trainer::GetInfiniteChallenge() {
    return _memory->ReadData<byte>({_recordPlayerUpdate}, 1)[0] == 0x0F;
}

bool Trainer::GetConsoleOpen() {
    return _memory->ReadData<float>(_consoleOpenTarget, 1)[0] == 1.0f;
}

bool Trainer::GetRandomDoorsPractice() {
    return _memory->ReadData<byte>({_doorOpen}, 1)[0] == 0x90;
}

bool Trainer::GetEPOverlay() {
    return _memory->ReadData<byte>({_showPatternStatus}, 1)[0] != 0x00;
}

bool Trainer::GetEPOverlayMinSize() {
    return _memory->ReadData<byte>({_drawPatternManager}, 1)[0] == 0x44;
}

bool Trainer::IsAimingPhiClamped() {
    return _memory->ReadData<byte>({_debugMode}, 1)[0] != 0x09;
}

void Trainer::SetNoclip(bool enable) {
    _memory->WriteData<byte>({_noclipEnabled}, {static_cast<byte>(enable)});
}

void Trainer::SetNoclipSpeed(float speed) {
    if (speed <= 0.0f) return;
    _memory->WriteData<float>({_noclipSpeed}, {speed});
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, pos);
}

void Trainer::SetCameraPos(const std::vector<float>& pos) {
    _memory->WriteData<float>({_cameraPos}, pos);
}

void Trainer::SetCameraAng(const std::vector<float>& ang) {
    _memory->WriteData<float>({_cameraAng}, ang);
}

void Trainer::SetFov(double fov) {
    if (_fovCurrent == 0) return;
    // This computation is called fov_vertical_from_horizontal.
    fov *= 0.00872664625997165;
    fov = tan(fov);
    fov *= 0.5625f;
    fov = atan(fov);
    fov *= 114.5915590261646;

    _memory->WriteData<float>({_fovCurrent}, {(float)fov});
}

void Trainer::SetCanSave(bool canSave) {
    _memory->WriteData<byte>({_campaignState, 0x50}, {canSave ? (byte)0x00 : (byte)0x01});
}

void Trainer::SetSprintSpeed(float speed) {
    if (speed == 0) return;
    float sprintSpeed = GetSprintSpeed();
    if (sprintSpeed == 0.0f) return; // sanity check, to avoid an accidental div0
    float multiplier = speed / sprintSpeed;
    if (multiplier == 1.0f) return;
    _memory->WriteData<float>({_runSpeed}, {speed});
    _memory->WriteData<float>({_walkAcceleration}, {_memory->ReadData<float>({_walkAcceleration}, 1)[0] * multiplier});
    _memory->WriteData<float>({_walkDeceleration}, {_memory->ReadData<float>({_walkDeceleration}, 1)[0] * multiplier});
}

void Trainer::SetConsoleOpen(bool enable) {
    if (enable) {
        _memory->WriteData<float>(_consoleWindowYB, {0.0f});
        _memory->WriteData<float>(_consoleOpenTarget, {1.0f});
    } else {
        _memory->WriteData<float>(_consoleOpenTarget, {0.0f});
    }
}

bool Trainer::SaveCampaign() {
    _memory->WriteData<byte>({_wantCampaignSave}, {0x01});
    for (int i=0; i<100; i++) {
        ::Sleep(10); // Wait a bit for the game to run
        byte wantCampaignSave = _memory->ReadData<byte>({_wantCampaignSave}, 1)[0];
        if (wantCampaignSave == 0x00) return true;
    }

    return false; // Didn't save successfully
}

void Trainer::SetInfiniteChallenge(bool enable) {
    if (enable) {
        // Jump over abort_speed_run, with NOP padding
        _memory->WriteData<byte>({_recordPlayerUpdate}, {0xEB, 0x07, 0x66, 0x90});
    } else {
        // (original code) Load entity_manager into rcx
        _memory->WriteData<byte>({_recordPlayerUpdate}, {0x48, 0x8B, 0x4B, 0x18});
    }
}

void Trainer::SetMainMenuColor(bool enable) {
    if (enable) { // Set the main menu to red by *not* setting the green or blue component.
        _memory->WriteData<byte>({_mainMenuColor}, {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}); // 8-byte NOP
    } else { // Restore the original setting by copy/pasting from the block below.
        std::vector<byte> code = _memory->ReadData<byte>({_mainMenuColor + 0x12}, 8);
        _memory->WriteData<byte>({_mainMenuColor}, code);
    }
}

void Trainer::SetMainMenuState(bool open) {
    _memory->WriteData<float>({_menuOpenTarget}, {open ? 1.0f : 0.0f});
}

void Trainer::SetRandomDoorsPractice(bool enable) {
    if (_solvedTargetOffset == 0) return;

    int idToPowerOffset = _solvedTargetOffset + 0x20;
    int onTargetOffset = _solvedTargetOffset + 0x10;

    // This block is up here because it needs to be rerun when we reload the game.
    if (enable) {
        // When the panel is solved, power nothing.
        _memory->WriteData<int>({_globals, 0x18, 0x1983 * 8, idToPowerOffset}, {0x00000000});
        _memory->WriteData<int>({_globals, 0x18, 0x1987 * 8, idToPowerOffset}, {0x00000000});

        // To make sure that the panels are randomized the first time they power on, we mark them as having already been solved.
        _memory->WriteData<float>({_globals, 0x18, 0x1983 * 8, _solvedTargetOffset}, {1.0f});
        _memory->WriteData<float>({_globals, 0x18, 0x1987 * 8, _solvedTargetOffset}, {1.0f});
    } else {
        // When the panel is solved, power the double doors.
        _memory->WriteData<int>({_globals, 0x18, 0x1983 * 8, idToPowerOffset}, {0x00017C68});
        _memory->WriteData<int>({_globals, 0x18, 0x1987 * 8, idToPowerOffset}, {0x00017C68});
    }

    // If the injection state matches the enable request, no futher action is needed.
    if (enable == GetRandomDoorsPractice()) return;

    if (enable) {
        // When the panel closes, always turn it off
        _memory->WriteData<byte>({_doorClose}, {0x90, 0x90});
        // When the panel opens, always reset it, and power it on.
        _memory->WriteData<byte>({_doorOpen}, {0x90, 0x90});
        // When the panel powers on, only randomize if it's solved
        _memory->WriteData<int>({_powerOn}, {_solvedTargetOffset});
        _memory->WriteData<byte>({_powerOn + 0x04}, {0x77, 0x18});

        // When the panel powers on, mark it as not solved
        _memory->WriteData<byte>({_powerOn + 0x3B}, {
            0x48, 0x63, 0x00, // movsxd rax, dword ptr [rax]
            0x66, 0xC7, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov word ptr ds:[rdi + <offset>], 0x00
        });
        // Note: We're writing to this register (which is a float!) as a word, since we don't have enough instruction space.
        // This float should only ever be set to 0x3F800000 (1.0f), though -- so we can just write to the high bits.
        _memory->WriteData<int>({_powerOn + 0x41}, {_solvedTargetOffset + 2});
    } else {
        // When the panel closes, if the puzzle has been solved, turn it off
        _memory->WriteData<byte>({_doorClose}, {0x76, 0x08});
        // When the panel opens, if the puzzle has not been solved, reset it and power it on
        _memory->WriteData<byte>({_doorOpen}, {0x76, 0x10});
        // When the panel powers on, only randomize if it's off
        _memory->WriteData<int>({_powerOn}, {onTargetOffset});
        _memory->WriteData<byte>({_powerOn + 0x04}, {0x76, 0x18});
    }
}

void Trainer::SetChallengePillarsPractice(bool enable) {
    if (_solvedTargetOffset == 0) return;
    int idToPowerOffset = _solvedTargetOffset + 0x20;

    if (enable) {
        // When enabled, you can solve the challenge vault box to turn on the pillars
        _memory->WriteData<int>({_globals, 0x18, 0x356B * 8, idToPowerOffset}, {0x3618});
    } else {
        // When disabled, the box powers its original target
        _memory->WriteData<int>({_globals, 0x18, 0x356B * 8, idToPowerOffset}, {0x3615});
    }
}

void Trainer::SetEPOverlay(bool enable) {
  _memory->WriteData<byte>({_showPatternStatus}, {static_cast<byte>(enable)});
}

void Trainer::SetEPOverlayMinSize(bool enable) {
    if (_drawPatternManager == 0) return;

    // If the injection state matches the enable request, no futher action is needed.
    if (enable == GetEPOverlayMinSize()) return;

    if (enable) {
        // If the size is less than 1.0 (xmm0), set it to 1.0.
        _memory->WriteData<byte>({_drawPatternManager}, {
            0x44, 0x0F, 0x2F, 0xC0,         // comiss xmm8, xmm0
            0x73, 0x06,                     // jae +0x6
            0xF3, 0x44, 0x0F, 0x10, 0xC0,   // movss xmm8, xmm0
            0x90,                           // nop
        }); // <-- jump target
    } else {
        // This is overridding 4 instructions that are useless sanity checks.
        _memory->WriteData<byte>({_drawPatternManager}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
    };
}

void Trainer::ClampAimingPhi(bool clamp) {
    _memory->WriteData<byte>({_debugMode}, {clamp ? (byte)0x00 : (byte)0x09});
}