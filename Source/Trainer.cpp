#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::unique_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::unique_ptr<Trainer>(new Trainer());

    memory->AddSigScan({0x84, 0xC0, 0x75, 0x59, 0xBA, 0x20, 0x00, 0x00, 0x00}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        // This int is actually desired_movement_direction, which immediately preceeds camera_position
        trainer->_cameraPos = Memory::ReadStaticInt(offset, index + 0x19, data) + 0x10;

        // This doesn't have a consistent offset from the scan, so search until we find "mov eax, [addr]"
        for (; index < data.size(); index++) {
            if (data[index - 2] == 0x8B && data[index - 1] == 0x05) {
                trainer->_noclipEnabled = Memory::ReadStaticInt(offset, index, data);
                break;
            }
        }
    });

    memory->AddSigScan({0xC7, 0x45, 0x77, 0x00, 0x00, 0x80, 0x3F, 0xC7, 0x45, 0x7F, 0x00, 0x00, 0x80, 0x3F}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_cameraAng = Memory::ReadStaticInt(offset, index + 0x17, data);
    });

    memory->AddSigScan({0x0F, 0x29, 0x7C, 0x24, 0x70, 0x44, 0x0F, 0x29, 0x54, 0x24, 0x60}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_noclipSpeed = Memory::ReadStaticInt(offset, index + 0x4F, data);
    });

    memory->AddSigScan({0x76, 0x09, 0xF3, 0x0F, 0x11, 0x05}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_fovCurrent = Memory::ReadStaticInt(offset, index + 0x0F, data);
    });

    memory->AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_globals = Memory::ReadStaticInt(offset, index + 0x14, data);
    });

    memory->AddSigScan({0x84, 0xC0, 0x74, 0x19, 0x0F, 0x2F, 0xB7}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_doorOpen = offset + index + 0x0B;
        trainer->_solvedTargetOffset = *(int*)&data[index + 0x07];
    });

    // This one is if you haven't run doors injection
    memory->AddSigScan({0x84, 0xC0, 0x74, 0x11, 0x0F, 0x2F, 0xBF}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_doorClose = offset + index + 0x04;
    });

    // And this one is if you have run doors injection
    memory->AddSigScan({0x84, 0xC0, 0x74, 0x11, 0x3A, 0x87}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_doorClose = offset + index + 0x04;
    });

    // Improve this? Something awkward is happening if you solve while the doors are closing.
    memory->AddSigScan({0x76, 0x18, 0x48, 0x8B, 0xCF, 0x89, 0x9F}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_powerOn = offset + index + 0x37;
    });

    memory->AddSigScan({0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x48, 0x8B, 0x3D}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_campaignState = Memory::ReadStaticInt(offset, index + 0x27, data);
    });

    memory->AddSigScan({0xF3, 0x0F, 0x59, 0xFD, 0xF3, 0x0F, 0x5C, 0xC8}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        // This doesn't have a consistent offset from the scan, so search until we find "jmp +08"
        for (; index < data.size(); index++) {
            if (data[index - 2] == 0xEB && data[index - 1] == 0x08) {
                trainer->_walkAcceleration = Memory::ReadStaticInt(offset, index - 0x06, data);
                trainer->_walkDeceleration = Memory::ReadStaticInt(offset, index + 0x04, data);
                break;
            }
        }
        // Once again, there's no consistent offset, so we read until "movss xmm1, [addr]"
        for (; index < data.size(); index++) {
            if (data[index - 4] == 0xF3 && data[index - 3] == 0x0F && data[index - 2] == 0x10 && data[index - 1] == 0x0D) {
                trainer->_runSpeed = Memory::ReadStaticInt(offset, index, data);
                break;
            }
        }
    });

    memory->AddSigScan({0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xE9, 0xB3}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_recordPlayerUpdate = offset + index - 0x0C;
    });

    memory->AddSigScan({0xF2, 0x0F, 0x58, 0xC8, 0x66, 0x0F, 0x5A, 0xC1, 0xF2}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        trainer->_activePanelOffsets.push_back(Memory::ReadStaticInt(offset, index + 0x36, data) + 1); // +1 because the line ends with an extra byte
        trainer->_activePanelOffsets.push_back(data[index + 0x5A]); // This is 0x10 in both versions I have, but who knows.
        trainer->_activePanelOffsets.push_back(*(int*) &data[index + 0x54]);
    });

    memory->AddSigScan({0x41, 0xB8, 0x61, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD3}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        for (; index > 0; index--) {
            if (data[index + 8] == 0x74 && data[index + 9] == 0x10) {
                trainer->_mainMenuColor = offset + index;
                break;
            }
        }
    });

    memory->AddSigScan({0x0F, 0x57, 0xC0, 0x0F, 0x2F, 0x80, 0xB4, 0x00, 0x00, 0x00, 0x0F, 0x92, 0xC0, 0xC3}, [&](__int64 offset, int index, const std::vector<byte>& data) {
        auto console = Memory::ReadStaticInt(offset, index-4, data);
        trainer->_consoleWindowYB = {console, 0x4C};
        trainer->_consoleOpenTarget = {console, 0xB4};
    });

    // We need to save _memory before we exit, otherwise we can't destroy properly.
    trainer->_memory = memory;

    size_t numFailedScans = memory->ExecuteSigScans();
    numFailedScans -= 1; // One of the two _doorClose scans will always fail.
    if (trainer->_globals && trainer->_globals == 0x5B28C0) numFailedScans -= 1; // FOV scan is expected to fail on older versions.
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    trainer->SetMainMenuColor(true);
    return trainer;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
    SetNoclip(false);
    SetRandomDoorsPractice(false);
    SetCanSave(true);
    SetInfiniteChallenge(false);
    float fov = GetFov();
    if (fov < 50.53401947f) SetFov(50.53401947f);
    if (fov > 88.50715637f) SetFov(88.50715637f);
    SetSprintSpeed(2.0f);
    SetMainMenuColor(false);
}

