#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"

#define HEARTBEAT 0x401
#define SAVE_POS 0x402
#define LOAD_POS 0x403
#define NOCLIP_SPEED 0x404
#define SPRINT_SPEED 0x405
#define FOV_CURRENT 0x406
#define NOCLIP_ENABLED 0x407
#define CAN_SAVE 0x408
#define DOORS_PRACTICE 0x409
#define INFINITE_CHALLENGE 0x410
#define OPEN_CONSOLE 0x411
#define ACTIVATE_GAME 0x412
#define OPEN_SAVES 0x413
#define SHOW_PANELS 0x414
#define SHOW_NEARBY 0x415
#define EXPORT 0x416
#define START_TIMER 0x417
#define CALLSTACK 0x418

// Feature requests:
// - show collision, somehow
// - Change current save name: Overwrite get_campaign_string_of_current_time
//  Nope, I mean the save name in-game.
// - "Save the game" button on the trainer?
// - "Load last save" button on the trainer?
// - Icon for trainer
//  https://stackoverflow.com/questions/40933304
// - Delete all saves (?)
// - Basic timer
// - Add "distance to panel" in the panel info. Might be fun to see *how far* some of the snipes are.
// - Starting a new game isn't triggering "load game", which means offsets are stale.
//  Once done, figure out what needs to be changed to properly reset "panel data".
// - Save settings to some file, and reload them on trainer start

// Bad/Hard ideas:
// - Avoid hanging the UI during load; call Trainer::ctor on a background thread.
// - Show currently traced line (hard, requires changes in Memory)
// - Improvement for 'while noclip is on', solve mode doesn't reset position (?)
// - _timing asl to the trainer? Just something simple would be good enough, mostly
// - LOD hack

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::unique_ptr<Trainer> g_trainer;
HWND g_noclipSpeed, g_currentPos, g_savedPos, g_fovCurrent, g_sprintSpeed, g_activePanel, g_panelName, g_panelState, g_panelPicture, g_activateGame;
auto g_witnessProc = std::make_shared<Memory>(L"witness64_d3d11.exe");

std::vector<float> g_savedCameraPos = {0.0f, 0.0f, 0.0f};
std::vector<float> g_savedCameraAng = {0.0f, 0.0f};
int previousPanel = -1;

void SetPosAndAngText(const std::vector<float>& pos, const std::vector<float>& ang, HWND hwnd)
{
    assert(pos.size() == 3);
    assert(ang.size() == 2);
    std::wstring text(65, '\0');
    swprintf_s(text.data(), text.size() + 1, L"X %8.3f\nY %8.3f\nZ %8.3f\n\u0398 %8.5f\n\u03A6 %8.5f", pos[0], pos[1], pos[2], ang[0], ang[1]);
    SetWindowText(hwnd, text.c_str());
}

