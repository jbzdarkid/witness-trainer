#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"
#include "Hotkeys.h"

#include <unordered_set>

#define HEARTBEAT           0x401
#define SAVE_POS            0x402
#define LOAD_POS            0x403
#define ACTIVATE_GAME       0x412
#define OPEN_SAVES          0x413
#define KEY_RELEASED        0x426
#define OPEN_KEYBINDS       0x427
#define INFINITE_HEALTH     0x428
#define INFINITE_CHARGE     0x429
#define RESPAWN             0x430
#define GOD_MODE            0x431
#define SHOW_COLLISION      0x432

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
std::shared_ptr<Memory> g_hobProc;
HWND g_currentPos, g_savedPos, g_activateGame, g_levelName;

std::vector<float> g_savedPlayerPos = {0.0f, 0.0f, 0.0f};
std::vector<float> g_savedPlayerAngle = {0.0f, 0.0f, 0.0f, 0.0f};

#define SetWindowTextA(...) static_assert(false, "Call SetStringText instead of SetWindowTextA");
#define SetWindowTextW(...) static_assert(false, "Call SetStringText instead of SetWindowTextW");
#undef SetWindowText
#define SetWindowText(...) static_assert(false, "Call SetStringText instead of SetWindowText");

void SetStringText(HWND hwnd, const std::string& text) {
    static std::unordered_map<HWND, std::string> hwndText;
    auto search = hwndText.find(hwnd);
    if (search != hwndText.end()) {
        if (search->second == text) return;
        search->second = text;
    } else {
        hwndText[hwnd] = text;
    }

#pragma push_macro("SetWindowTextA")
#undef SetWindowTextA
    SetWindowTextA(hwnd, text.c_str());
#pragma pop_macro("SetWindowTextA")
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

#pragma push_macro("SetWindowTextW")
#undef SetWindowTextW
    SetWindowTextW(hwnd, text.c_str());
#pragma pop_macro("SetWindowTextW")
}

void SetPosText(HWND hwnd, const std::vector<float>& pos, const std::vector<float>& angle) {
    assert(pos.size() == 3, "[Internal error] Attempted to set position of <> 3 elements");
    assert(angle.size() == 4, "[Internal error] Attempted to set position of <> 4 elements");
    std::wstring text(128, '\0');
    // Do some math to convert the angle into a polar coordinate (0-360 degrees).
    // I don't know why this is correct, this isn't how quat math works usually.
    double x = angle[3], y = angle[1];
    double degrees = atan(y / x) * 360.0 / 3.14159 + 90.0;
    swprintf_s(text.data(), text.size(), L"X %.3f\nY %.3f\nZ %.3f\n\u0398 %.3f", pos[0], pos[1], pos[2], degrees);
    SetStringText(hwnd, text);
}

void SetFloatText(HWND hwnd, float f) {
    std::wstring text(15, '\0');
    int size = swprintf_s(text.data(), text.size() + 1, L"%.8g", f);
    text.resize(size);
    SetStringText(hwnd, text);
}

std::wstring GetWindowString(HWND hwnd) {
    SetLastError(0); // GetWindowTextLength does not clear LastError.
    int length = GetWindowTextLengthW(hwnd);
    std::wstring text(length, L'\0');
    length = GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size() + 1)); // Length includes the null terminator
    text.resize(length);
    return text;
}

float GetWindowFloat(HWND hwnd) {
    return wcstof(GetWindowString(hwnd).c_str(), nullptr);
}

// https://stackoverflow.com/a/12662950
void ToggleOption(int message, void (Trainer::*setter)(bool)) {
    bool enabled = IsDlgButtonChecked(g_hwnd, message);
    CheckDlgButton(g_hwnd, message, !enabled);
    // Note that this allows options to be toggled even when the trainer (i.e. game) isn't running.
    if (g_trainer) (*g_trainer.*setter)(!enabled);
}