int Trainer::GetActivePanel() {
    MEMORY_TRY
        return _memory->ReadData<int>(_activePanelOffsets, 1)[0] - 1;
    MEMORY_CATCH(return 0)
}

std::shared_ptr<Trainer::EntityData> Trainer::GetEntityData(int id) {
    MEMORY_TRY
        std::string typeName = _memory->ReadString({_globals, 0x18, id * 8, 0x08, 0x08});
        if (typeName == "Machine_Panel") return GetPanelData(id);
        if (typeName == "Pattern_Point") return GetEPData(id);
        DebugPrint("Don't know how to get data for entity type: " + typeName);
        assert(false);
        return nullptr;
    MEMORY_CATCH(return nullptr)
}

std::shared_ptr<Trainer::EntityData> Trainer::GetPanelData(int id) {
    int nameOffset = _solvedTargetOffset - 0x7C;
    int tracedEdgesOffset = _solvedTargetOffset - 0x6C;
    int stateOffset = _solvedTargetOffset - 0x14;
    int hasEverBeenSolvedOffset = _solvedTargetOffset + 0x04;
    int numDotsOffset = _solvedTargetOffset + 0x11C;
    int dotPositionsOffset = _solvedTargetOffset + 0x12C;

    MEMORY_TRY
        auto data = std::make_shared<EntityData>();
        data->name = _memory->ReadString({_globals, 0x18, id * 8, nameOffset});
        int state = _memory->ReadData<int>({_globals, 0x18, id * 8, stateOffset}, 1)[0];
        int hasEverBeenSolved = _memory->ReadData<int>({_globals, 0x18, id * 8, hasEverBeenSolvedOffset}, 1)[0];
        data->solved = hasEverBeenSolved;
        if (state == 0 && hasEverBeenSolved == 0) data->state = "Has never been solved";
        else if (state == 0 && hasEverBeenSolved == 1) data->state = "Was previously solved";
        else if (state == 1) data->state = "Solved";
        else if (state == 2) data->state = "Failed";
        else if (state == 3) data->state = "Exited";
        else if (state == 4) data->state = "Negation pending";
        else data->state = "Unknown";

        int numEdges = _memory->ReadData<int>({_globals, 0x18, id * 8, tracedEdgesOffset}, 1)[0];
        return data;
    MEMORY_CATCH(return nullptr)
    /* BUG: Traced edges are being re-allocated, and thus moving around. I think memory needs to own this directly, so that it can carefully invalidate a cache entry. Or, it can ComputeOffset(false) to not cache.
    if (numEdges > 0) {
        std::vector<Traced_Edge> edges = _memory->ReadData<Traced_Edge>({_globals, 0x18, id*8, tracedEdgesOffset + 0x08, 0}, numEdges);
        int numDots = _memory->ReadData<int>({_globals, 0x18, id*8, numDotsOffset}, 1)[0];
        std::vector<float> positions = _memory->ReadData<float>({_globals, 0x18, id*8, dotPositionsOffset, 0}, numDots*2);
        for (auto edge : edges) {
            data->tracedEdges.push_back(positions[edge.index_a * 2]); // x1
            data->tracedEdges.push_back(positions[edge.index_a * 2 + 1]); // y1
            data->tracedEdges.push_back(positions[edge.index_b * 2]); // x2
            data->tracedEdges.push_back(positions[edge.index_b * 2 + 1]); // y2
        }
    }
    */
}

