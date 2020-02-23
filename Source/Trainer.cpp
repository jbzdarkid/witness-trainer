#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

Trainer::Trainer(const std::shared_ptr<Memory>& memory) : _memory(memory){
    _memory->AddSigScan({0x84, 0xC0, 0x75, 0x59, 0xBA, 0x20, 0x00, 0x00, 0x00}, [&](int offset, int index, const std::vector<byte>& data){
        // This int is actually desired_movement_direction, which immediately preceeds camera_position
        _cameraPos = Memory::ReadStaticInt(offset, index + 0x19, data) + 0x10;

        // This doesn't have a consistent offset from the scan, so search until we find "mov eax, [addr]"
        for (; index < data.size(); index++) {
            if (data[index-2] == 0x8B && data[index-1] == 0x05) {
                _noclipEnabled = Memory::ReadStaticInt(offset, index, data);
                break;
            }
        }
    });

    _memory->AddSigScan({0xC7, 0x45, 0x77, 0x00, 0x00, 0x80, 0x3F, 0xC7, 0x45, 0x7F, 0x00, 0x00, 0x80, 0x3F}, [&](int offset, int index, const std::vector<byte>& data){
        _cameraAng = Memory::ReadStaticInt(offset, index + 0x17, data);
    });

    _memory->AddSigScan({0x0F, 0x29, 0x7C, 0x24, 0x70, 0x44, 0x0F, 0x29, 0x54, 0x24, 0x60}, [&](int offset, int index, const std::vector<byte>& data){
        _noclipSpeed = Memory::ReadStaticInt(offset, index + 0x4F, data);
    });

    _memory->AddSigScan({0x76, 0x09, 0xF3, 0x0F, 0x11, 0x05}, [&](int offset, int index, const std::vector<byte>& data){
        _fovCurrent = Memory::ReadStaticInt(offset, index + 0x0F, data);
    });

    _memory->AddSigScan({0x74, 0x41, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x48, 0x8B, 0x48, 0x10}, [&](int offset, int index, const std::vector<byte>& data){
        _globals = Memory::ReadStaticInt(offset, index + 0x14, data);
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
        _campaignState = Memory::ReadStaticInt(offset, index + 0x27, data);
    });

    _memory->AddSigScan({0xF3, 0x0F, 0x59, 0xFD, 0xF3, 0x0F, 0x5C, 0xC8}, [&](int offset, int index, const std::vector<byte>& data) {
        // This doesn't have a consistent offset from the scan, so search until we find "jmp +08"
        for (; index < data.size(); index++) {
            if (data[index-2] == 0xEB && data[index-1] == 0x08) {
                _walkAcceleration = Memory::ReadStaticInt(offset, index - 0x06, data);
                _walkDeceleration = Memory::ReadStaticInt(offset, index + 0x04, data);
                break;
            }
        }
        // Once again, there's no consistent offset, so we read until "movss xmm1, [addr]"
        for (; index < data.size(); index++) {
            if (data[index-4] == 0xF3 && data[index-3] == 0x0F && data[index-2] == 0x10 && data[index-1] == 0x0D) {
                _runSpeed = Memory::ReadStaticInt(offset, index, data);
                break;
            }
        }
    });

    _memory->AddSigScan({0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xE9, 0xB3}, [&](int offset, int index, const std::vector<byte>& data) {
        _recordPlayerUpdate = offset + index - 0x0C;
    });

    _memory->AddSigScan({0xF2, 0x0F, 0x58, 0xC8, 0x66, 0x0F, 0x5A, 0xC1, 0xF2}, [&](int offset, int index, const std::vector<byte>& data) {
        _activePanelOffsets.push_back(Memory::ReadStaticInt(offset, index + 0x36, data) + 1); // +1 because the line ends with an extra byte
        _activePanelOffsets.push_back(data[index + 0x5A]); // This is 0x10 in both versions I have, but who knows.
        _activePanelOffsets.push_back(*(int*)&data[index + 0x54]);
    });

    _memory->ExecuteSigScans();
}

int Trainer::GetActivePanel() {
    if (_activePanelOffsets.empty()) return 0;
    return _memory->ReadData<int>(_activePanelOffsets, 1)[0] - 1;
}

