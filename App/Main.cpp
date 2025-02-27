#include "pch.h"
#include "Richedit.h"
#include "Version.h"
#include "shellapi.h"
#include "Shlobj.h"

#include "Trainer.h"

#include <unordered_set>

#define HEARTBEAT           0x401
#define SAVE_POS            0x402
#define LOAD_POS            0x403
#define NOCLIP_SPEED        0x404
#define SPRINT_SPEED        0x405
#define FOV_CURRENT         0x406
#define NOCLIP_ENABLED      0x407
#define CAN_SAVE            0x408
#define DOORS_PRACTICE      0x409
#define INFINITE_CHALLENGE  0x410
#define OPEN_CONSOLE        0x411
#define ACTIVATE_GAME       0x412
#define OPEN_SAVES          0x413
#define SHOW_PANELS         0x414
#define SHOW_NEARBY         0x415
#define EXPORT              0x416
#define START_TIMER         0x417
#define CALLSTACK           0x418
#define SNAP_TO_PANEL       0x419
#define DISTANCE_GATING     0x420
#define EP_OVERLAY          0x421
#define CLAMP_AIM           0x422
#define OPEN_DOOR           0x423
#define NOCLIP_FLY_UP       0x424
#define NOCLIP_FLY_DOWN     0x425
#define NOCLIP_FLY_NONE     0x426

// BUGS:
// - Changing from old ver to new ver can set FOV = 0?

// Feature requests:
// - Bug report can submit via google forms? Maybe also a 'help/about' which has a button for generic feedback?
// - show solve collision
//   The trick here is going to be immediate-mode UI, coupled with determining which entities are presenting collision.
// - show player collision
// - Icon for trainer
//   https://stackoverflow.com/questions/40933304
// - Delete all saves (?)
// - Save settings to some file, and reload them on trainer start
// - CreateRemoteThread + VirtualAllocEx allows me to *run* code in another process. This seems... powerful!
// - SuspendThread as a way to pause the game when an assert fires? Then I could investigate...
// - Hotkeys should eat from game (e.g. shift-ctrl-s)
// - Change the solution fade time so that you can TP to puzzle (or whatever) and see what you traced. This will be easier than GDI+ nonsense.
//   It's still hard though.

// Bad/Hard ideas:
// - Basic timer, Show time of last challenge / last 20 challenges
//   Out of scope.
// - Avoid hanging the UI during load; call Trainer::ctor on a background thread.
//   Instead, I just sped up the sigscan.
// - Show currently traced line (hard, requires changes in Memory)
//   Would also require figuring out what needs to be changed to properly reset "panel data".
//   I actually figured out how to do this, but drawing the line is a behemoth.
// - _timing asl to the trainer? Just something simple would be good enough, mostly
// - LOD hack
// - Change current save name
//   Not possible -- the name of the save is created dynamically from the save file

// Globals
HWND g_hwnd;
HINSTANCE g_hInstance;
std::shared_ptr<Trainer> g_trainer;
std::shared_ptr<Memory> g_witnessProc;
HWND g_noclipSpeed, g_currentPos, g_savedPos, g_fovCurrent, g_sprintSpeed, g_activePanel, g_panelDist, g_panelName, g_panelState, g_panelPicture, g_activateGame, g_snapToPanel, g_snapToLabel, g_canSave, g_videoData;

std::vector<float> g_savedCameraPos = {0.0f, 0.0f, 0.0f};
std::vector<float> g_savedCameraAng = {0.0f, 0.0f};
int previousPanel = -1;
std::vector<float> previousPanelStart;

constexpr int32_t MASK_SHIFT   = 0x0100;
constexpr int32_t MASK_CONTROL = 0x0200;
constexpr int32_t MASK_ALT     = 0x0400;
constexpr int32_t MASK_WIN     = 0x0800;
constexpr int32_t MASK_REPEAT  = 0x1000;
constexpr int32_t MASK_RELEASE = 0x9900;
std::map<int32_t, __int64> hotkeyCodes;
std::unordered_set<int32_t> hotkeys; // Just the keys, for perf.

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

