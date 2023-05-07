#pragma once

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);
    ~Trainer();

    int GetActivePanel();
    struct EntityData {
        std::string name;
        std::string type;
        std::string state;
        bool solved = false;
        std::vector<float> startPoint;
    };
    std::shared_ptr<EntityData> GetEntityData(int id);
    void ShowMissingPanels();
    void ShowNearbyEntities();
    void ExportEntities();
    void SnapToPoint(const std::vector<float>& point);
    void DisableDistanceGating();

    bool GetNoclip();
    float GetNoclipSpeed();
    std::vector<float> GetPlayerPos();
    std::vector<float> GetCameraPos();
    std::vector<float> GetCameraAng();
    float GetFov();
    bool CanSave();
    float GetSprintSpeed();
    bool GetInfiniteChallenge();
    bool GetConsoleOpen();
    bool GetRandomDoorsPractice();
    bool GetEPOverlay();
    bool GetEPOverlayMinSize();

    void SetNoclip(bool enable);
    void SetNoclipSpeed(float speed);
    void SetPlayerPos(const std::vector<float>& pos);
    void SetCameraPos(const std::vector<float>& pos);
    void SetCameraAng(const std::vector<float>& ang);
    void SetFov(double fov);
    void SetCanSave(bool canSave);
    void SetSprintSpeed(float speed);
    void SetInfiniteChallenge(bool enable);
    void SetConsoleOpen(bool enable);
    bool SaveCampaign();
    void SetMainMenuColor(bool enable);
    void SetMainMenuState(bool open);
    void SetRandomDoorsPractice(bool enable);
    void SetChallengePillarsPractice(bool enable);
    void SetEPOverlay(bool enable);
    void SetEPOverlayMinSize(bool enable);

private:
    std::shared_ptr<Memory> _memory;

    __int64 _noclipEnabled = 0;
    __int64 _noclipSpeed = 0;
    __int64 _cameraPos = 0;
    __int64 _cameraAng = 0;
    __int64 _fovCurrent = 0;
    __int64 _globals = 0;
    __int64 _campaignState = 0;
    __int64 _doorOpen = 0;
    __int64 _doorClose = 0;
    int _solvedTargetOffset = 0;
    __int64 _powerOn = 0;
    __int64 _walkAcceleration = 0;
    __int64 _walkDeceleration = 0;
    __int64 _runSpeed = 0;
    __int64 _recordPlayerUpdate = 0;
    std::vector<__int64> _activePanelOffsets;
    __int64 _mainMenuColor = 0;
    std::vector<__int64> _consoleOpenTarget;
    std::vector<__int64> _consoleWindowYB;
    __int64 _wantCampaignSave = 0;
    int _epNameOffset = 0;
    __int64 _menuOpenTarget = 0;
    __int64 _showPatternStatus = 0;
    __int64 _drawPatternManager = 0;

    std::shared_ptr<EntityData> GetPanelData(int id);
    std::shared_ptr<EntityData> GetEPData(int id);
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
