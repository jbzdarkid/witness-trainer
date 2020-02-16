#pragma once

class Trainer {
public:
    Trainer(const std::shared_ptr<Memory>& memory);

    int GetActivePanel();
    struct PanelData {
        std::string name;
        std::string state;
        bool solved = false;
        std::vector<float> tracedEdges;
    };
    std::shared_ptr<PanelData> GetPanelData(int id);
    void ShowMissingPanels();
    void ShowNearbyEntities();

    bool GetNoclip();
    float GetNoclipSpeed();
    std::vector<float> GetPlayerPos();
    std::vector<float> GetCameraPos();
    std::vector<float> GetCameraAng();
    float GetFov();
    bool CanSave();
    float GetSprintSpeed();
    bool GetInfiniteChallenge();
    bool GetRandomDoorsPractice();

    void SetNoclip(bool enabled);
    void SetNoclipSpeed(float speed);
    void SetPlayerPos(const std::vector<float>& pos);
    void SetCameraPos(const std::vector<float>& pos);
    void SetCameraAng(const std::vector<float>& ang);
    void SetFov(float fov);
    void SetCanSave(bool canSave);
    void SetSprintSpeed(float speed);
    void SetInfiniteChallenge(bool enable);
    void SetRandomDoorsPractice(bool enable);

private:
    std::shared_ptr<Memory> _memory;

    int _noclipEnabled = 0;
    int _noclipSpeed = 0;
    int _cameraPos = 0;
    int _cameraAng = 0;
    int _fovCurrent = 0;
    int _globals = 0;
    int _campaignState = 0;
    int _doorOpen = 0;
    int _doorClose = 0;
    int _solvedTargetOffset = 0;
    int _powerOn = 0;
    int _walkAcceleration = 0;
    int _walkDeceleration = 0;
    int _runSpeed = 0;
    int _recordPlayerUpdate = 0;
    std::vector<__int64> _activePanelOffsets;

    struct Traced_Edge {
        int index_a;
        int index_b;
        int id_a;
        int id_b;
        float t;
        float t_highest;
        float position_a[3];
        float position_b[3];
        int flags;
    };
};
