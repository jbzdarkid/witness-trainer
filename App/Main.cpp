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
#define SHOW_SEED           0x0000000001000408
#define TELE_TO_CHALLENGE   0x409
#define SEED_HIDDEN         L"(click to show)"

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
HWND g_activateGame, g_seed, g_eventLog;
std::shared_ptr<Memory> g_witnessProc;
ChallengeState g_challengeState = ChallengeState::Off;

void SetWindowString(HWND hwnd, const std::string& text) {
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

void SetWindowString(HWND hwnd, const std::wstring& text) {
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

void AddEvent(const std::wstring& event) {
    std::wstring text = GetWindowString(g_eventLog);
    text = event + L'\n' + text;
    SetWindowString(g_eventLog, text);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            // Since the challenge derandomization isn't reversible anyways, don't worry about cleaning up the trainer/etc on close.
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
            break; // LOWORD(wParam) contains the command
		    case WM_ERASEBKGND:
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
        case HEARTBEAT:
            switch ((ProcStatus)wParam) {
            case ProcStatus::Stopped:
            case ProcStatus::NotRunning:
                // Don't discard any settings, just free the trainer.
                if (g_trainer) g_trainer = nullptr;
                SetStringText(g_hwnd, L"Witness Challenge Randomizer");
                SetWindowString(g_activateGame, L"Launch game");
                break;
            case ProcStatus::Reload:
            case ProcStatus::NewGame:
            case ProcStatus::Started:
                if (!g_trainer) {
                    // Process just started (we were already alive), enforce our settings.
                    SetStringText(g_hwnd, L"Attaching to The Witness...");
                    g_trainer = Trainer::Create(g_witnessProc);
                }
                if (!g_trainer) break;
                SetStringText(g_hwnd, L"Witness Challenge Randomizer");
                // Or, we started a new game / loaded a save, in which case some of the entity data might have been reset.
                g_trainer->SetInfiniteChallenge(IsDlgButtonChecked(hwnd, INFINITE_CHALLENGE));
                g_trainer->SetMkChallenge(IsDlgButtonChecked(hwnd, MK_CHALLENGE));
                PostMessage(hwnd, WM_COMMAND, SET_SEED, 0); // Set seed from Randomizer -> Game
                SetWindowString(g_activateGame, L"Switch to game");
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Process was already running, and we just started. Load settings from the game.
                    SetStringText(g_hwnd, L"Attaching to The Witness...");
                    g_trainer = Trainer::Create(g_witnessProc);
                    if (!g_trainer) break;
                    SetStringText(g_hwnd, L"Witness Challenge Randomizer");
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetInfiniteChallenge());
                    CheckDlgButton(hwnd, MK_CHALLENGE, g_trainer->GetMkChallenge());
                    PostMessage(hwnd, WM_COMMAND, SHOW_SEED, 0); // Load seed from Game -> Randomizer
                    SetWindowString(g_activateGame, L"Switch to game");
                    g_trainer->SetMainMenuState(true); // TODO: I know I turned this into an "always forced on" but it's smarter to just always run it during the enable block. Fix this.
                } else {
                    // Process was already running, and so were we (this recurs every heartbeat). Enforce settings and apply repeated actions.
                    ChallengeState newState = g_trainer->GetChallengeState();
                    if (g_challengeState != ChallengeState::Finished && newState == ChallengeState::Finished) {
                        int32_t seconds = static_cast<int>(g_trainer->GetChallengeTimer());
                        int32_t minutes = seconds / 60;
                        seconds -= minutes * 60;
                        std::wstring buffer(128, L'\0');
                        swprintf(&buffer[0], buffer.size(), L"Completed seed %u in %02d:%02d", g_trainer->GetSeed(), minutes, seconds);
                        AddEvent(buffer);
                        if (IsDlgButtonChecked(hwnd, CHALLENGE_REROLL)) {
                            bool seedWasHidden = (GetWindowString(g_seed) == SEED_HIDDEN);
                            PostMessage(hwnd, WM_COMMAND, RANDOM_SEED, 0);
                            if (!seedWasHidden) PostMessage(hwnd, WM_COMMAND, SHOW_SEED, 0);
                        }
                    } else if (g_challengeState != ChallengeState::Started && newState == ChallengeState::Started) {
                        // TODO: The challenge timer keeps running! So how do I know if it's a new challenge...?
                        // Probably I should look at the sound thing I found.
                        if (GetWindowString(g_seed) == SEED_HIDDEN) {
                            // AddEvent(L"Started challenge with a hidden seed");
                        } else {
                            // AddEvent(L"Started challenge with seed " + GetWindowString(g_seed));
                        }
                    }
                    g_challengeState = newState;
                }

                break;
            }
            return 0;
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    WORD command = LOWORD(wParam);
    if (command == ACTIVATE_GAME) {
        if (!g_trainer) ShellExecute(NULL, L"open", L"steam://rungameid/210970", NULL, NULL, SW_SHOWDEFAULT);
        else g_witnessProc->BringToFront();
    } else if (command == INFINITE_CHALLENGE) {
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
    } else if (!g_trainer && HIWORD(wParam) == 0) { // Message was triggered by the user
        MessageBox(g_hwnd, L"The Witness must be running in order to use this button", L"", MB_OK);
    }

    // All other messages need the trainer to be live in order to execute.
    if (!g_trainer) return DefWindowProc(hwnd, message, wParam, lParam);

    if (command == SET_SEED) {
        uint32_t seed = wcstoul(GetWindowString(g_seed).c_str(), nullptr, 10); // Load seed from UI
        SetWindowString(g_seed, std::to_wstring(seed).c_str()); // Restore parsed value
        g_trainer->SetSeed(seed);
    } else if (command == RANDOM_SEED) {
        g_trainer->RandomizeSeed();
        SetWindowString(g_seed, SEED_HIDDEN);
    } else if (wParam == SHOW_SEED) { // Using wParam instead of command because we need to check the high bits.
        uint32_t seed = g_trainer->GetSeed();
        SetWindowString(g_seed, std::to_wstring(seed).c_str());
    } else if (command == TELE_TO_CHALLENGE) {
        g_trainer->SetPlayerPos({-39.0f, -31.4f, -11.7f});
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

HWND CreateCheckbox(int x, int& y, __int64 message) {
    HWND checkbox = CreateWindow(L"BUTTON", NULL,
        WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
        x, y + 2, 12, 12,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 20;
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

    CreateButton(x, y, 200, L"Teleport to Challenge", TELE_TO_CHALLENGE);

    CreateLabel(x, y + 5, 100, L"Seed:");
    uint32_t seed = 0x8664f205 * static_cast<uint32_t>(time(nullptr)) + 5; // Seeded from time & randomized once.
    g_seed = CreateText(x + 40, y, 160, std::to_wstring(seed).c_str(), SHOW_SEED);

    CreateButton(x, y, 200, L"Set seed", SET_SEED);
    CreateButton(x, y, 200, L"Generate new seed", RANDOM_SEED);

    CreateLabelAndCheckbox(x, y, 185, L"Disable time limit", INFINITE_CHALLENGE);

    CreateLabelAndCheckbox(x, y, 185, L"First Song Only", MK_CHALLENGE);

    CreateLabelAndCheckbox(x, y, 185, L"Reroll RNG after victory", CHALLENGE_REROLL);

    g_eventLog = CreateLabel(x, y, 200, 300);
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
        rect.right - 550, 200, 240, 500,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    g_hInstance = hInstance;

    CreateComponents();
    DebugUtils::version = VERSION_STR;

    g_witnessProc = std::make_shared<Memory>(L"witness64_d3d11.exe");
    g_witnessProc->StartHeartbeat(g_hwnd, HEARTBEAT);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    // Since the challenge derandomization isn't reversible anyways, don't worry about cleaning up the trainer/etc on close.

    CoUninitialize();
    return (int)msg.wParam;
}