std::shared_ptr<Trainer::PanelData> Trainer::GetPanelData(int id) {
    if (_solvedTargetOffset == 0) return nullptr;

    int nameOffset = _solvedTargetOffset - 0x7C;
    int tracedEdgesOffset = _solvedTargetOffset - 0x6C;
    int stateOffset = _solvedTargetOffset - 0x14;
    int hasEverBeenSolvedOffset = _solvedTargetOffset + 0x04;
    int numDotsOffset = _solvedTargetOffset + 0x11C;
    int dotPositionsOffset = _solvedTargetOffset + 0x12C;

    auto data = std::make_shared<PanelData>();
    {
        std::vector<char> tmp = _memory->ReadData<char>({_globals, 0x18, id*8, nameOffset, 0}, 100);
        data->name = std::string(tmp.begin(), tmp.end());
        // Remove garbage past the null terminator (we read 100 chars, but the string was probably shorter)
        data->name.resize(strnlen_s(tmp.data(), tmp.size()));
    }
    int state = _memory->ReadData<int>({_globals, 0x18, id*8, stateOffset}, 1)[0];
    int hasEverBeenSolved = _memory->ReadData<int>({_globals, 0x18, id*8, hasEverBeenSolvedOffset}, 1)[0];
    data->solved = hasEverBeenSolved;
    if (state == 0 && hasEverBeenSolved == 0) data->state = "Has never been solved";
    else if (state == 0 && hasEverBeenSolved == 1) data->state = "Was previously solved";
    else if (state == 1) data->state = "Solved";
    else if (state == 2) data->state = "Failed";
    else if (state == 3) data->state = "Exited";
    else if (state == 4) data->state = "Negation pending";
    else if (state == 5) data->state = "???";

    int numEdges = _memory->ReadData<int>({_globals, 0x18, id*8, tracedEdgesOffset}, 1)[0];
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
    return data;
}

