#include "pch.h"
#include "Richedit.h"
#include "Version.h"

#include "Trainer.h"

#define HEARTBEAT 0x401
#define NOCLIP_ENABLED 0x402
#define NOCLIP_SPEED 0x403
#define SAVE_POS 0x404
#define SAVE_ANG 0x405
#define LOAD_POS 0x406
#define LOAD_ANG 0x407
#define FOV_CURRENT 0x408
#define CAN_SAVE 0x409
#define SPRINT_SPEED 0x410
#define INFINITE_CHALLENGE 0x411
#define DOORS_PRACTICE 0x412
#define ACTIVATE_GAME 0x413

// Feature requests:
// - show collision?

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
HWND g_noclipSpeed, g_currentPos, g_currentAng, g_savedPos, g_savedAng, g_fovCurrent, g_sprintSpeed;
std::vector<float> savedPos = {0.0f, 0.0f, 0.0f};
std::vector<float> savedAng = {0.0f, 0.0f};
auto g_witnessProc = std::make_shared<Memory>(L"witness64_d3d11.exe");

void SetPosText(const std::vector<float>& pos, HWND hwnd) {
    std::wstring text(40, '\0');
    swprintf_s(text.data(), text.size() + 1, L"X %8.3f\nY %8.3f\nZ %8.3f", pos[0], pos[1], pos[2]);
    SetWindowText(hwnd, text.c_str());
}

void SetAngText(const std::vector<float>& ang, HWND hwnd) {
    std::wstring text(25, '\0');
    swprintf_s(text.data(), text.size() + 1, L"\u0398 %8.5f\n\u03A6 %8.5f", ang[0], ang[1]);
    SetWindowText(hwnd, text.c_str());
}

float GetWindowFloat(HWND hwnd) {
    std::wstring text(128, L'\0');
    int length = GetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
    text.resize(length);
    return wcstof(text.c_str(), nullptr);
}

