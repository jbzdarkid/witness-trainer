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
#define DOORS_PRACTICE 0x409

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
HWND g_noclipSpeed, g_currentPos, g_currentAng, g_savedPos, g_savedAng, g_fovCurrent;
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

std::wstring GetWindowTextStr(HWND hwnd) {
    std::wstring text(128, L'\0');
    int length = GetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
    text.resize(length);
    return text;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
        } else if (message == WM_COMMAND || message == WM_TIMER || message == WM_NOTIFY) {
            switch (LOWORD(wParam)) {
                case HEARTBEAT:
                    switch ((ProcStatus)lParam) {
                        case ProcStatus::NotRunning:
                            if (g_trainer) {
                                g_trainer = nullptr;
                                // If you restart the game, both of these will be disabled.
                                CheckDlgButton(hwnd, NOCLIP_ENABLED, false);
                                CheckDlgButton(hwnd, DOORS_PRACTICE, false);
                            }
                            break;
                        case ProcStatus::Running:
                            if (!g_trainer) {
                                // Trainer started, game is running
                                g_trainer = std::make_shared<Trainer>(g_witnessProc);
                                SetWindowText(g_noclipSpeed, std::to_wstring(g_trainer->GetNoclipSpeed()).c_str());
                                SetWindowText(g_fovCurrent, std::to_wstring(g_trainer->GetFov()).c_str());
                                CheckDlgButton(hwnd, NOCLIP_ENABLED, g_trainer->GetNoclip());
                                CheckDlgButton(hwnd, DOORS_PRACTICE, g_trainer->GetRandomDoorsPractice());
                            }
                            g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));
                            SetPosText(g_trainer->GetCameraPos(), g_currentPos);
                            SetAngText(g_trainer->GetCameraAng(), g_currentAng);
                            if (g_hwnd != GetActiveWindow()) {
                                // Only replace when in the background (i.e. if someone changed their FOV in-game)
                                SetWindowText(g_fovCurrent, std::to_wstring(g_trainer->GetFov()).c_str());
                            }
                            break;
                        }
                    break;
                case NOCLIP_ENABLED:
                    if (g_trainer) {
                        bool noclipEnabled = IsDlgButtonChecked(hwnd, NOCLIP_ENABLED);
                        g_trainer->SetNoclip(!noclipEnabled);
                        CheckDlgButton(hwnd, NOCLIP_ENABLED, !noclipEnabled);
                    }
                    break;
                case NOCLIP_SPEED:
                    if (g_trainer) {
                        std::wstring text = GetWindowTextStr(g_noclipSpeed);
                        g_trainer->SetNoclipSpeed(wcstof(text.c_str(), nullptr));
                    }
                    break;
                case SAVE_POS:
                    if (g_trainer) {
                        savedPos = g_trainer->GetCameraPos();
                        SetPosText(savedPos, g_savedPos);
                    }
                    break;
                case LOAD_POS:
                    if (g_trainer) {
                        g_trainer->SetCameraPos(savedPos);
                        SetPosText(savedPos, g_currentPos);
                    }
                    break;
                case SAVE_ANG:
                    if (g_trainer) {
                        savedAng = g_trainer->GetCameraAng();
                        SetAngText(savedAng, g_savedAng);
                    }
                    break;
                case LOAD_ANG:
                    if (g_trainer) {
                        g_trainer->SetCameraAng(savedAng);
                        SetAngText(savedAng, g_currentAng);
                    }
                    break;
                case FOV_CURRENT:
                    if (g_trainer) {
                        std::wstring text(128, L'\0');
                        int length = GetWindowText(g_fovCurrent, text.data(), static_cast<int>(text.size()));
                        text.resize(length);
                        g_trainer->SetFov(wcstof(text.c_str(), nullptr));
                    }
                    break;
                case DOORS_PRACTICE:
                    if (g_trainer) {
                        bool doorsPractice = IsDlgButtonChecked(hwnd, DOORS_PRACTICE);
                        g_trainer->SetRandomDoorsPractice(!doorsPractice);
                        CheckDlgButton(hwnd, DOORS_PRACTICE, !doorsPractice);
                    }
                    break;
            }
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

    CreateLabel(10, 10, 100, L"Noclip Enabled");
    CreateCheckbox(115, 12, NOCLIP_ENABLED);

    CreateLabel(10, 34, 100, L"Noclip Speed");
    g_noclipSpeed = CreateText(100, 30, 150, L"", NOCLIP_SPEED);

    CreateLabel(10, 64, 100, L"Field of View");
    g_fovCurrent = CreateText(100, 60, 150, L"", FOV_CURRENT);

    CreateLabel(10, 90, 160, L"Random Doors Practice");
    CreateCheckbox(175, 92, DOORS_PRACTICE);

    CreateButton(10, 110, 100, L"Save Position", SAVE_POS);
    g_currentPos = CreateLabel(15, 140, 90, 48);
    SetPosText({0.0f, 0.0f, 0.0f}, g_currentPos);

    CreateButton(110, 110, 100, L"Load Position", LOAD_POS);
    g_savedPos = CreateLabel(115, 140, 90, 48);
    SetPosText({0.0f, 0.0f, 0.0f}, g_savedPos);

    CreateButton(10, 190, 100, L"Save Angle", SAVE_ANG);
    g_currentAng = CreateLabel(15, 220, 90, 32);
    SetAngText({0.0f, 0.0f}, g_currentAng);

    CreateButton(110, 190, 100, L"Load Angle", LOAD_ANG);
    g_savedAng = CreateLabel(115, 220, 90, 32);
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
