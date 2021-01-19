#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"

#define HEARTBEAT           0x401
#define ACTIVATE_GAME       0x402
#define INFINITE_CHALLENGE  0x403
#define MK_CHALLENGE        0x404
#define CHALLENGE_REROLL    0x405
#define SET_SEED            0x406
#define RANDOM_SEED         0x407
#define SHOW_SEED           0x408

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
// HWND g_noclipSpeed, g_currentPos, g_savedPos, g_fovCurrent, g_sprintSpeed, g_activePanel, g_panelDist, g_panelName, g_panelState, g_panelPicture, g_activateGame;
HWND g_activateGame, g_seed;
auto g_witnessProc = std::make_shared<Memory>(L"witness64_d3d11.exe");

void SetStringText(HWND hwnd, const std::string& text) {
    static std::unordered_map<HWND, std::string> hwndText;
    auto search = hwndText.find(hwnd);
    if (search != hwndText.end()) {
        if (search->second == text) return;
        search->second = text;
    } else {
        hwndText[hwnd] = text;
    }

    SetWindowTextA(hwnd, text.c_str());
}

void SetStringText(HWND hwnd, const std::wstring& text) {
    static std::unordered_map<HWND, std::wstring> hwndText;
    auto search = hwndText.find(hwnd);
    if (search != hwndText.end()) {
        if (search->second == text) return;
        search->second = text;
    } else {
        hwndText[hwnd] = text;
    }

    SetWindowTextW(hwnd, text.c_str());
}