// https://stackoverflow.com/a/12662950
void ToggleOption(int message, void (Trainer::*setter)(bool)) {
    if (!g_trainer) return;
    bool enabled = IsDlgButtonChecked(g_hwnd, message);
    (*g_trainer.*setter)(!enabled);
    CheckDlgButton(g_hwnd, message, !enabled);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (message != WM_COMMAND) {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    try {
        if (LOWORD(wParam) == HEARTBEAT) {
            switch ((ProcStatus)lParam) {
            case ProcStatus::NotRunning:
                if (g_trainer) {
                    g_trainer = nullptr;
                    // If you restart the game, restore the defaults for a new game
                    CheckDlgButton(hwnd, NOCLIP_ENABLED, false);
                    CheckDlgButton(hwnd, DOORS_PRACTICE, false);
                    CheckDlgButton(hwnd, CAN_SAVE, true);
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, false);
                }
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Trainer started, game is running
                    g_trainer = std::make_shared<Trainer>(g_witnessProc);
                    SetWindowText(g_noclipSpeed, std::to_wstring(g_trainer->GetNoclipSpeed()).c_str());
                    SetWindowText(g_fovCurrent, std::to_wstring(g_trainer->GetFov()).c_str());
                    SetWindowText(g_sprintSpeed, std::to_wstring(g_trainer->GetSprintSpeed()).c_str());
                    CheckDlgButton(hwnd, NOCLIP_ENABLED, g_trainer->GetNoclip());
                    CheckDlgButton(hwnd, DOORS_PRACTICE, g_trainer->GetRandomDoorsPractice());
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetInfiniteChallenge());
                }
                g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));
                g_trainer->SetCanSave(IsDlgButtonChecked(hwnd, CAN_SAVE));
                SetPosText(g_trainer->GetCameraPos(), g_currentPos);
                SetAngText(g_trainer->GetCameraAng(), g_currentAng);
                if (g_hwnd != GetActiveWindow()) {
                    // Only replace when in the background (i.e. if someone changed their FOV in-game)
                    SetWindowText(g_fovCurrent, std::to_wstring(g_trainer->GetFov()).c_str());
                }
                break;
            }
            return 0;
        }

        // All other messages need the trainer to be live in order to execute.
        if (!g_trainer) return DefWindowProc(hwnd, message, wParam, lParam);

        WORD command = LOWORD(wParam);
        if (command == NOCLIP_ENABLED)           ToggleOption(NOCLIP_ENABLED, &Trainer::SetNoclip);
        else if (command == CAN_SAVE)            ToggleOption(CAN_SAVE, &Trainer::SetCanSave);
        else if (command == INFINITE_CHALLENGE)  ToggleOption(INFINITE_CHALLENGE, &Trainer::SetInfiniteChallenge);
        else if (command == DOORS_PRACTICE)      ToggleOption(DOORS_PRACTICE, &Trainer::SetRandomDoorsPractice);
        else if (command == NOCLIP_SPEED)        g_trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
        else if (command == FOV_CURRENT)         g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
        else if (command == SPRINT_SPEED)        g_trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
        else if (command == ACTIVATE_GAME)       g_witnessProc->BringToFront();
        else if (command == SAVE_POS) {
            savedPos = g_trainer->GetCameraPos();
            SetPosText(savedPos, g_savedPos);
        } else if (command == LOAD_POS) {
            g_trainer->SetCameraPos(savedPos);
            SetPosText(savedPos, g_currentPos);
        } else if (command == SAVE_ANG) {
            savedAng = g_trainer->GetCameraAng();
            SetAngText(savedAng, g_savedAng);
        } else if (command == LOAD_ANG) {
            g_trainer->SetCameraAng(savedAng);
            SetAngText(savedAng, g_currentAng);
        }

    } catch (MemoryException exc) {
        MemoryException::HandleException(exc);
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

#pragma warning(push)
#pragma warning(disable: 4312)
HWND CreateLabel(int x, int y, int width, int height, LPCWSTR text=L"") {
    return CreateWindow(L"STATIC", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, width, height, g_hwnd, NULL, g_hInstance, NULL);
}

HWND CreateLabel(int x, int y, int width, LPCWSTR text) {
    return CreateLabel(x, y, width, 16, text);
}

HWND CreateButton(int x, int y, int width, LPCWSTR text, int message) {
    return CreateWindow(L"BUTTON", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        x, y, width, 26, g_hwnd, (HMENU)message, g_hInstance, NULL);
}

HWND CreateCheckbox(int x, int y, int message) {
    return CreateWindow(L"BUTTON", L"",
        WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
        x, y, 12, 12, g_hwnd, (HMENU)message, g_hInstance, NULL);
}

HWND CreateText(int x, int y, int width, LPCWSTR defaultText = L"", int message=NULL) {
    return CreateWindow(MSFTEDIT_CLASS, defaultText,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER,
        x, y, width, 26, g_hwnd, (HMENU)message, g_hInstance, NULL);
}
#pragma warning(pop)

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    LoadLibrary(L"Msftedit.dll");
    WNDCLASSW wndClass = {
        CS_HREDRAW | CS_VREDRAW,
        WndProc,
        0,
        0,
        hInstance,
        NULL,
        LoadCursor(nullptr, IDC_ARROW),
        (HBRUSH)(COLOR_WINDOW+1),
        WINDOW_CLASS,
        WINDOW_CLASS,
    };
    RegisterClassW(&wndClass);

    g_hInstance = hInstance;

    RECT rect;
    GetClientRect(GetDesktopWindow(), &rect);
    g_hwnd = CreateWindow(WINDOW_CLASS, PRODUCT_NAME, WS_OVERLAPPEDWINDOW,
      rect.right - 550, 200, 500, 500, nullptr, nullptr, hInstance, nullptr);

    int y = 10;

    CreateLabel(10, y, 100, L"Noclip Enabled");
    CreateCheckbox(115, y+2, NOCLIP_ENABLED);
    CreateButton(350, y, 120, L"Switch to game", ACTIVATE_GAME);
    y += 20;

    CreateLabel(10, y+4, 100, L"Noclip Speed");
    g_noclipSpeed = CreateText(100, y, 150, L"", NOCLIP_SPEED);
    y += 30;

    CreateLabel(10, y+4, 100, L"Sprint Speed");
    g_sprintSpeed = CreateText(100, y, 150, L"", SPRINT_SPEED);
    y += 30;

    CreateLabel(10, y+4, 100, L"Field of View");
    g_fovCurrent = CreateText(100, y, 150, L"", FOV_CURRENT);
    y += 30;

    CreateLabel(10, y, 130, L"Can save the game");
    CreateCheckbox(145, y+2, CAN_SAVE);
    CheckDlgButton(g_hwnd, CAN_SAVE, true);
    y += 20;

    CreateLabel(10, y, 155, L"Random Doors Practice");
    CreateCheckbox(170, y+2, DOORS_PRACTICE);
    y += 20;

    CreateLabel(10, y, 185, L"Disable Challenge time limit");
    CreateCheckbox(200, y+2, INFINITE_CHALLENGE);
    y += 20;

    CreateButton(10, y, 100, L"Save Position", SAVE_POS);
    CreateButton(110, y, 100, L"Load Position", LOAD_POS);
    y += 30;
    g_currentPos = CreateLabel(15, y, 90, 48);
    g_savedPos = CreateLabel(115, y, 90, 48);
    y += 50;
    SetPosText({0.0f, 0.0f, 0.0f}, g_currentPos);
    SetPosText({0.0f, 0.0f, 0.0f}, g_savedPos);

    CreateButton(10, y, 100, L"Save Angle", SAVE_ANG);
    CreateButton(110, y, 100, L"Load Angle", LOAD_ANG);
    y += 30;
    g_currentAng = CreateLabel(15, y, 90, 32);
    g_savedAng = CreateLabel(115, y, 90, 32);
    y += 40;
    SetAngText({0.0f, 0.0f}, g_currentAng);
    SetAngText({0.0f, 0.0f}, g_savedAng);

    g_witnessProc->StartHeartbeat(g_hwnd, HEARTBEAT);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) == TRUE) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
}