std::shared_ptr<Trainer::EntityData> Trainer::GetEPData(int id) {
    auto data = std::make_shared<EntityData>();

    MEMORY_TRY
        // This is a bit of a hack. Oh well.
        auto tmp = _memory->ReadData<int>({_globals, 0x18, id * 8, 0xC8}, 1)[0];
        if (tmp != 0) {
            data->name = _memory->ReadString({_globals, 0x18, id * 8, 0xC8});
        } else {
            data->name = _memory->ReadString({_globals, 0x18, id * 8, 0xC0});
        }
    MEMORY_CATCH((void)0)
    return data;
}

void Trainer::ShowMissingPanels() {
    std::vector<std::string> missingPanels;
    for (const auto& [id, panelName] : PANELS) {
        std::shared_ptr<EntityData> data = GetEntityData(id);
        if (data && !data->solved) missingPanels.push_back(panelName);
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

void Trainer::ShowNearbyEntities() {
    int32_t maxId;
    MEMORY_TRY
        maxId = _memory->ReadData<int>({_globals, 0x14}, 1)[0];
    MEMORY_CATCH(return)

    std::vector<std::pair<float, int32_t>> nearbyEntities(20, {99999.9f, 0});

    auto basePos = GetCameraPos();
    for (int32_t id = 0; id < maxId; id++) {
        if (id == 0x1E465) continue; // Skip over Entity_Human
        MEMORY_TRY
            int32_t entity = _memory->ReadData<int>({_globals, 0x18, id * 8}, 1)[0];
            if (entity == 0) continue;
            std::vector<float> pos = _memory->ReadData<float>({_globals, 0x18, id * 8, 0x24}, 3);

            float norm = std::pow(basePos[0] - pos[0], 2) + std::pow(basePos[1] - pos[1], 2) + std::pow(basePos[2] - pos[2], 2);
            for (int i = 0; i < nearbyEntities.size(); i++) {
                if (norm < nearbyEntities[i].first) {
                    nearbyEntities.insert(nearbyEntities.begin() + i, {norm, id});
                    nearbyEntities.resize(nearbyEntities.size() - 1);
                    break;
                }
            }
        MEMORY_CATCH(continue)
    }

    DebugPrint("Entity ID\tDistance\t     X\t     Y\t     Z\tType");
    for (const auto& [norm, entityId] : nearbyEntities) {
        std::vector<float> pos;
        std::string typeName;
        MEMORY_TRY
            pos = _memory->ReadData<float>({_globals, 0x18, entityId * 8, 0x24}, 3);
            typeName = _memory->ReadString({_globals, 0x18, entityId * 8, 0x08, 0x08});
        MEMORY_CATCH(continue)

        std::stringstream message;
        message << "0x" << std::hex << std::setfill('0') << std::setw(5) << entityId << '\t';
        message << std::sqrt(norm) << '\t';
        message << pos[0] << '\t' << pos[1] << '\t' << pos[2] << '\t' << typeName;
        DebugPrint(message.str());
    }
}

void Trainer::ExportEntities() {
    int32_t maxId;
    MEMORY_TRY
        maxId = _memory->ReadData<int>({_globals, 0x14}, 1)[0];
    MEMORY_CATCH(return)

    DebugPrint("Entity ID\tType\tName\t     X\t     Y\t     Z");
    for (int32_t id = 0; id < maxId; id++) {
        MEMORY_TRY
            int32_t entity = _memory->ReadData<int>({_globals, 0x18, id * 8}, 1)[0];
            if (entity == 0) continue;
            std::string typeName = _memory->ReadString({_globals, 0x18, id * 8, 0x08, 0x08});
            std::string entityName = _memory->ReadString({_globals, 0x18, id * 8, 0x58});
            std::vector<float> pos = _memory->ReadData<float>({_globals, 0x18, id * 8, 0x24}, 3);

            std::stringstream message;
            message << "0x" << std::hex << std::setfill('0') << std::setw(5) << id << '\t';
            message << typeName << '\t';
            message << entityName << '\t';
            message << pos[0] << '\t' << pos[1] << '\t' << pos[2] << '\t';
            DebugPrint(message.str());
        MEMORY_CATCH(continue)
    }
}

bool Trainer::GetNoclip() {
    MEMORY_TRY
        return (bool) _memory->ReadData<int>({_noclipEnabled}, 1)[0];
    MEMORY_CATCH(return false)
}

float Trainer::GetNoclipSpeed() {
    MEMORY_TRY
        return _memory->ReadData<float>({_noclipSpeed}, 1)[0];
    MEMORY_CATCH(return 0.0f)
}

std::vector<float> Trainer::GetPlayerPos() {
    MEMORY_TRY
        return _memory->ReadData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, 3);
    MEMORY_CATCH(return (std::vector<float>{0.0f, 0.0f, 0.0f}))
}

std::vector<float> Trainer::GetCameraPos() {
    MEMORY_TRY
        return _memory->ReadData<float>({_cameraPos}, 3);
    MEMORY_CATCH(return (std::vector<float>{0.0f, 0.0f, 0.0f}))
}

std::vector<float> Trainer::GetCameraAng() {
    MEMORY_TRY
        return _memory->ReadData<float>({_cameraAng}, 2);
    MEMORY_CATCH(return (std::vector<float>{0.0f, 0.0f}))
}

float Trainer::GetFov() {
    if (_fovCurrent == 0) return 0.0f; // FOV is not available on some old patches
    MEMORY_TRY
        return _memory->ReadData<float>({_fovCurrent}, 1)[0];
    MEMORY_CATCH(return 0.0f)
}

bool Trainer::CanSave() {
    MEMORY_TRY
        return _memory->ReadData<byte>({_campaignState, 0x50}, 1)[0] == 0x00;
    MEMORY_CATCH(return true)
}

float Trainer::GetSprintSpeed() {
    MEMORY_TRY
        return _memory->ReadData<float>({_runSpeed}, 1)[0];
    MEMORY_CATCH(return 2.0)
}

bool Trainer::GetInfiniteChallenge() {
    MEMORY_TRY
        return _memory->ReadData<byte>({_recordPlayerUpdate}, 1)[0] == 0x0F;
    MEMORY_CATCH(return false)
}

bool Trainer::GetConsoleOpen() {
    MEMORY_TRY
        return _memory->ReadData<float>(_consoleOpenTarget, 1)[0] == 1.0f;
    MEMORY_CATCH(return false)
}

bool Trainer::GetRandomDoorsPractice() {
    MEMORY_TRY
        return _memory->ReadData<byte>({_doorOpen}, 1)[0] == 0xEB;
    MEMORY_CATCH(return false)
}

void Trainer::SetNoclip(bool enabled) {
    MEMORY_TRY
        _memory->WriteData<byte>({_noclipEnabled}, {(byte) enabled});
    MEMORY_CATCH(return)
}

void Trainer::SetNoclipSpeed(float speed) {
    if (speed <= 0.0f) return;
    MEMORY_TRY
        _memory->WriteData<float>({_noclipSpeed}, {speed});
    MEMORY_CATCH(return)
}

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    MEMORY_TRY
        _memory->WriteData<float>({_globals, 0x18, 0x1E465 * 8, 0x24}, pos);
    MEMORY_CATCH(return)
}