void Trainer::ShowMissingPanels() {
    std::vector<std::string> missingPanels;
    for (const auto& [id, panelName] : PANELS) {
        std::shared_ptr<PanelData> data;
        try {
            data = GetPanelData(id);
        } catch (MemoryException exc) {
            continue;
        }
        if (!data->solved) missingPanels.push_back(panelName);
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
    MessageBoxA(NULL, message.c_str(), "Unsolved panels, in no particular order", MB_OK);
}

void Trainer::ShowNearbyEntities() {
    int32_t maxId;
    try {
        maxId = _memory->ReadData<int>({_globals, 0x14}, 1)[0];
    } catch (MemoryException exc) {
        return;
    }

    std::vector<std::pair<float, int32_t>> nearbyEntities(10, {99999.9f, 0});

    auto basePos = GetCameraPos();
    for (int32_t id = 0; id < maxId; id++) {
        if (id == 0x1E465) continue; // Skip over Entity_Human
        try {
            int32_t entity = _memory->ReadData<int>({_globals, 0x18, id*8}, 1)[0];
            if (entity == 0) continue;
            std::vector<float> pos = _memory->ReadData<float>({_globals, 0x18, id*8, 0x24}, 3);

            float norm = std::pow(basePos[0] - pos[0], 2) + std::pow(basePos[1] - pos[1], 2) + std::pow(basePos[2] - pos[2], 2);
            for (int i=0; i<nearbyEntities.size(); i++) {
                if (norm < nearbyEntities[i].first) {
                    nearbyEntities.insert(nearbyEntities.begin() + i, {norm, id});
                    nearbyEntities.resize(nearbyEntities.size() - 1);
                    break;
                }
            }
        } catch (MemoryException exc) {
            continue;
        }
    }

    std::stringstream message;
    message << "Entity ID\tDistance\t     X\t     Y\t     Z\tType\r\n";
    for (const auto& [norm, entityId] : nearbyEntities) {
        message << "0x" << std::hex << std::setfill('0') << std::setw(5) << entityId << '\t';
        message << std::sqrt(norm) << '\t';

        std::vector<float> pos = _memory->ReadData<float>({_globals, 0x18, entityId*8, 0x24}, 3);
        message << pos[0] << '\t' << pos[1] << '\t' << pos[2] << '\t';

        std::vector<char> tmp = _memory->ReadData<char>({_globals, 0x18, entityId*8, 0x08, 0x08, 0}, 100);
        std::string typeName = std::string(tmp.begin(), tmp.end());
        // Remove garbage past the null terminator (we read 100 chars, but the string was probably shorter)
        typeName.resize(strnlen_s(tmp.data(), tmp.size()));
        message << typeName << "\r\n";
    }
    MessageBoxA(NULL, message.str().c_str(), "Nearby entities", MB_OK);
}

bool Trainer::GetNoclip() {
    if (_noclipEnabled == 0) return false;
    return (bool)_memory->ReadData<int>({_noclipEnabled}, 1)[0];
}

float Trainer::GetNoclipSpeed() {
    if (_noclipSpeed == 0) return 0.0f;
    return _memory->ReadData<float>({_noclipSpeed}, 1)[0];
}

std::vector<float> Trainer::GetPlayerPos() {
    if (_globals == 0) return {0.0f, 0.0f, 0.0f};
    return _memory->ReadData<float>({_globals, 0x18, 0x1E465*8, 0x24}, 3);
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

float Trainer::GetSprintSpeed() {
    if (_runSpeed == 0) return 2.0;
    return _memory->ReadData<float>({_runSpeed}, 1)[0];
}

bool Trainer::GetInfiniteChallenge() {
    if (_recordPlayerUpdate == 0) return false;
    return _memory->ReadData<byte>({_recordPlayerUpdate}, 1)[0] == 0x0F;
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

void Trainer::SetPlayerPos(const std::vector<float>& pos) {
    if (_globals == 0) return;
    _memory->WriteData<float>({_globals, 0x18, 0x1E465*8, 0x24}, pos);
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

void Trainer::SetSprintSpeed(float speed) {
    if (_walkAcceleration == 0 || _walkDeceleration == 0 || _runSpeed == 0 || speed == 0) return;
    float multiplier = speed / GetSprintSpeed();
    if (multiplier == 1.0f) return;
    _memory->WriteData<float>({_runSpeed}, {speed});
    _memory->WriteData<float>({_walkAcceleration}, {_memory->ReadData<float>({_walkAcceleration}, 1)[0] * multiplier});
    _memory->WriteData<float>({_walkDeceleration}, {_memory->ReadData<float>({_walkDeceleration}, 1)[0] * multiplier});
}

void Trainer::SetInfiniteChallenge(bool enable) {
    if (_recordPlayerUpdate == 0) return;
    if (enable) {
        // Jump over abort_speed_run, with NOP padding
        _memory->WriteData<byte>({_recordPlayerUpdate}, {0xEB, 0x07, 0x66, 0x90});
    } else {
        // (original code) Load entity_manager into rcx
        _memory->WriteData<byte>({_recordPlayerUpdate}, {0x48, 0x8B, 0x4B, 0x18});
    }
}

void Trainer::SetRandomDoorsPractice(bool enable) {
    if (_globals == 0 || _solvedTargetOffset == 0 || _doorOpen == 0 || _doorClose == 0 || _powerOn == 0) return;

    int hasEverBeenSolvedOffset = _solvedTargetOffset + 0x04;
    int idToPowerOffset = _solvedTargetOffset + 0x20;

    if (enable) {
        // When the panel is solved, power nothing.
        _memory->WriteData<int>({_globals, 0x18, 0x1983*8, idToPowerOffset}, {0x00000000});
        _memory->WriteData<int>({_globals, 0x18, 0x1987*8, idToPowerOffset}, {0x00000000});
    } else {
        // When the panel is solved, power the double doors.
        _memory->WriteData<int>({_globals, 0x18, 0x1983*8, idToPowerOffset}, {0x00017C68});
        _memory->WriteData<int>({_globals, 0x18, 0x1987*8, idToPowerOffset}, {0x00017C68});
    }

    // If the injection state matches the enable request, no futher action is needed.
    if (enable == GetRandomDoorsPractice()) return;

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
}
