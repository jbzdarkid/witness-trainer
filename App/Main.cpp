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
#define RANDOMIZE           0x405
//#define RANDOMIZING         0x406
//#define RANDOMIZE_DONE      0x407

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
// HWND g_noclipSpeed, g_currentPos, g_savedPos, g_fovCurrent, g_sprintSpeed, g_activePanel, g_panelDist, g_panelName, g_panelState, g_panelPicture, g_activateGame;
HWND g_activateGame;
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
            if (g_trainer) {
                auto trainer = g_trainer;
                g_trainer = nullptr; // Reset any modifications
                g_witnessProc = nullptr; // Free any allocated memory
                // Wait to actually quit until all background threads have finished their work.
                while (trainer.use_count() > 1) std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
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
                // TODO: Set limited challenge?
                SetStringText(g_activateGame, L"Switch to game");
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Process was already running, and we just started. Load settings from the game.
                    g_trainer = Trainer::Create(g_witnessProc);
                    if (!g_trainer) break;
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetInfiniteChallenge());
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

    // All commands should execute on a background thread, to avoid hanging the UI.
    std::thread t([trainer = g_trainer, wParam] {
#pragma warning (suppress: 4101)
        void* g_trainer; // Prevent access to the global variable in this scope
        SetCurrentThreadName(L"Command Helper");

        WORD command = LOWORD(wParam);
        if (command == INFINITE_CHALLENGE) {
            bool enabled = !IsDlgButtonChecked(g_hwnd, INFINITE_CHALLENGE);
            CheckDlgButton(g_hwnd, INFINITE_CHALLENGE, enabled);
            if (trainer) trainer->SetInfiniteChallenge(enabled);
        } else if (command == MK_CHALLENGE) {
            bool enabled = !IsDlgButtonChecked(g_hwnd, INFINITE_CHALLENGE);
            CheckDlgButton(g_hwnd, INFINITE_CHALLENGE, enabled);
            // if (trainer) trainer->SetInfiniteChallenge(enabled);
        }

        // All other messages need the trainer to be live in order to execute.
        if (!trainer) return;

        if (command == RANDOMIZE) {
            trainer->Randomize();
        }


        if (command == NOCLIP_SPEED)        trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
        else if (command == FOV_CURRENT)    trainer->SetFov(GetWindowFloat(g_fovCurrent));
        else if (command == SPRINT_SPEED)   trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
        else if (command == SHOW_PANELS)    trainer->ShowMissingPanels();
        else if (command == SHOW_NEARBY)    trainer->ShowNearbyEntities();
        else if (command == EXPORT)         trainer->ExportEntities();
        else if (command == SAVE_POS) {
            g_savedCameraPos = trainer->GetCameraPos();
            g_savedCameraAng = trainer->GetCameraAng();
            SetPosAndAngText(g_savedPos, g_savedCameraPos, g_savedCameraAng);
        } else if (command == LOAD_POS) {
            trainer->SetCameraPos(g_savedCameraPos);
            trainer->SetCameraAng(g_savedCameraAng);

            // The player position is from the feet, not the eyes, so we have to adjust slightly.
            auto playerPos = g_savedCameraPos;
            playerPos[2] -= 1.69f;
            trainer->SetPlayerPos(playerPos);
            SetPosAndAngText(g_currentPos, g_savedCameraPos, g_savedCameraAng);
        }
    });
    t.detach();

    return DefWindowProc(hwnd, message, wParam, lParam);
}

int32_t lastCode = 0;
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Only steal hotkeys when we (or the game) are the active window.
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        if (g_hwnd == GetForegroundWindow() || g_witnessProc->IsForeground()) {
            PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;

            int32_t fullCode = p->vkCode;
            if (GetKeyState(VK_SHIFT) & 0x8000)   fullCode |= MASK_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000) fullCode |= MASK_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000)    fullCode |= MASK_ALT;
            if (GetKeyState(VK_LWIN) & 0x8000)    fullCode |= MASK_WIN;
            if (GetKeyState(VK_RWIN) & 0x8000)    fullCode |= MASK_WIN;
            if (lastCode == fullCode)             fullCode |= MASK_REPEAT;

            auto search = hotkeyCodes.find(fullCode);
            if (search != std::end(hotkeyCodes)) {
                PostMessage(g_hwnd, WM_COMMAND, search->second, NULL);
            }
            lastCode = fullCode;
            return 0;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
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