void SetPosAndAngText(HWND hwnd, const std::vector<float>& pos, const std::vector<float>& ang) {
    assert(pos.size() == 3, "[Internal error] Attempted to set position of <> 3 elements");
    assert(ang.size() == 2, "[Internal error] Attempted to set angle of <> 2 elements");
    std::wstring text(128, '\0');
    swprintf_s(text.data(), text.size(), L"X %.3f\nY %.3f\nZ %.3f\n\u0398 %.8f\n\u03A6 %.8f", pos[0], pos[1], pos[2], ang[0], ang[1]);
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

void SetVideoData(const Trainer::VideoData& videoData) {
#if _DEBUG
    std::string text(1024, '\0');
    sprintf_s(text.data(), text.size(), "Next sound index: %d\nMax sound index: %d\nDry sound ID: 0x%05X (index: %d)\nVideo: %s\nFrame: %05d / %05d\n",
        videoData.nextUnusedIdIdx,
        videoData.numUnusedIds,
        videoData.videoDrySoundId,
        videoData.videoDrySoundIdIdx,
        videoData.fileName.c_str(),
        videoData.currentFrame,
        videoData.totalFrames);
    SetStringText(g_videoData, text);
#endif
}

// We can do 3 different things in this function:
// activePanel != -1, previousPanel != activePanel -> Started solving a panel, show information about it
// activePanel != -1, previousPanel == activePanel -> Actively solving a panel, show information about it
// activePanel == -1, previousPanel != -1 -> Stopped solving a panel, show information about the previous panel.
void SetActivePanel(int activePanel) {
    if (activePanel != -1) previousPanel = activePanel;

    std::string typeName = "entity";

    if (g_trainer) {
        std::shared_ptr<Trainer::EntityData> entityData = g_trainer->GetEntityData(previousPanel);
        if (!entityData) {
            SetStringText(g_panelName, "");
            SetStringText(g_panelState, "");
            SetStringText(g_panelDist, "");
            CheckDlgButton(g_hwnd, SNAP_TO_PANEL, false);
            EnableWindow(g_snapToLabel, false);
            EnableWindow(g_snapToPanel, false);
        } else {
            typeName = entityData->type;
            SetStringText(g_panelName, "Name: " + entityData->name);
            SetStringText(g_panelState, entityData->state);
            if (!entityData->startPoint.empty()) {
                previousPanelStart = entityData->startPoint;
            }
            if (!previousPanelStart.empty()) {
                auto cameraPos = g_trainer->GetCameraPos();
                auto distance = sqrt(pow(previousPanelStart[0] - cameraPos[0], 2) + pow(previousPanelStart[1] - cameraPos[1], 2) + pow(previousPanelStart[2] - cameraPos[2], 2));
                SetStringText(g_panelDist, "Distance to " + entityData->type + ": " + std::to_string(distance));
                SetStringText(g_snapToLabel, "Lock view to " + entityData->type);
                EnableWindow(g_snapToLabel, true);
                EnableWindow(g_snapToPanel, true);
            }
        }
    }

    std::stringstream ss;
    if (activePanel != -1) {
        ss << "Active " << typeName << ":";
    } else if (previousPanel != -1) {
        ss << "Previous " << typeName << ":";
    } else {
        ss << "No active entity";
    }
    if (previousPanel != -1) {
        ss << " 0x" << std::hex << std::setfill('0') << std::setw(5) << previousPanel;
    }
    SetStringText(g_activePanel, ss.str());
}

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
        case WM_COMMAND:
            break; // LOWORD(wParam) contains the command
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
            case ProcStatus::NotRunning:
                // Don't discard any settings, just free the trainer.
                if (g_trainer) g_trainer = nullptr;
                // Also reset the title & launch text, since they can get stuck
                SetStringText(g_hwnd, L"Witness Trainer");
                SetStringText(g_activateGame, L"Launch game");
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
                SetStringText(g_hwnd, L"Witness Trainer");
                // Or, we started a new game / loaded a save, in which case some of the entity data might have been reset.
                SetActivePanel(-1);
                previousPanel = -1;
                g_trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
                g_trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
                g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
                g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));
                g_trainer->SetCanSave(IsDlgButtonChecked(hwnd, CAN_SAVE));
                g_trainer->SetRandomDoorsPractice(IsDlgButtonChecked(hwnd, DOORS_PRACTICE));
                g_trainer->SetInfiniteChallenge(IsDlgButtonChecked(hwnd, INFINITE_CHALLENGE));
                g_trainer->SetConsoleOpen(IsDlgButtonChecked(hwnd, OPEN_CONSOLE));
                g_trainer->SetEPOverlay(IsDlgButtonChecked(hwnd, EP_OVERLAY));
                g_trainer->SetChallengePillarsPractice(true);
                SetStringText(g_activateGame, L"Switch to game");
                break;
            case ProcStatus::Running:
                if (!g_trainer) {
                    // Process was already running, and we just started. Load settings from the game.
                    SetStringText(g_hwnd, L"Attaching to The Witness...");
                    g_trainer = Trainer::Create(g_witnessProc);
                    if (!g_trainer) break;
                    SetStringText(g_hwnd, L"Witness Trainer");
                    SetFloatText(g_noclipSpeed, g_trainer->GetNoclipSpeed());
                    SetFloatText(g_sprintSpeed, g_trainer->GetSprintSpeed());
                    SetFloatText(g_fovCurrent, g_trainer->GetFov());
                    CheckDlgButton(hwnd, NOCLIP_ENABLED, g_trainer->GetNoclip());
                    CheckDlgButton(hwnd, CAN_SAVE, g_trainer->CanSave());
                    CheckDlgButton(hwnd, DOORS_PRACTICE, g_trainer->GetRandomDoorsPractice());
                    CheckDlgButton(hwnd, INFINITE_CHALLENGE, g_trainer->GetInfiniteChallenge());
                    CheckDlgButton(hwnd, OPEN_CONSOLE, g_trainer->GetConsoleOpen());
                    CheckDlgButton(hwnd, EP_OVERLAY, g_trainer->GetEPOverlay());
                    SetStringText(g_activateGame, L"Switch to game");
                    g_trainer->SetMainMenuState(true);
                    g_trainer->SetChallengePillarsPractice(true);
                } else {
                    // Process was already running, and so were we (this recurs every heartbeat). Enforce settings and apply repeated actions.
                    g_trainer->SetNoclip(IsDlgButtonChecked(hwnd, NOCLIP_ENABLED));

                    // If we are the foreground window, set FOV. Otherwise, read FOV.
                    if (g_hwnd == GetForegroundWindow()) {
                        g_trainer->SetFov(GetWindowFloat(g_fovCurrent));
                    } else {
                        SetFloatText(g_fovCurrent, g_trainer->GetFov());
                    }

                    if (IsDlgButtonChecked(hwnd, SNAP_TO_PANEL) && previousPanel != -1) {
                        g_trainer->SnapToPoint(previousPanelStart);
                    }
                }

                // Settings which are always sourced from the game, since they are not editable in the trainer.
                // For performance reasons (redrawing text is expensive), these update 10x slower than other display fields.
                static int64_t update = 0;
                if (++update % 10 == 0) {
                    SetPosAndAngText(g_currentPos, g_trainer->GetCameraPos(), g_trainer->GetCameraAng());
                    SetActivePanel(g_trainer->GetActivePanel());
#if _DEBUG
                    SetVideoData(g_trainer->GetVideoData());
#endif
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
        if (command == INFINITE_CHALLENGE)      ToggleOption(INFINITE_CHALLENGE, &Trainer::SetInfiniteChallenge);
        else if (command == DOORS_PRACTICE)     ToggleOption(DOORS_PRACTICE, &Trainer::SetRandomDoorsPractice);
        else if (command == OPEN_CONSOLE)       ToggleOption(OPEN_CONSOLE, &Trainer::SetConsoleOpen);
        else if (command == EP_OVERLAY)         ToggleOption(EP_OVERLAY, &Trainer::SetEPOverlay);
        else if (command == CLAMP_AIM)          ToggleOption(CLAMP_AIM, &Trainer::ClampAimingPhi);
        else if (command == CALLSTACK)          DebugUtils::RegenerateCallstack(GetWindowString(g_fovCurrent));
        else if (command == NOCLIP_ENABLED) {
            // Fix up the player position when exiting noclip
            if (IsDlgButtonChecked(g_hwnd, NOCLIP_ENABLED) && trainer) {
                // The player position is from the feet, not the eyes, so we have to adjust slightly.
                auto playerPos = trainer->GetCameraPos();
                playerPos[2] -= 1.69f;
                trainer->SetPlayerPos(playerPos);
            }
            ToggleOption(NOCLIP_ENABLED, &Trainer::SetNoclip);
        } else if (command == NOCLIP_FLY_UP) {
            // Only change position if NOCLIP is enabled.
            if (IsDlgButtonChecked(g_hwnd, NOCLIP_ENABLED) && trainer) {
                trainer->SetNoclipFlyDirection(trainer->UP);
            }
        } else if (command == NOCLIP_FLY_DOWN) {
            // Only change position if NOCLIP is enabled.
            if (IsDlgButtonChecked(g_hwnd, NOCLIP_ENABLED) && trainer) {
                trainer->SetNoclipFlyDirection(trainer->DOWN);
            }
        } else if (command == NOCLIP_FLY_NONE) {
            // Only change position if NOCLIP is enabled.
            if (IsDlgButtonChecked(g_hwnd, NOCLIP_ENABLED) && trainer) {
                trainer->SetNoclipFlyDirection(trainer->NONE);
            }
        } else if (command == CAN_SAVE) {
            if (IsDlgButtonChecked(g_hwnd, CAN_SAVE)) {
                // If the game is running, request one last save before disabling saving
                if (trainer) {
                    EnableWindow(g_canSave, false); // This can take a little while, prevent accidental re-clicks by disabling the checkbox.
                    bool saved = trainer->SaveCampaign();
                    EnableWindow(g_canSave, true);
                    if (!saved) return; // If we failed to save after the timeout, don't toggle the checkbox.
                }
            }
            ToggleOption(CAN_SAVE, &Trainer::SetCanSave);
        } else if (command == ACTIVATE_GAME) {
            if (!trainer) LaunchSteamGame("210970", "-skip_config_dialog");
            else g_witnessProc->BringToFront();
        } else if (command == OPEN_SAVES) {
            PWSTR outPath;
            SHGetKnownFolderPath(FOLDERID_RoamingAppData, SHGFP_TYPE_CURRENT, NULL, &outPath);
            std::wstring savesFolder = outPath;
            CoTaskMemFree(outPath);
            savesFolder += L"\\The Witness";
            ShellExecute(NULL, L"open", savesFolder.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        } else if (command == SNAP_TO_PANEL) {
            if (HIWORD(wParam) == STN_CLICKED && IsWindowEnabled(g_snapToPanel)) {
                bool enabled = IsDlgButtonChecked(g_hwnd, SNAP_TO_PANEL);
                CheckDlgButton(g_hwnd, SNAP_TO_PANEL, !enabled);
            }
        } else if (!trainer && HIWORD(wParam) == 0) { // Message was triggered by the user
            MessageBox(g_hwnd, L"The process must be running in order to use this button", L"", MB_OK);
        }

        // All other messages need the trainer to be live in order to execute.
        if (!trainer) return;

        if (command == NOCLIP_SPEED)         trainer->SetNoclipSpeed(GetWindowFloat(g_noclipSpeed));
        else if (command == FOV_CURRENT)     {} // Because we constantly update FOV, we should not respond to this command here.
        else if (command == SPRINT_SPEED)    trainer->SetSprintSpeed(GetWindowFloat(g_sprintSpeed));
        else if (command == SHOW_PANELS)     trainer->ShowMissingPanels();
        else if (command == SHOW_NEARBY)     trainer->ShowNearbyEntities();
        else if (command == EXPORT)          trainer->ExportEntities();
        else if (command == DISTANCE_GATING) trainer->DisableDistanceGating();
        else if (command == OPEN_DOOR)       trainer->OpenNearbyDoors();
        else if (command == SAVE_POS) {
            g_savedCameraPos = trainer->GetCameraPos();
            g_savedCameraAng = trainer->GetCameraAng();
            SetPosAndAngText(g_savedPos, g_savedCameraPos, g_savedCameraAng);
        } else if (command == LOAD_POS) {
            if (g_savedCameraPos[0] != 0 ||  g_savedCameraPos[1] != 0 || g_savedCameraPos[2] != 0) { // Prevent TP to origin (i.e. if the user hasn't set a position yet)
                trainer->SetCameraPos(g_savedCameraPos);
                trainer->SetCameraAng(g_savedCameraAng);

                // The player position is from the feet, not the eyes, so we have to adjust slightly.
                auto playerPos = g_savedCameraPos;
                playerPos[2] -= 1.69f;
                trainer->SetPlayerPos(playerPos);
                SetPosAndAngText(g_currentPos, g_savedCameraPos, g_savedCameraAng);
            }
        }
    });
    t.detach();

    return DefWindowProc(hwnd, message, wParam, lParam);
}