void Trainer::SetCameraPos(const std::vector<float>& pos) {
    MEMORY_TRY
        _memory->WriteData<float>({_cameraPos}, pos);
    MEMORY_CATCH(return)
}

void Trainer::SetCameraAng(const std::vector<float>& ang) {
    MEMORY_TRY
        _memory->WriteData<float>({_cameraAng}, ang);
    MEMORY_CATCH(return)
}

void Trainer::SetFov(float fov) {
    if (!_fovCurrent) return;
    MEMORY_TRY
        _memory->WriteData<float>({_fovCurrent}, {fov});
    MEMORY_CATCH(return)
}

void Trainer::SetCanSave(bool canSave) {
    MEMORY_TRY
        _memory->WriteData<byte>({_campaignState, 0x50}, {canSave ? (byte) 0x00 : (byte) 0x01});
    MEMORY_CATCH(return)
}

void Trainer::SetSprintSpeed(float speed) {
    if (speed == 0) return;
    float multiplier = speed / GetSprintSpeed();
    if (multiplier == 1.0f) return;
    MEMORY_TRY
        _memory->WriteData<float>({_runSpeed}, {speed});
        _memory->WriteData<float>({_walkAcceleration}, {_memory->ReadData<float>({_walkAcceleration}, 1)[0] * multiplier});
        _memory->WriteData<float>({_walkDeceleration}, {_memory->ReadData<float>({_walkDeceleration}, 1)[0] * multiplier});
    MEMORY_CATCH(return)
}