std::wstring GetWindowString(HWND hwnd) {
    SetLastError(0); // GetWindowTextLength does not clear LastError.
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length, L'\0');
    length = GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size() + 1)); // Length includes the null terminator
    text.resize(length);
    return text;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
            break; // LOWORD(wParam) contains the command
        case WM_CTLCOLORSTATIC:
            // Get rid of the gross gray background. https://stackoverflow.com/a/4495814
            SetBkColor((HDC)wParam, RGB(255, 255, 255));
            return 0;
        case HEARTBEAT:
            switch ((ProcStatus)wParam) {
            case ProcStatus::Stopped:
            case ProcStatus::NotRunning:
                // Don't discard any settings, just free the trainer.
                if (g_trainer) {
                    g_trainer = nullptr;
                    SetStringText(g_activateGame, L"Launch game");
                }
                break;
            case ProcStatus::Reload:
            case ProcStatus::NewGame:
            case ProcStatus::Started:
                if (!g_trainer) {
                    // Process just started (we were already alive), enforce our settings.
                    g_trainer = Trainer::Create(g_witnessProc);
                }
                if (!g_trainer) break;
                // Or, we started a new game / loaded a save, in which case some of the entity data might have been reset.
                g_trainer->SetInfiniteChallenge(IsDlgButtonChecked(hwnd, INFINITE_CHALLENGE));
                g_trainer->SetMkChallenge(IsDlgButtonChecked(hwnd, MK_CHALLENGE));
                SetStringText(g_activateGame, L"Switch to game");
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Process was already running, and we just started. Load settings from the game.
                    g_trainer = Trainer::Create(g_witnessProc);
                    if (!g_trainer) break;
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetInfiniteChallenge());
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetMkChallenge());
                    SetStringText(g_activateGame, L"Switch to game");
                } else {
                    // Process was already running, and so were we (this recurs every heartbeat). Enforce settings and apply repeated actions.
                }

                break;
            }
            return 0;
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    WORD command = LOWORD(wParam);
    if (command == INFINITE_CHALLENGE) {
        bool enabled = !IsDlgButtonChecked(g_hwnd, INFINITE_CHALLENGE);
        CheckDlgButton(g_hwnd, INFINITE_CHALLENGE, enabled);
        if (enabled) CheckDlgButton(g_hwnd, MK_CHALLENGE, false);
        if (g_trainer) g_trainer->SetInfiniteChallenge(enabled);
    } else if (command == MK_CHALLENGE) {
        bool enabled = !IsDlgButtonChecked(g_hwnd, MK_CHALLENGE);
        CheckDlgButton(g_hwnd, MK_CHALLENGE, enabled);
        if (enabled) CheckDlgButton(g_hwnd, INFINITE_CHALLENGE, false);
        if (g_trainer) g_trainer->SetMkChallenge(enabled);
    } else if (command == CHALLENGE_REROLL) {
        bool enabled = !IsDlgButtonChecked(g_hwnd, CHALLENGE_REROLL);
        CheckDlgButton(g_hwnd, CHALLENGE_REROLL, enabled);
        if (g_trainer) g_trainer->SetChallengeReroll(enabled);
    }

    // All other messages need the trainer to be live in order to execute.
    if (!g_trainer) return DefWindowProc(hwnd, message, wParam, lParam);

    if (command == SET_SEED) {
        int32_t seed = std::stoi(GetWindowString(g_seed)); // Load seed from UI
        SetStringText(g_seed, std::to_wstring(seed).c_str()); // Restore parsed value
        g_trainer->SetSeed(seed);
    } else if (command == RANDOM_SEED) {
        g_trainer->RandomizeSeed();
        SetStringText(g_seed, L"(hidden)");
    } else if (command == SHOW_SEED) {
        int32_t seed = g_trainer->GetSeed();
        SetStringText(g_seed, std::to_wstring(seed).c_str());
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

HWND CreateTooltip(HWND target, LPCWSTR hoverText) {
    HWND tooltip = CreateWindow(TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        target, NULL, g_hInstance, NULL);

    // Note that this requires Common Controls 6.0.0.0 to work -- see manifest settings.
    TOOLINFO toolInfo;
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd = g_hwnd;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = (UINT_PTR)target;
    toolInfo.lpszText = const_cast<wchar_t*>(hoverText);
    SendMessage(tooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
    return tooltip;
}

HWND CreateLabel(int x, int y, int width, int height, LPCWSTR text = L"") {
    return CreateWindow(L"STATIC", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, width, height,
        g_hwnd, NULL, g_hInstance, NULL);
}

HWND CreateLabel(int x, int y, int width, LPCWSTR text) {
    return CreateLabel(x, y, width, 16, text);
}

HWND CreateButton(int x, int& y, int width, LPCWSTR text, __int64 message) {
    HWND button = CreateWindow(L"BUTTON", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, width, 26,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 30;
    return button;
}

HWND CreateCheckbox(int x, int& y, __int64 message) {
    HWND checkbox = CreateWindow(L"BUTTON", NULL,
        WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
        x, y + 2, 12, 12,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 20;
    return checkbox;
}

HWND CreateText(int x, int& y, int width, LPCWSTR defaultText = L"", __int64 message = NULL) {
    HWND text = CreateWindow(MSFTEDIT_CLASS, defaultText,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER,
        x, y, width, 26,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 30;
    return text;
}

void CreateComponents() {
    // Column 1
    int x = 10;
    int y = 10;

    CreateLabel(x, y + 5, 100, L"Seed:");
    g_seed = CreateText(x + 40, y, 100, L"(hidden)", SHOW_SEED);
    // PostMessage(g_seed, EM_SETEVENTMASK, 0, ENM_KEYEVENTS);
    
    CreateButton(x, y, 200, L"Set seed", SET_SEED);
    CreateButton(x, y, 200, L"Generate new seed", RANDOM_SEED);

    CreateLabel(x, y, 185, L"Disable Challenge time limit");
    CreateCheckbox(200, y, INFINITE_CHALLENGE);

    CreateLabel(x, y, 185, L"First Song Only");
    CreateCheckbox(200, y, MK_CHALLENGE);

    CreateLabel(x, y, 185, L"Reroll RNG after victory");
    CreateCheckbox(200, y, CHALLENGE_REROLL);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return hr;
    LoadLibrary(L"Msftedit.dll");
    WNDCLASS wndClass = {
        CS_HREDRAW | CS_VREDRAW,
        WndProc,
        0,
        0,
        hInstance,
        NULL,
        NULL, // LoadCursor(nullptr, IDC_ARROW),
        NULL,
        WINDOW_CLASS,
        WINDOW_CLASS,
    };
    RegisterClass(&wndClass);

    RECT rect;
    GetClientRect(GetDesktopWindow(), &rect);
    g_hwnd = CreateWindow(WINDOW_CLASS, PRODUCT_NAME,
        WS_SYSMENU | WS_MINIMIZEBOX,
        rect.right - 550, 200, 500, 500,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    g_hInstance = hInstance;

    CreateComponents();
    DebugUtils::version = VERSION_STR;

    g_witnessProc->StartHeartbeat(g_hwnd, HEARTBEAT);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int) msg.wParam;
}