int32_t lastCode = 0;
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Only steal hotkeys when we (or the game) are the active window.
    if (nCode == HC_ACTION) {
        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            lastCode = 0; // Cancel key repeat

            // Handle KeyRelease event.
            auto foreground = GetForegroundWindow();
            if (g_hwnd == foreground || g_witnessProc->IsForeground()) {
                auto p = (PKBDLLHOOKSTRUCT)lParam;
                int32_t fullCode = p->vkCode | MASK_RELEASE;
                auto search = hotkeyCodes.find(fullCode);
                if (search != std::end(hotkeyCodes)) PostMessage(g_hwnd, WM_COMMAND, search->second, NULL);
            }
        } else if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            auto foreground = GetForegroundWindow();
            if (g_hwnd == foreground || g_witnessProc->IsForeground()) {
                auto p = (PKBDLLHOOKSTRUCT)lParam;
                int32_t fullCode = p->vkCode;

                __int64 found = 0;
                if (hotkeys.find(fullCode) != hotkeys.end()) { // For perf, we look at just the keyboard key first (before consulting GetKeyState).
                    if (GetKeyState(VK_SHIFT) & 0x8000)     fullCode |= MASK_SHIFT;
                    if (GetKeyState(VK_CONTROL) & 0x8000)   fullCode |= MASK_CONTROL;
                    if (GetKeyState(VK_MENU) & 0x8000)      fullCode |= MASK_ALT;
                    if (GetKeyState(VK_LWIN) & 0x8000)      fullCode |= MASK_WIN;
                    if (GetKeyState(VK_RWIN) & 0x8000)      fullCode |= MASK_WIN;
                    if (lastCode == fullCode)               fullCode |= MASK_REPEAT;

                    auto search = hotkeyCodes.find(fullCode);
                    if (search != std::end(hotkeyCodes)) found = search->second;
                }
                lastCode = fullCode & ~MASK_REPEAT;

                if (found) {
                    PostMessage(g_hwnd, WM_COMMAND, found, NULL);
                    return -1; // Do not let the game see this keyboard input (in case it overlaps with the user's keybinds)
                }
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

// Call this to create a regular key binding.
void CreateHotkey(__int64 message, int32_t hotkey) {
    hotkeyCodes[hotkey] = message;
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

    CreateLabelAndCheckbox(x, y, 100, L"Noclip Enabled", NOCLIP_ENABLED, L"Control-N", MASK_CONTROL | 'N');
    // Create fly up/down hotkeys when using noclip.
    CreateHotkey(NOCLIP_FLY_UP, 'E');
    CreateHotkey(NOCLIP_FLY_DOWN, 'Q');
    CreateHotkey(NOCLIP_FLY_NONE, MASK_RELEASE | 'E');
    CreateHotkey(NOCLIP_FLY_NONE, MASK_RELEASE | 'Q');

    CreateLabel(x, y + 4, 100, L"Noclip Speed");
    g_noclipSpeed = CreateText(100, y, 130, L"10", NOCLIP_SPEED);

    CreateLabel(x, y + 4, 100, L"Sprint Speed");
    g_sprintSpeed = CreateText(100, y, 130, L"2", SPRINT_SPEED);

    CreateLabel(x, y + 4, 100, L"Field of View");
    g_fovCurrent = CreateText(100, y, 130, L"50.534012", FOV_CURRENT);

    auto [_, canSave] = CreateLabelAndCheckbox(x, y, 185, L"Can save the game", CAN_SAVE, L"Shift-Control-S", MASK_SHIFT | MASK_CONTROL | 'S');
    g_canSave = canSave;
    CheckDlgButton(g_hwnd, CAN_SAVE, true);

    CreateLabelAndCheckbox(x, y, 185, L"Random Doors Practice", DOORS_PRACTICE);

    CreateLabelAndCheckbox(x, y, 185, L"Disable Challenge time limit", INFINITE_CHALLENGE);

    CreateLabelAndCheckbox(x, y, 185, L"Open the Console", OPEN_CONSOLE, L"Tilde (~)", MASK_SHIFT | VK_OEM_3);

    CreateLabelAndCheckbox(x, y, 185, L"Show Entity Solvability", EP_OVERLAY, L"Alt-2", MASK_ALT | '2');

    CreateLabelAndCheckbox(x, y, 185, L"Enable vertical aim limit", CLAMP_AIM);
    CheckDlgButton(g_hwnd, CLAMP_AIM, true);

    CreateButton(x, y, 110, L"Save Position", SAVE_POS, L"Control-P", MASK_CONTROL | 'P');
    y -= 30;
    CreateButton(x + 120, y, 110, L"Load Position", LOAD_POS, L"Shift-Control-P", MASK_SHIFT | MASK_CONTROL | 'P');
    g_currentPos = CreateLabel(x + 5,   y, 110, 80);
    g_savedPos   = CreateLabel(x + 125, y, 110, 80);
    SetPosAndAngText(g_currentPos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f });
    SetPosAndAngText(g_savedPos,   { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f });
    y += 90;

#if _DEBUG
    g_videoData = CreateLabel(x, y, 250, 100);
#endif

    // Column 2
    x = 270;
    y = 10;

    g_activateGame = CreateButton(x, y, 200, L"Launch game", ACTIVATE_GAME);
    CreateButton(x, y, 200, L"Open save folder", OPEN_SAVES);

    g_activePanel = CreateLabel(x, y, 200, L"No active entity");
    y += 20;

    g_panelDist = CreateLabel(x, y, 200, L"");
    y += 20;

    g_panelName = CreateLabel(x, y, 200, L"");
    y += 20;

    g_panelState = CreateLabel(x, y, 200, L"");
    y += 20;

    std::tie(g_snapToLabel, g_snapToPanel) = CreateLabelAndCheckbox(x, y, 200, L"Lock view to entity", SNAP_TO_PANEL, L"Control-L", MASK_CONTROL | 'L');
    EnableWindow(g_snapToLabel, false);
    EnableWindow(g_snapToPanel, false);

    CreateButton(x, y, 200, L"Show unsolved panels", SHOW_PANELS);

    CreateButton(x, y, 200, L"Disable distance gating", DISTANCE_GATING);

    CreateButton(x, y, 200, L"Open nearby doors", OPEN_DOOR, L"Control-O", MASK_CONTROL | 'O');

    // Hotkey for debug purposes, to get addresses based on a reported callstack
    hotkeyCodes[MASK_CONTROL | MASK_SHIFT | MASK_ALT | VK_OEM_PLUS] = CALLSTACK;

#ifdef _DEBUG
    CreateButton(x, y, 200, L"Show nearby entities", SHOW_NEARBY);
    CreateButton(x, y, 200, L"Export all entities", EXPORT);
#endif

    for (const auto [key, _] : hotkeyCodes) hotkeys.insert(key & 0xFF);
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

    g_witnessProc = std::make_shared<Memory>(L"witness64_d3d11.exe");
    g_witnessProc->StartHeartbeat(g_hwnd, HEARTBEAT);
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
    g_witnessProc->StopHeartbeat();
    g_witnessProc = nullptr;

    CoUninitialize();
    return (int) msg.wParam;
}