void SetFloatText(float f, HWND hwnd) {
    std::wstring text(10, '\0');
    int size = swprintf_s(text.data(), text.size() + 1, L"%.8g", f);
    text.resize(size);
    SetWindowText(hwnd, text.c_str());
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

void SetActivePanel(int activePanel) {
    if (activePanel != -1) previousPanel = activePanel;

    std::wstringstream ss;
    if (activePanel != -1) {
        ss << L"Active Panel:";
    } else if (previousPanel != -1) {
        ss << L"Previous Panel:";
    } else {
        ss << L"No Active Panel";
    }
    if (previousPanel != -1) {
        ss << L" 0x" << std::hex << std::setfill(L'0') << std::setw(5) << previousPanel;
    }
    SetWindowText(g_activePanel, ss.str().c_str());

    if (previousPanel != -1) {
        if (g_trainer) {
            std::shared_ptr<Trainer::EntityData> panelData = g_trainer->GetEntityData(previousPanel);
            if (!panelData) return;
            SetWindowTextA(g_panelName, panelData->name.c_str());
            SetWindowTextA(g_panelState, panelData->state.c_str());
            // TODO(Future): draw path with GDI
        }
    }
}

// https://stackoverflow.com/a/12662950
void ToggleOption(int message, void (Trainer::*setter)(bool)) {
    bool enabled = IsDlgButtonChecked(g_hwnd, message);
    CheckDlgButton(g_hwnd, message, !enabled);
    // Note that this allows options to be toggled even when the trainer (i.e. game) isn't running.
    if (g_trainer) (*g_trainer.*setter)(!enabled);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            if (g_trainer) {
                g_trainer = nullptr; // Reset any modifications
                g_witnessProc = nullptr; // Free any allocated memory
            }
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
            break; // LOWORD(wParam) contains the command
        case WM_CTLCOLORSTATIC:
            // Get rid of the gross gray background. https://stackoverflow.com/a/4495814
            SetBkColor((HDC)wParam, RGB(255, 255, 255));
            return 0;
        case WM_HOTKEY:
            break; // LOWORD(wParam) contains the command
        case HEARTBEAT:
            switch ((ProcStatus)wParam) {
            case ProcStatus::Stopped:
            case ProcStatus::NotRunning:
                // Don't discard any settings, just free the trainer.
                if (g_trainer) {
                    g_trainer = nullptr;
                    SetWindowText(g_activateGame, L"Launch game");
                }
                break;
            case ProcStatus::Reload:
            case ProcStatus::NewGame:
                SetActivePanel(-1);
                previousPanel = -1;
                break;
            case ProcStatus::Started:
                assert(!g_trainer);
                g_trainer = Trainer::Create(g_witnessProc);
                if (!g_trainer) break;
                // Process just started (we were already alive), enforce our settings.
                g_trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
                g_trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
                g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
                g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));
                g_trainer->SetCanSave(IsDlgButtonChecked(hwnd, CAN_SAVE));
                g_trainer->SetRandomDoorsPractice(IsDlgButtonChecked(hwnd, DOORS_PRACTICE));
                g_trainer->SetInfiniteChallenge(IsDlgButtonChecked(hwnd, INFINITE_CHALLENGE));
                g_trainer->SetConsoleOpen(IsDlgButtonChecked(hwnd, OPEN_CONSOLE));
                SetWindowText(g_activateGame, L"Switch to game");
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Process was already running, and we just started. Load settings from the game.
                    g_trainer = Trainer::Create(g_witnessProc);
                    if (!g_trainer) break;
                    SetFloatText(g_trainer->GetNoclipSpeed(), g_noclipSpeed);
                    SetFloatText(g_trainer->GetSprintSpeed(), g_sprintSpeed);
                    SetFloatText(g_trainer->GetFov(), g_fovCurrent);
                    CheckDlgButton(hwnd, NOCLIP_ENABLED, g_trainer->GetNoclip());
                    CheckDlgButton(hwnd, CAN_SAVE, g_trainer->CanSave());
                    CheckDlgButton(hwnd, DOORS_PRACTICE, g_trainer->GetRandomDoorsPractice());
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetInfiniteChallenge());
                    CheckDlgButton(hwnd, OPEN_CONSOLE, g_trainer->GetConsoleOpen());
                    SetWindowText(g_activateGame, L"Switch to game");
                } else {
                    // Process was already running, and so were we (this recurs every heartbeat). Enforce settings.
                    g_trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
                    g_trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
                    g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));
                    g_trainer->SetCanSave(IsDlgButtonChecked(hwnd, CAN_SAVE));
                    g_trainer->SetRandomDoorsPractice(IsDlgButtonChecked(hwnd, DOORS_PRACTICE));
                    g_trainer->SetInfiniteChallenge(IsDlgButtonChecked(hwnd, INFINITE_CHALLENGE));
                    g_trainer->SetConsoleOpen(IsDlgButtonChecked(hwnd, OPEN_CONSOLE));

                    // If we are the active window, set FOV. Otherwise, read FOV.
                    if (g_hwnd == GetActiveWindow()) {
                        g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
                    } else {
                        SetFloatText(g_trainer->GetFov(), g_fovCurrent);
                    }
                }

                // Some settings are always sourced from the game, since they are not editable in the trainer.
                SetPosAndAngText(g_trainer->GetCameraPos(), g_trainer->GetCameraAng(), g_currentPos);
                SetActivePanel(g_trainer->GetActivePanel());
                break;
            }
            return 0;
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    WORD command = LOWORD(wParam);
    if (command == CAN_SAVE)           ToggleOption(CAN_SAVE, &Trainer::SetCanSave);
    else if (command == INFINITE_CHALLENGE) ToggleOption(INFINITE_CHALLENGE, &Trainer::SetInfiniteChallenge);
    else if (command == DOORS_PRACTICE)     ToggleOption(DOORS_PRACTICE, &Trainer::SetRandomDoorsPractice);
    else if (command == OPEN_CONSOLE)       ToggleOption(OPEN_CONSOLE, &Trainer::SetConsoleOpen);
    else if (command == CALLSTACK)          DebugUtils::RegenerateCallstack(GetWindowString(g_fovCurrent));
    else if (command == NOCLIP_ENABLED) {
        bool enabled = IsDlgButtonChecked(g_hwnd, NOCLIP_ENABLED);
        if (IsDlgButtonChecked(g_hwnd, NOCLIP_ENABLED) && g_trainer) {
            // The player position is from the feet, not the eyes, so we have to adjust slightly.
            auto playerPos = g_trainer->GetCameraPos();
            playerPos[2] -= 1.69f;
            g_trainer->SetPlayerPos(playerPos);
        }
        ToggleOption(NOCLIP_ENABLED, &Trainer::SetNoclip);
    } else if (command == ACTIVATE_GAME) {
        if (!g_trainer) ShellExecute(NULL, L"open", L"steam://rungameid/210970", NULL, NULL, SW_SHOWDEFAULT);
        else g_witnessProc->BringToFront();
    } else if (command == OPEN_SAVES) {
        PWSTR outPath;
        size_t size = SHGetKnownFolderPath(FOLDERID_RoamingAppData, SHGFP_TYPE_CURRENT, NULL, &outPath);
        std::wstring savesFolder(outPath, size);
        savesFolder += L"\\The Witness";
        ShellExecute(NULL, L"open", savesFolder.c_str(), NULL, NULL, SW_SHOWDEFAULT);
    } else if (!g_trainer) {
        // All other messages need the trainer to be live in order to execute.
        if (HIWORD(wParam) == 0) { // Initiated by the user
            MessageBox(g_hwnd, L"The process must be running in order to use this button", L"", MB_OK);
        }
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    if (command == NOCLIP_SPEED)        g_trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
    else if (command == FOV_CURRENT)    g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
    else if (command == SPRINT_SPEED)   g_trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
    else if (command == SHOW_PANELS)    g_trainer->ShowMissingPanels();
    else if (command == SHOW_NEARBY)    g_trainer->ShowNearbyEntities();
    else if (command == EXPORT)         g_trainer->ExportEntities();
    else if (command == SAVE_POS) {
        g_savedCameraPos = g_trainer->GetCameraPos();
        g_savedCameraAng = g_trainer->GetCameraAng();
        SetPosAndAngText(g_savedCameraPos, g_savedCameraAng, g_savedPos);
    } else if (command == LOAD_POS) {
        g_trainer->SetCameraPos(g_savedCameraPos);
        g_trainer->SetCameraAng(g_savedCameraAng);

        // The player position is from the feet, not the eyes, so we have to adjust slightly.
        auto playerPos = g_savedCameraPos;
        playerPos[2] -= 1.69f;
        g_trainer->SetPlayerPos(playerPos);
        SetPosAndAngText(g_savedCameraPos, g_savedCameraAng, g_currentPos);
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

HWND CreateButton(int x, int& y, int width, LPCWSTR text, __int64 message, LPCWSTR hoverText = L"") {
    HWND button = CreateWindow(L"BUTTON", text,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, width, 26,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 30;
    CreateTooltip(button, hoverText);
    return button;
}

HWND CreateCheckbox(int x, int& y, __int64 message, LPCWSTR hoverText = L"") {
    HWND checkbox = CreateWindow(L"BUTTON", NULL,
        WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
        x, y + 2, 12, 12,
        g_hwnd, (HMENU)message, g_hInstance, NULL);
    y += 20;
    CreateTooltip(checkbox, hoverText);
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
    CreateCheckbox(115, y, NOCLIP_ENABLED, L"Control-N");
    RegisterHotKey(g_hwnd, NOCLIP_ENABLED, MOD_NOREPEAT | MOD_CONTROL, 'N');

    CreateLabel(x, y + 4, 100, L"Noclip Speed");
    g_noclipSpeed = CreateText(100, y, 130, L"10", NOCLIP_SPEED);

    CreateLabel(x, y + 4, 100, L"Sprint Speed");
    g_sprintSpeed = CreateText(100, y, 130, L"2", SPRINT_SPEED);

    CreateLabel(x, y + 4, 100, L"Field of View");
    g_fovCurrent = CreateText(100, y, 130, L"50.534012", FOV_CURRENT);

    CreateLabel(x, y, 185, L"Can save the game");
    CreateCheckbox(200, y, CAN_SAVE, L"Shift-Control-S");
    RegisterHotKey(g_hwnd, CAN_SAVE, MOD_NOREPEAT | MOD_SHIFT | MOD_CONTROL, 'S');
    CheckDlgButton(g_hwnd, CAN_SAVE, true);

    CreateLabel(x, y, 185, L"Random Doors Practice");
    CreateCheckbox(200, y, DOORS_PRACTICE);

    CreateLabel(x, y, 185, L"Disable Challenge time limit");
    CreateCheckbox(200, y, INFINITE_CHALLENGE);

    CreateLabel(x, y, 185, L"Open the Console");
    CreateCheckbox(200, y, OPEN_CONSOLE, L"Tilde (~)");
    RegisterHotKey(g_hwnd, OPEN_CONSOLE, MOD_NOREPEAT | MOD_SHIFT, VK_OEM_3);

    CreateButton(x, y, 100, L"Save Position", SAVE_POS, L"Control-P");
    RegisterHotKey(g_hwnd, SAVE_POS, MOD_NOREPEAT | MOD_CONTROL, 'P');
    g_currentPos = CreateLabel(x + 5, y, 90, 80);
    SetPosAndAngText({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }, g_currentPos);

    // Column 1a
    x = 110;
    y -= 30;
    CreateButton(x, y, 100, L"Load Position", LOAD_POS, L"Shift-Control-P");
    RegisterHotKey(g_hwnd, LOAD_POS, MOD_NOREPEAT | MOD_SHIFT | MOD_CONTROL, 'P');
    g_savedPos = CreateLabel(x + 5, y, 90, 80);
    SetPosAndAngText({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }, g_savedPos);

    // Column 2
    x = 270;
    y = 10;

    g_activateGame = CreateButton(x, y, 200, L"Launch game", ACTIVATE_GAME);
    CreateButton(x, y, 200, L"Open save folder", OPEN_SAVES);

    g_activePanel = CreateLabel(x, y, 200, L"No Active Panel");
    y += 20;

    g_panelName = CreateLabel(x, y, 200, L"");
    y += 20;

    g_panelState = CreateLabel(x, y, 200, L"");
    y += 20;

    CreateButton(x, y, 200, L"Show unsolved panels", SHOW_PANELS);

    // Hotkey for debug purposes, to get addresses based on a reported callstack
    RegisterHotKey(g_hwnd, CALLSTACK, MOD_NOREPEAT | MOD_CONTROL | MOD_SHIFT | MOD_ALT, VK_OEM_PLUS);

#ifdef _DEBUG
    CreateButton(x, y, 200, L"Show nearby entities", SHOW_NEARBY);
    CreateButton(x, y, 200, L"Export all entities", EXPORT);
#endif
    // RegisterHotKey(g_hwnd, START_TIMER, MOD_NOREPEAT | MOD_CONTROL, 'T');
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
