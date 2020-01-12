#pragma once

class Trainer {
public:
    Trainer(const std::shared_ptr<Memory>& memory);

    bool GetNoclip();
    float GetNoclipSpeed();
    std::vector<float> GetCameraPos();
    std::vector<float> GetCameraAng();
    float GetFov();

    void SetNoclip(bool enabled);
    void SetNoclipSpeed(float speed);
    void SetCameraPos(const std::vector<float>& pos);
    void SetCameraAng(const std::vector<float>& ang);
    void SetFov(float fov);

private:
    int ReadStaticInt(int offset, int index, const std::vector<byte>& data);

    std::shared_ptr<Memory> _memory;

    int _noclipEnabled = 0;
    int _noclipSpeed = 0;
    int _cameraPos = 0;
    int _cameraAng = 0;
    int _fovCurrent = 0;
};
