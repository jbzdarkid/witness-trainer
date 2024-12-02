#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"
#include "Utils.h"

#include <unordered_set>

#define ACTIVATE_GAME       0x401
#define CALLSTACK           0x402
#define READWRITE           0x403
#define READ_BUFFER         0x404

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
std::shared_ptr<Memory> g_witnessProc;
HWND g_activateGame, g_readWrite;

// https://stackoverflow.com/a/12662950
void ToggleOption(int message, void (Trainer::*setter)(bool)) {
    bool enabled = IsDlgButtonChecked(g_hwnd, message);
    CheckDlgButton(g_hwnd, message, !enabled);
    // Note that this allows options to be toggled even when the trainer (i.e. game) isn't running.
    if (g_trainer) (*g_trainer.*setter)(!enabled);
}

void LaunchSteamGame(const char* gameId, const char* arguments = "") {
    std::string steamUrl = "steam://rungameid/";
    steamUrl += gameId;
    ShellExecuteA(g_hwnd, "open", steamUrl.c_str(), NULL, NULL, SW_SHOWDEFAULT);

    /* The above doesn't really work with arguments, so in the future we should do this:
    auto key = REG_QUERY("Computer\HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam")
    char* steamPath = REG_KEY_READ(key, "InstallPath");

    std::string fullArguments = "-applaunch " + gameId + " " + arguments;
    ShellExecuteW(g_hwnd, L"open", steamPath, fullArguments.c_str(), NULL, SW_SHOWDEFAULT);
    */
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
		case WM_ERASEBKGND: // Seems to fix the latent background image issues
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
            SetBkMode((HDC)wParam, OPAQUE);
            static HBRUSH s_solidBrush = CreateSolidBrush(RGB(255, 255, 255));
            return (LRESULT)s_solidBrush;
        case WM_COMMAND: // All commands should execute on a background thread, to avoid hanging the UI.
            std::thread([trainer = g_trainer, wParam] {
                void* g_trainer = nullptr; // Prevent access to the global variable in this scope, all access should come through the trainer reference.
                SetCurrentThreadName(L"Command Helper");

                switch (LOWORD(wParam)) {
                case CALLSTACK:
                    DebugUtils::RegenerateCallstack(L"something"); // GetWindowString()
                    break;
                case ACTIVATE_GAME:
                    if (!trainer) LaunchSteamGame("238460");
                    else g_witnessProc->BringToFront();
                    break;
                case READWRITE:
                    ToggleOption(READWRITE, &Trainer::SetWrite);
                    break;
                case READ_BUFFER:
                    trainer->GetBuffer();
                    break;
                }
            }).detach();
            break;
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

HWND CreateLabel(int x, int y, int width, int height, LPCWSTR text = L"", __int64 message = 0) {
    return CreateWindow(L"STATIC", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOTIFY,
        x, y, width, height,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
}

HWND CreateLabel(int x, int y, int width, LPCWSTR text, __int64 message = 0) {
    return CreateLabel(x, y, width, 16, text, message);
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
    return checkbox;
}

// The same arguments as Button.
std::pair<HWND, HWND> CreateLabelAndCheckbox(int x, int& y, int width, LPCWSTR text, __int64 message, LPCWSTR hoverText, int32_t hotkey) {
    // We need a distinct message (HMENU) for the label so that when we call CheckDlgButton it targets the checkbox, not the label.
    // However, we only use the low word (bottom 2 bytes) for logic, so we can safely modify the high word to make it distinct.
    auto label = CreateLabel(x + 20, y, width, text, message + 0x10000);
    CreateTooltip(label, hoverText);
    auto checkbox = CreateCheckbox(x, y, message, hoverText, hotkey);
    return {label, checkbox};
}

// Also the same arguments as Button.
std::pair<HWND, HWND> CreateLabelAndCheckbox(int x, int& y, int width, LPCWSTR text, __int64 message) {
    return CreateLabelAndCheckbox(x, y, width, text, message, L"", 0);
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

    g_activateGame = CreateButton(x, y, 200, L"Launch game", ACTIVATE_GAME);
    auto [_, g_readWrite] = CreateLabelAndCheckbox(x, y, 100, L"Read/Write", READWRITE);

    CreateButton(x, y, 100, L"Read buffer", READ_BUFFER);
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
    g_hwnd = CreateWindow(WINDOW_CLASS, PRODUCT_NAME,
        WS_SYSMENU | WS_MINIMIZEBOX,
        rect.right - 550, 200, 500, 500,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    g_hInstance = hInstance;

    CreateComponents();
    DebugUtils::version = VERSION_STR;

    std::thread([] {
        SetCurrentThreadName(L"Heartbeat");
        SetStringText(g_hwnd, L"Attaching to Battle Block Theater...");
        while (!g_witnessProc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            g_witnessProc = Memory::Create(L"BattleBlockTheater.exe");
        }
        SetStringText(g_activateGame, L"Switch to game");

        while (!g_trainer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            g_trainer = Trainer::Create(g_witnessProc);
        }
        SetStringText(g_hwnd, PRODUCT_NAME);
    }).detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_witnessProc = nullptr;

    CoUninitialize();
    return (int) msg.wParam;
}
