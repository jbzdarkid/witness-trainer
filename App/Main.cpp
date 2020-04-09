#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"

#define HEARTBEAT 0x401
#define NOCLIP_ENABLED 0x402
#define NOCLIP_SPEED 0x403
#define SAVE_POS 0x404
#define LOAD_POS 0x405
#define FOV_CURRENT 0x408
#define CAN_SAVE 0x409
#define SPRINT_SPEED 0x410
#define INFINITE_CHALLENGE 0x411
#define DOORS_PRACTICE 0x412
#define ACTIVATE_GAME 0x413
#define OPEN_SAVES 0x414
#define SHOW_PANELS 0x415
#define SHOW_NEARBY 0x416
#define EXPORT 0x417

// Bugs:
// - Use campaign_state changes to detect game reload (avoid crashes with "Can save the game")
//  Wrong, because campaign_state isn't changing. But I still would like to avoid / figure out why we're crashing with "can save the game"

// Feature requests:
// - show collision, somehow
// - Change current save name: Overwrite get_campaign_string_of_current_time
//  Nope, I mean the save name in-game.
// - "Save the game" button on the trainer?
// - "Load last save" button on the trainer?
// - Icon for trainer
// - Toggle console button
// - Delete all saves (?)
// - Fix noclip position -- maybe just repeatedly TP the player to the camera pos?
//  Naive solution did not work. Maybe an action taken (only once) as we exit noclip?
// - Add count of missed panels to the "Show unsolved panels" dialogue

// Bad/Hard ideas:
// - Avoid hanging the UI during load; call Trainer::ctor on a background thread.
// - Show currently traced line (hard, requires changes in Memory)
// - Improvement for 'while noclip is on', solve mode doesn't reset position (?)
// - _timing asl to the trainer? Just something simple would be good enough, mostly
// - LOD hack
// - possible improvements with last_saved_campaign (instead of using campaign save time), or time_of_last_save? Not sure.

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
HWND g_noclipSpeed, g_currentPos, g_savedPos, g_fovCurrent, g_sprintSpeed, g_activePanel, g_panelName, g_panelState, g_panelPicture;
std::vector<float> savedPos = {0.0f, 0.0f, 0.0f};
std::vector<float> savedAng = {0.0f, 0.0f};
int currentPanel = -1;
auto g_witnessProc = std::make_shared<Memory>(L"witness64_d3d11.exe");

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

float GetWindowFloat(HWND hwnd) {
    std::wstring text(128, L'\0');
    int length = GetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
    text.resize(length);
    return wcstof(text.c_str(), nullptr);
}