void LaunchSteamGame(int gameId) {
    // Steam does not like launching games with arguments. Just accept it.
    std::string steamUrl = "steam://rungameid/" + std::to_string(gameId);
    ShellExecuteA(g_hwnd, "open", steamUrl.c_str(), NULL, NULL, SW_SHOWDEFAULT);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            if (g_trainer) {
                auto trainer = g_trainer;
                g_trainer = nullptr; // Close the trainer, which undoes any modifications to the game.

                // Wait to actually quit until all background threads have finished their work.
                // Note that we do need to pump messages here, since said work may require the message pump,
                // which we are currently holding hostage.
                while (trainer.use_count() > 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    MSG msg;
                    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND: // ???
        {
            RECT rc;
            ::GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(255,255,255));
            FillRect((HDC)wParam, &rc, brush);
            DeleteObject(brush);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            // Get rid of the gross gray background. https://stackoverflow.com/a/4495814
            SetTextColor((HDC)wParam, RGB(0, 0, 0));
            SetBkColor((HDC)wParam, RGB(255, 255, 255));
            SetBkMode((HDC)wParam, OPAQUE); // ???
            static HBRUSH s_solidBrush = CreateSolidBrush(RGB(255, 255, 255));
            return (LRESULT)s_solidBrush;
        case HEARTBEAT:
            switch ((ProcStatus)wParam) {
            case ProcStatus::Stopped:
                // Don't discard any settings, just free the trainer.
                g_trainer = nullptr;
                // Also reset the title & launch text, since they can get stuck
                SetStringText(g_hwnd, WINDOW_TITLE);
                SetStringText(g_activateGame, L"Launch game");
                break;
            case ProcStatus::Started:
                SetStringText(g_hwnd, L"Attaching to HOB...");
                g_trainer = Trainer::Create(g_hobProc);
                [[fallthrough]];
            case ProcStatus::Reload:
                if (!g_trainer) break; // It's possible that trainer failed to attach before a new game was started.
                // Process just started (or reloaded), enforce our settings.
                SetStringText(g_hwnd, WINDOW_TITLE);
                if (IsDlgButtonChecked(hwnd, INFINITE_HEALTH))  g_trainer->SetMaxHealth(100);
                if (IsDlgButtonChecked(hwnd, INFINITE_CHARGE))  g_trainer->SetMaxCharge(100);
                if (IsDlgButtonChecked(hwnd, GOD_MODE))         g_trainer->SetGodMode(true);
                if (IsDlgButtonChecked(hwnd, SHOW_COLLISION))   g_trainer->SetShowCollision(true);
                SetStringText(g_activateGame, L"Switch to game");
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Process was already running, and we haven't attached yet. Try to attach, then load settings from the game once we do.
                    SetStringText(g_hwnd, L"Attaching to HOB...");
                    g_trainer = Trainer::Create(g_hobProc);
                    if (!g_trainer) break;
                    SetStringText(g_hwnd, WINDOW_TITLE);
                    CheckDlgButton(hwnd, INFINITE_HEALTH, g_trainer->GetHealth() == 100);
                    CheckDlgButton(hwnd, INFINITE_CHARGE, g_trainer->GetCharge() == 100);
                    CheckDlgButton(hwnd, GOD_MODE, g_trainer->GetGodMode());
                    CheckDlgButton(hwnd, SHOW_COLLISION, g_trainer->GetShowCollision());
                    SetStringText(g_activateGame, L"Switch to game");
                }

                // Settings which are always sourced from the game, since they are not editable in the trainer.
                // For performance reasons (redrawing text is expensive), these update 10x slower than other display fields.
                static int64_t update = 0;
                if (++update % 10 == 0) {
                    SetPosText(g_currentPos, g_trainer->GetPlayerPos(), g_trainer->GetPlayerAngle());
                    if (IsDlgButtonChecked(g_hwnd, INFINITE_HEALTH) == TRUE) {
                        // Note: We still need to allow the player to die to instakill effects like fall damage.
                        // Otherwise, Hob just gets stuck in the "dying" state.
                        // (Don't bother setting HP if it's already full, to avoid churn)
                        int currentHealth = g_trainer->GetHealth();
                        if (currentHealth != 0 && currentHealth != 100) g_trainer->SetHealth(100);
                    }
                    if (IsDlgButtonChecked(g_hwnd, INFINITE_CHARGE) == TRUE) {
                        g_trainer->SetCharge(100);
                    }

                    std::string levelName = g_trainer->GetLevelName();
                    levelName = levelName.substr(13); // Trim leading MEDIA/LEVELS/ (common to all levels)
                    SetStringText(g_levelName, levelName);
                }
                break;
            }
            return 0;
        case WM_COMMAND:
            break; // LOWORD(wParam) contains the actual command, handled below
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    // All commands should execute on a background thread, to avoid hanging the UI.
    std::thread t([trainer = g_trainer, wParam] {
#pragma warning (suppress: 4101)
        void* g_trainer; // Prevent access to the global variable in this scope
        SetCurrentThreadName(L"Command Helper");

        WORD command = LOWORD(wParam);
        if (command == ACTIVATE_GAME) {
            if (!trainer) LaunchSteamGame(404680);
            else g_hobProc->BringToFront();
        } else if (command == OPEN_SAVES) {
            const wchar_t* savesFolder = LR"(C:\Users\localhost\Documents\my games\runic games\hob\saves)";
            ShellExecute(NULL, L"open", savesFolder, NULL, NULL, SW_SHOWDEFAULT);
        } else if (command == OPEN_KEYBINDS) {
            std::wstring hotkeyFile = Hotkeys::Get()->GetHotkeyFilePath();
            ShellExecute(NULL, L"open", hotkeyFile.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        } else if (!trainer && HIWORD(wParam) == 0) { // Message was triggered by the user
            MessageBox(g_hwnd, L"The process must be running in order to use this button", L"", MB_OK);
        }

        // All other messages need the trainer to be live in order to execute.
        if (!trainer) return;

        if (command == SAVE_POS) {
            g_savedPlayerPos = trainer->GetPlayerPos();
            g_savedPlayerAngle = trainer->GetPlayerAngle();
            SetPosText(g_savedPos, g_savedPlayerPos, g_savedPlayerAngle);
        } else if (command == LOAD_POS) {
            if (g_savedPlayerPos[0] != 0.0f || g_savedPlayerPos[1] != 0.0f || g_savedPlayerPos[2] != 0.0f) { // Prevent TP to origin (i.e. if the user hasn't set a position yet)
                trainer->SetPlayerPos(g_savedPlayerPos);
                trainer->SetPlayerAngle(g_savedPlayerAngle);
                SetPosText(g_currentPos, g_savedPlayerPos, g_savedPlayerAngle);
            }
        } else if (command == INFINITE_HEALTH) {
            if (IsDlgButtonChecked(g_hwnd, INFINITE_HEALTH)) {
                trainer->SetMaxHealth(3);
                if (trainer->GetHealth() > 0) trainer->SetHealth(3);
                CheckDlgButton(g_hwnd, INFINITE_HEALTH, false);
            } else {
                trainer->SetMaxHealth(100);
                if (trainer->GetHealth() > 0) trainer->SetHealth(100);
                CheckDlgButton(g_hwnd, INFINITE_HEALTH, true);
            }
        } else if (command == INFINITE_CHARGE) {
            if (IsDlgButtonChecked(g_hwnd, INFINITE_CHARGE)) {
                trainer->SetMaxCharge(1);
                trainer->SetCharge(1);
                CheckDlgButton(g_hwnd, INFINITE_CHARGE, false);
            } else {
                trainer->SetMaxCharge(100);
                trainer->SetCharge(100);
                CheckDlgButton(g_hwnd, INFINITE_CHARGE, true);
            }
        } else if (command == RESPAWN) {
            trainer->SetHealth(0);
        } else if (command == GOD_MODE) {
            ToggleOption(GOD_MODE, &Trainer::SetGodMode);
        } else if (command == SHOW_COLLISION) {
            ToggleOption(SHOW_COLLISION, &Trainer::SetShowCollision);
        }
    });
    t.detach();

    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        // Only steal hotkeys when we (or the game) are the active window.
        auto foreground = GetForegroundWindow();
        if (g_hwnd == foreground || g_hobProc->IsForeground()) {
            int64_t found = Hotkeys::Get()->CheckMatchingHotkey(wParam, lParam);
            if (found) {
                PostMessage(g_hwnd, WM_COMMAND, found, NULL);

                // If this command is a keydown, do not send it to the game in case it overlaps with the user's keybinds.
                if (found != KEY_RELEASED) return -1;
            }
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

HWND CreateLabel(int x, int y, int width, int height, LPCWSTR text = L"", __int64 message = 0) {
    return CreateWindow(L"STATIC", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOTIFY,
        x, y, width, height,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
}

HWND CreateLabel(int x, int y, int width, LPCWSTR text, __int64 message = 0) {
    return CreateLabel(x, y, width, 16, text, message);
}

HWND CreateButton(int x, int& y, int width, LPCWSTR text, __int64 message, LPCSTR hotkeyName) {
    HWND button = CreateWindow(L"BUTTON", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, width, 26,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 30;

    Hotkeys::Get()->RegisterHotkey(hotkeyName, message);
    std::wstring hoverText = Hotkeys::Get()->GetHoverText(hotkeyName);
    CreateTooltip(button, hoverText.c_str());

    return button;
}

HWND CreateCheckbox(int x, int& y, __int64 message, const std::wstring& hoverText) {
    HWND checkbox = CreateWindow(L"BUTTON", NULL,
        WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
        x, y + 2, 12, 12,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 20;
    CreateTooltip(checkbox, hoverText.c_str());
    return checkbox;
}

// The same arguments as Button.
std::pair<HWND, HWND> CreateLabelAndCheckbox(int x, int& y, int width, LPCWSTR text, __int64 message, LPCSTR hotkeyName) {
    // We need a distinct message (HMENU) for the label so that when we call CheckDlgButton it targets the checkbox, not the label.
    // However, we only use the low word (bottom 2 bytes) for logic, so we can safely modify the high word to make it distinct.
    auto label = CreateLabel(x + 20, y, width, text, message + 0x10000);

    Hotkeys::Get()->RegisterHotkey(hotkeyName, message);
    std::wstring hoverText = Hotkeys::Get()->GetHoverText(hotkeyName);
    CreateTooltip(label, hoverText.c_str());

    auto checkbox = CreateCheckbox(x, y, message, hoverText);
    return {label, checkbox};
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

    CreateLabelAndCheckbox(x, y, 100, L"God Mode", GOD_MODE, "god_mode");

    CreateLabelAndCheckbox(x, y, 250, L"Show Collision (requires reload)", SHOW_COLLISION, "show_collision");

    CreateLabelAndCheckbox(x, y, 100, L"Infinite Health", INFINITE_HEALTH, "infinite_health");

    CreateLabelAndCheckbox(x, y, 100, L"Infinite Charge", INFINITE_CHARGE, "infinite_charge");

    CreateButton(x, y, 100, L"Respawn", RESPAWN, "respawn");

    CreateButton(x, y, 110, L"Save Position", SAVE_POS, "save_position");
    y -= 30;
    CreateButton(x + 120, y, 110, L"Load Position", LOAD_POS, "load_position");
    g_currentPos = CreateLabel(x + 5,   y, 110, 80);
    g_savedPos   = CreateLabel(x + 125, y, 110, 80);
    SetPosText(g_currentPos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f });
    SetPosText(g_savedPos,   { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f });
    y += 90;

    CreateLabel(x, y, 200, L"Level name:");
    y += 20;
    g_levelName = CreateLabel(x, y, 1000, L"");

    // Column 2
    x = 270;
    y = 10;

    g_activateGame = CreateButton(x, y, 200, L"Launch game", ACTIVATE_GAME, "launch_game");
    CreateButton(x, y, 200, L"Open save folder", OPEN_SAVES, "open_save_folder");
    CreateButton(x, y, 200, L"Open keybinds file", OPEN_KEYBINDS, "open_keybinds");

    // Required to 'unselect' any hold-based keybinds
    Hotkeys::Get()->RegisterHotkey("key_released", KEY_RELEASED);
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
        NULL,
        NULL,
        WINDOW_CLASS,
        WINDOW_CLASS,
    };
    RegisterClass(&wndClass);

    RECT rect;
    GetClientRect(GetDesktopWindow(), &rect);
    g_hwnd = CreateWindow(WINDOW_CLASS, WINDOW_TITLE,
        WS_SYSMENU | WS_MINIMIZEBOX,
        rect.right - 550, 200, 500, 500,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    g_hInstance = hInstance;

    DebugUtils::version = VERSION_STR;
    CreateComponents();
    Hotkeys::Get()->SanityCheckHotkeys();

    g_hobProc = std::make_shared<Memory>(L"HOB.exe");
    g_hobProc->StartHeartbeat(g_hwnd, HEARTBEAT);
    HHOOK hook = NULL;
#ifndef _DEBUG
    // Don't hook in debug mode. While debugging, we are paused (and thus cannot run the hook). So, we will timeout on every hook call!
    hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, hInstance, NULL);
#endif

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hook) UnhookWindowsHookEx(hook);
    g_hobProc->StopHeartbeat();
    g_hobProc = nullptr;

    CoUninitialize();
    return (int) msg.wParam;
}