void Trainer::SetConsoleOpen(bool enable) {
    MEMORY_TRY
        if (enable) {
            _memory->WriteData<float>(_consoleWindowYB, {0.0f});
            _memory->WriteData<float>(_consoleOpenTarget, {1.0f});
        } else {
            _memory->WriteData<float>(_consoleOpenTarget, {0.0f});
        }
    MEMORY_CATCH(return)
}

void Trainer::SetInfiniteChallenge(bool enable) {
    MEMORY_TRY
        if (enable) {
            // Jump over abort_speed_run, with NOP padding
            _memory->WriteData<byte>({_recordPlayerUpdate}, {0xEB, 0x07, 0x66, 0x90});
        } else {
            // (original code) Load entity_manager into rcx
            _memory->WriteData<byte>({_recordPlayerUpdate}, {0x48, 0x8B, 0x4B, 0x18});
        }
    MEMORY_CATCH(return)
}

void Trainer::SetMainMenuColor(bool enable) {
    MEMORY_TRY
        if (enable) { // Set the main menu to red by *not* setting the green or blue component.
            _memory->WriteData<byte>({_mainMenuColor}, {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}); // 8-byte NOP
        } else { // Restore the original setting by copy/pasting from the block below.
            std::vector<byte> code = _memory->ReadData<byte>({_mainMenuColor + 0x12}, 8);
            _memory->WriteData<byte>({_mainMenuColor}, code);
        }
    MEMORY_CATCH(return)
}

void Trainer::SetRandomDoorsPractice(bool enable) {
    if (_solvedTargetOffset == 0) return;

    int hasEverBeenSolvedOffset = _solvedTargetOffset + 0x04;
    int idToPowerOffset = _solvedTargetOffset + 0x20;

    MEMORY_TRY
        if (enable) {
            // When the panel is solved, power nothing.
            _memory->WriteData<int>({_globals, 0x18, 0x1983 * 8, idToPowerOffset}, {0x00000000});
            _memory->WriteData<int>({_globals, 0x18, 0x1987 * 8, idToPowerOffset}, {0x00000000});
        } else {
            // When the panel is solved, power the double doors.
            _memory->WriteData<int>({_globals, 0x18, 0x1983 * 8, idToPowerOffset}, {0x00017C68});
            _memory->WriteData<int>({_globals, 0x18, 0x1987 * 8, idToPowerOffset}, {0x00017C68});
        }
    MEMORY_CATCH(return)

    // If the injection state matches the enable request, no futher action is needed.
        if (enable == GetRandomDoorsPractice()) return;

    MEMORY_TRY
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
            // When the panel powers on, do not modify its previously-solved state
            _memory->WriteData<byte>({_powerOn}, {
                0x48, 0x85, 0xC0, // test rax, rax
                0x74, 0x5C, // je +0x5C
                0x48, 0x63, 0x00, // movsxd rax, dword ptr [rax]
                0x85, 0xC0, // test eax, eax
                0x74, 0x55, // je +0x55
                });
        }
    MEMORY_CATCH(return)
}