HWND CreateButton(int x, int& y, int width, LPCWSTR text, __int64 message, LPCWSTR hoverText, int32_t hotkey) {
    auto button = CreateButton(x, y, width, text, message);
    CreateTooltip(button, hoverText);
    hotkeyCodes[hotkey] = message;
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

HWND CreateCheckbox(int x, int& y, __int64 message, LPCWSTR hoverText, int32_t hotkey) {
    auto checkbox = CreateCheckbox(x, y, message);
    CreateTooltip(checkbox, hoverText);
    hotkeyCodes[hotkey] = message;
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

    CreateLabel(x, y, 100, L"Noclip Enabled");
    CreateCheckbox(115, y, NOCLIP_ENABLED, L"Control-N", MASK_CONTROL | 'N');

    CreateLabel(x, y + 4, 100, L"Noclip Speed");
    g_noclipSpeed = CreateText(100, y, 130, L"10", NOCLIP_SPEED);

    CreateLabel(x, y + 4, 100, L"Sprint Speed");
    g_sprintSpeed = CreateText(100, y, 130, L"2", SPRINT_SPEED);

    CreateLabel(x, y + 4, 100, L"Field of View");
    g_fovCurrent = CreateText(100, y, 130, L"50.534012", FOV_CURRENT);

    CreateLabel(x, y, 185, L"Can save the game");
    CreateCheckbox(200, y, CAN_SAVE, L"Shift-Control-S", MASK_SHIFT | MASK_CONTROL | 'S');
    CheckDlgButton(g_hwnd, CAN_SAVE, true);

    CreateLabel(x, y, 185, L"Random Doors Practice");
    CreateCheckbox(200, y, DOORS_PRACTICE);

    CreateLabel(x, y, 185, L"Disable Challenge time limit");
    CreateCheckbox(200, y, INFINITE_CHALLENGE);

    CreateLabel(x, y, 185, L"Open the Console");
    CreateCheckbox(200, y, OPEN_CONSOLE, L"Tilde (~)", MASK_SHIFT | VK_OEM_3);

    CreateButton(x, y, 100, L"Save Position", SAVE_POS, L"Control-P", MASK_CONTROL | 'P');
    g_currentPos = CreateLabel(x + 5, y, 90, 80);
    SetPosAndAngText(g_currentPos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f });

    // Column 1a
    x = 110;
    y -= 30;
    CreateButton(x, y, 100, L"Load Position", LOAD_POS, L"Shift-Control-P", MASK_SHIFT | MASK_CONTROL | 'P');
    g_savedPos = CreateLabel(x + 5, y, 90, 80);
    SetPosAndAngText(g_savedPos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f });

    // Column 2
    x = 270;
    y = 10;

    g_activateGame = CreateButton(x, y, 200, L"Launch game", ACTIVATE_GAME);
    CreateButton(x, y, 200, L"Open save folder", OPEN_SAVES);

    g_activePanel = CreateLabel(x, y, 200, L"No Active Panel");
    y += 20;

    g_panelDist = CreateLabel(x, y, 200, L"");
    y += 20;

    g_panelName = CreateLabel(x, y, 200, L"");
    y += 20;

    g_panelState = CreateLabel(x, y, 200, L"");
    y += 20;

    CreateLabel(x+20, y, 200, L"Lock view to panel");
    CreateCheckbox(x, y, SNAP_TO_PANEL, L"Control-L", MASK_CONTROL | 'L');

    CreateButton(x, y, 200, L"Show unsolved panels", SHOW_PANELS);

    // Hotkey for debug purposes, to get addresses based on a reported callstack
    hotkeyCodes[MASK_CONTROL | MASK_SHIFT | MASK_ALT | VK_OEM_PLUS] = CALLSTACK;

#ifdef _DEBUG
    CreateButton(x, y, 200, L"Show nearby entities", SHOW_NEARBY);
    CreateButton(x, y, 200, L"Export all entities", EXPORT);
#endif
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
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hook);

    CoUninitialize();
    return (int) msg.wParam;
}