void SetActivePanel(int activePanel) {
    if (activePanel != -1) currentPanel = activePanel;

    std::wstringstream ss;
    if (activePanel != -1) {
        ss << L"Active Panel:";
    } else {
        ss << L"Previous Panel:";
    }
    ss << L" 0x" << std::hex << std::setfill(L'0') << std::setw(5) << currentPanel;
    SetWindowText(g_activePanel, ss.str().c_str());

    if (currentPanel != -1) {
        std::shared_ptr<Trainer::EntityData> panelData = g_trainer->GetEntityData(currentPanel);
        SetWindowTextA(g_panelName, panelData->name.c_str());
        SetWindowTextA(g_panelState, panelData->state.c_str());
        // TODO(Future): draw path with GDI
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
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        if (g_trainer) {
            g_trainer = nullptr;
            g_witnessProc = nullptr; // Free any allocated memory
        }
        return 0;
    }
    if (message != WM_COMMAND) {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    if (LOWORD(wParam) == HEARTBEAT) {
        switch ((ProcStatus)lParam) {
        case ProcStatus::NotRunning:
            // Don't discard any settings, just free the trainer.
            if (g_trainer) g_trainer = nullptr;
            break;
        case ProcStatus::Reload:
            break;
        case ProcStatus::Running:
            if (!g_trainer) {
                // Trainer started, game is running; or
                // Trainer running, game is started
                g_trainer = std::make_shared<Trainer>(g_witnessProc);
                SetFloatText(g_trainer->GetNoclipSpeed(), g_noclipSpeed);
                SetFloatText(g_trainer->GetFov(), g_fovCurrent);
                SetFloatText(g_trainer->GetSprintSpeed(), g_sprintSpeed);
            }
            g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));
            g_trainer->SetCanSave(IsDlgButtonChecked(hwnd, CAN_SAVE));
            g_trainer->SetInfiniteChallenge(IsDlgButtonChecked(hwnd, INFINITE_CHALLENGE));
            g_trainer->SetRandomDoorsPractice(IsDlgButtonChecked(hwnd, DOORS_PRACTICE));
            SetPosAndAngText(g_trainer->GetPlayerPos(), g_trainer->GetCameraAng(), g_currentPos);

            if (g_hwnd != GetActiveWindow()) {
                // Only replace when in the background (i.e. if someone changed their FOV in-game)
                SetFloatText(g_trainer->GetFov(), g_fovCurrent);
            }
            SetActivePanel(g_trainer->GetActivePanel());
            break;
        }
        return 0;
    }

    WORD command = LOWORD(wParam);
    if (command == NOCLIP_ENABLED)          ToggleOption(NOCLIP_ENABLED, &Trainer::SetNoclip);
    else if (command == CAN_SAVE)           ToggleOption(CAN_SAVE, &Trainer::SetCanSave);
    else if (command == INFINITE_CHALLENGE) ToggleOption(INFINITE_CHALLENGE, &Trainer::SetInfiniteChallenge);
    else if (command == DOORS_PRACTICE)     ToggleOption(DOORS_PRACTICE, &Trainer::SetRandomDoorsPractice);

    // All other messages need the trainer to be live in order to execute.
    if (!g_trainer) {
        if (HIWORD(wParam) == 0) { // Initiated by the user
            MessageBoxA(g_hwnd, "The process must be running in order to use this button", "", MB_OK);
        }
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    if (command == NOCLIP_SPEED)        g_trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
    else if (command == FOV_CURRENT)    g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
    else if (command == SPRINT_SPEED)   g_trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
    else if (command == SHOW_PANELS)    g_trainer->ShowMissingPanels();
    else if (command == SHOW_NEARBY)    g_trainer->ShowNearbyEntities();
    else if (command == EXPORT)         g_trainer->ExportEntities();
    else if (command == ACTIVATE_GAME)  g_witnessProc->BringToFront();
    else if (command == OPEN_SAVES) {
        PWSTR outPath;
        size_t size = SHGetKnownFolderPath(FOLDERID_RoamingAppData, SHGFP_TYPE_CURRENT, NULL, &outPath);
        ShellExecute(NULL, L"open", (outPath + std::wstring(L"\\The Witness")).c_str(), NULL, NULL, SW_SHOWDEFAULT);
    } else if (command == SAVE_POS) {
        savedPos = g_trainer->GetPlayerPos();
        savedAng = g_trainer->GetCameraAng();
        SetPosAndAngText(savedPos, savedAng, g_savedPos);
    } else if (command == LOAD_POS) {
        g_trainer->SetPlayerPos(savedPos);
        g_trainer->SetCameraAng(savedAng);
        SetPosAndAngText(savedPos, savedAng, g_currentPos);
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

    // Column 1
    int x = 10;
    int y = 10;

    CreateLabel(x, y, 100, L"Noclip Enabled"); // BUG: Not rendering?
    CreateCheckbox(115, y+2, NOCLIP_ENABLED);
    y += 20;

    CreateLabel(x, y+4, 100, L"Noclip Speed");
    g_noclipSpeed = CreateText(100, y, 130, L"", NOCLIP_SPEED);
    y += 30;

    CreateLabel(x, y+4, 100, L"Sprint Speed");
    g_sprintSpeed = CreateText(100, y, 130, L"", SPRINT_SPEED);
    y += 30;

    CreateLabel(x, y+4, 100, L"Field of View");
    g_fovCurrent = CreateText(100, y, 130, L"", FOV_CURRENT);
    y += 30;

    CreateLabel(x, y, 130, L"Can save the game");
    CreateCheckbox(145, y+2, CAN_SAVE);
    CheckDlgButton(g_hwnd, CAN_SAVE, true);
    y += 20;

    CreateLabel(x, y, 155, L"Random Doors Practice");
    CreateCheckbox(170, y+2, DOORS_PRACTICE);
    y += 20;

    CreateLabel(x, y, 185, L"Disable Challenge time limit");
    CreateCheckbox(200, y+2, INFINITE_CHALLENGE);
    y += 20;

    CreateButton(x, y, 100, L"Save Position", SAVE_POS);
    CreateButton(x+100, y, 100, L"Load Position", LOAD_POS);
    y += 30;
    g_currentPos = CreateLabel(x+5, y, 90, 80);
    g_savedPos = CreateLabel(x+105, y, 90, 80);
    y += 90;
    SetPosAndAngText({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, g_currentPos);
    SetPosAndAngText({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, g_savedPos);

    // Column 2
    x = 270;
    y = 10;

    CreateButton(x, y, 200, L"Switch to game", ACTIVATE_GAME);
    y += 30;

    CreateButton(x, y, 200, L"Open save folder", OPEN_SAVES);
    y += 30;

    g_activePanel = CreateLabel(x, y, 200, L"No Active Panel");
    y += 20;

    g_panelName = CreateLabel(x, y, 200, L"");
    y += 20;

    g_panelState = CreateLabel(x, y, 200, L"");
    y += 20;

    CreateButton(x, y, 200, L"Show unsolved panels", SHOW_PANELS);
    y += 30;

#ifndef NDEBUG
    CreateButton(x, y, 200, L"Show nearby entities", SHOW_NEARBY);
    y += 30;

    CreateButton(x, y, 200, L"Export all entities", EXPORT);
    y += 30;
#endif

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
