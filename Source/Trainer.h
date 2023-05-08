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

    __int64 _noclipEnabled = 0; // globals.camera_mode
    __int64 _noclipSpeed = 0; // globals.free_camera_speed
    __int64 _cameraPos = 0; // camera_position
    __int64 _cameraAng = 0; // aiming_theta, aiming_phi
    __int64 _fovCurrent = 0; // Unnamed, but the scan lands somewhere in init_scripted_stuff()
    __int64 _globals = 0; // globals
    __int64 _campaignState = 0; // globals.campaign_state
    __int64 _doorOpen = 0; // Entity_Door::open()
    __int64 _doorClose = 0; // Entity_Door::close()
    int _solvedTargetOffset = 0; // Entity_Machine_Panel.solved_t_target
    __int64 _powerOn = 0; // Entity_Machine_Panel::power_on()
    __int64 _walkAcceleration = 0; // globals.walk_acceleration
    __int64 _walkDeceleration = 0; // globals.walk_deceleration
    __int64 _runSpeed = 0; // RUN_SPEED
    __int64 _recordPlayerUpdate = 0; // Entity_Record_Player.play_state
    std::vector<__int64> _activePanelOffsets; // globals.gesture_handler.pattern_manager.gesture_id
    __int64 _mainMenuColor = 0; // draw_menu_general()
    std::vector<__int64> _consoleOpenTarget; // globals.console.open_t_target
    std::vector<__int64> _consoleWindowYB; // globals.console.window_y_b
    __int64 _wantCampaignSave = 0; // globals.want_campaign_save
    int _epNameOffset = 0; // Entity_Pattern_Point.pattern_name
    __int64 _menuOpenTarget = 0; // menu_open_t_target
    __int64 _showPatternStatus = 0; // show_pattern_status
    __int64 _drawPatternManager = 0; // draw_pattern_manager()

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
