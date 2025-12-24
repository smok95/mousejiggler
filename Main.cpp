// MouseJiggler - C++ Win32 Version
//
// Ported from C# version by Alistair J R Young
//
// A simple utility to prevent screensavers and idle detection
// by periodically sending mouse movement events.

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <tchar.h>
#include "Resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Global variables
HINSTANCE g_hInst = NULL;
HWND g_hMainDlg = NULL;
NOTIFYICONDATA g_nid = { 0 };
HMENU g_hTrayMenu = NULL;

// Settings
struct Settings {
    bool minimizeOnStartup;
    bool zenJiggle;
    int jigglePeriod;  // in seconds
    bool startJiggling;
} g_Settings = { false, false, 60, false };

// State
bool g_IsJiggling = false;
bool g_Zig = true;
TCHAR g_IniFilePath[MAX_PATH] = { 0 };

// Function declarations
INT_PTR CALLBACK MainDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void LoadSettings();
void SaveSettings();
void UpdateTrayIcon();
void UpdatePeriodLabel(HWND hDlg);
void MinimizeToTray();
void RestoreFromTray();
void PerformJiggle(int delta);
void StartJiggling();
void StopJiggling();
bool CreateSingleInstanceMutex();

// Get INI file path (in the same directory as the executable)
void InitializeIniPath() {
    GetModuleFileName(NULL, g_IniFilePath, MAX_PATH);
    TCHAR* lastSlash = _tcsrchr(g_IniFilePath, _T('\\'));
    if (lastSlash) {
        *(lastSlash + 1) = _T('\0');
        _tcscat_s(g_IniFilePath, MAX_PATH, _T("MouseJiggler.ini"));
    }
}

// Load settings from INI file
void LoadSettings() {
    g_Settings.minimizeOnStartup = GetPrivateProfileInt(_T("Settings"), _T("MinimizeOnStartup"), 0, g_IniFilePath) != 0;
    g_Settings.zenJiggle = GetPrivateProfileInt(_T("Settings"), _T("ZenJiggle"), 0, g_IniFilePath) != 0;
    g_Settings.jigglePeriod = GetPrivateProfileInt(_T("Settings"), _T("JigglePeriod"), 60, g_IniFilePath);

    // Validate jiggle period
    if (g_Settings.jigglePeriod < 1) g_Settings.jigglePeriod = 1;
    if (g_Settings.jigglePeriod > 10800) g_Settings.jigglePeriod = 10800;
}

// Save settings to INI file
void SaveSettings() {
    TCHAR buffer[32];

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.minimizeOnStartup ? 1 : 0);
    WritePrivateProfileString(_T("Settings"), _T("MinimizeOnStartup"), buffer, g_IniFilePath);

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.zenJiggle ? 1 : 0);
    WritePrivateProfileString(_T("Settings"), _T("ZenJiggle"), buffer, g_IniFilePath);

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.jigglePeriod);
    WritePrivateProfileString(_T("Settings"), _T("JigglePeriod"), buffer, g_IniFilePath);
}

// Perform the mouse jiggle
void PerformJiggle(int delta) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = delta;
    input.mi.dy = delta;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;

    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result != 1) {
        DWORD error = GetLastError();
        TCHAR msg[256];
        _stprintf_s(msg, 256, _T("Failed to send input: error code 0x%08X"), error);
        OutputDebugString(msg);
    }
}

// Start jiggling
void StartJiggling() {
    if (!g_IsJiggling) {
        g_IsJiggling = true;
        SetTimer(g_hMainDlg, TIMER_JIGGLE, g_Settings.jigglePeriod * 1000, NULL);
        UpdateTrayIcon();
    }
}

// Stop jiggling
void StopJiggling() {
    if (g_IsJiggling) {
        g_IsJiggling = false;
        KillTimer(g_hMainDlg, TIMER_JIGGLE);
        UpdateTrayIcon();
    }
}

// Update the period label
void UpdatePeriodLabel(HWND hDlg) {
    TCHAR text[64];
    _stprintf_s(text, 64, _T("%d s"), g_Settings.jigglePeriod);
    SetDlgItemText(hDlg, IDC_LABEL_PERIOD, text);
}

// Update tray icon tooltip
void UpdateTrayIcon() {
    if (g_nid.hWnd) {
        if (!g_IsJiggling) {
            _tcscpy_s(g_nid.szTip, 128, _T("Not jiggling the mouse."));
        } else {
            TCHAR text[128];
            _stprintf_s(text, 128, _T("Jiggling mouse every %d s, %s Zen."),
                g_Settings.jigglePeriod,
                g_Settings.zenJiggle ? _T("with") : _T("without"));
            _tcscpy_s(g_nid.szTip, 128, text);
        }
        Shell_NotifyIcon(NIM_MODIFY, &g_nid);
    }
}

// Minimize to system tray
void MinimizeToTray() {
    ShowWindow(g_hMainDlg, SW_HIDE);

    // Add tray icon
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hMainDlg;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_TRAYICON));
    UpdateTrayIcon();

    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

// Restore from system tray
void RestoreFromTray() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    ShowWindow(g_hMainDlg, SW_SHOW);
    SetForegroundWindow(g_hMainDlg);
}

// About dialog procedure
INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Main dialog procedure
INT_PTR CALLBACK MainDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        {
            g_hMainDlg = hDlg;

            // Set icon
            HICON hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_MAINICON));
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

            // Initialize controls
            CheckDlgButton(hDlg, IDC_CHECK_MINIMIZE, g_Settings.minimizeOnStartup ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHECK_ZEN, g_Settings.zenJiggle ? BST_CHECKED : BST_UNCHECKED);

            // Setup trackbar
            HWND hTrackbar = GetDlgItem(hDlg, IDC_SLIDER_PERIOD);
            SendMessage(hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(1, 180));
            SendMessage(hTrackbar, TBM_SETPOS, TRUE, g_Settings.jigglePeriod);
            SendMessage(hTrackbar, TBM_SETPAGESIZE, 0, 10);

            UpdatePeriodLabel(hDlg);

            // Settings panel initially hidden
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_SETTINGS), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_CHECK_MINIMIZE), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_CHECK_ZEN), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_SLIDER_PERIOD), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_LABEL_PERIOD), SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PERIOD_LABEL), SW_HIDE);

            // Start jiggling if requested
            if (g_Settings.startJiggling) {
                CheckDlgButton(hDlg, IDC_CHECK_JIGGLING, BST_CHECKED);
                StartJiggling();
            }

            // Minimize on startup if requested
            if (g_Settings.minimizeOnStartup) {
                PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_MINIMIZE, BN_CLICKED), 0);
            }

            return TRUE;
        }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHECK_JIGGLING:
            if (IsDlgButtonChecked(hDlg, IDC_CHECK_JIGGLING) == BST_CHECKED) {
                StartJiggling();
            } else {
                StopJiggling();
            }
            break;

        case IDC_CHECK_SETTINGS:
            {
                BOOL show = IsDlgButtonChecked(hDlg, IDC_CHECK_SETTINGS) == BST_CHECKED;
                ShowWindow(GetDlgItem(hDlg, IDC_STATIC_SETTINGS), show ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_CHECK_MINIMIZE), show ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_CHECK_ZEN), show ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_SLIDER_PERIOD), show ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_LABEL_PERIOD), show ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PERIOD_LABEL), show ? SW_SHOW : SW_HIDE);
            }
            break;

        case IDC_CHECK_MINIMIZE:
            g_Settings.minimizeOnStartup = IsDlgButtonChecked(hDlg, IDC_CHECK_MINIMIZE) == BST_CHECKED;
            SaveSettings();
            break;

        case IDC_CHECK_ZEN:
            g_Settings.zenJiggle = IsDlgButtonChecked(hDlg, IDC_CHECK_ZEN) == BST_CHECKED;
            SaveSettings();
            UpdateTrayIcon();
            break;

        case IDC_BUTTON_ABOUT:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTDIALOG), hDlg, AboutDialogProc);
            break;

        case IDC_BUTTON_MINIMIZE:
            MinimizeToTray();
            break;

        case ID_TRAY_OPEN:
            RestoreFromTray();
            break;

        case ID_TRAY_START:
            CheckDlgButton(hDlg, IDC_CHECK_JIGGLING, BST_CHECKED);
            StartJiggling();
            break;

        case ID_TRAY_STOP:
            CheckDlgButton(hDlg, IDC_CHECK_JIGGLING, BST_UNCHECKED);
            StopJiggling();
            break;

        case ID_TRAY_EXIT:
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            break;
        }
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_PERIOD)) {
            HWND hTrackbar = GetDlgItem(hDlg, IDC_SLIDER_PERIOD);
            g_Settings.jigglePeriod = (int)SendMessage(hTrackbar, TBM_GETPOS, 0, 0);
            UpdatePeriodLabel(hDlg);
            SaveSettings();

            // Update timer if jiggling
            if (g_IsJiggling) {
                KillTimer(hDlg, TIMER_JIGGLE);
                SetTimer(hDlg, TIMER_JIGGLE, g_Settings.jigglePeriod * 1000, NULL);
            }

            UpdateTrayIcon();
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_JIGGLE) {
            if (g_Settings.zenJiggle) {
                PerformJiggle(0);
            } else if (g_Zig) {
                PerformJiggle(4);
            } else {
                PerformJiggle(-4);
            }
            g_Zig = !g_Zig;
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            RestoreFromTray();
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            // Create context menu
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_OPEN, _T("Open"));
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

            if (!g_IsJiggling) {
                AppendMenu(hMenu, MF_STRING, ID_TRAY_START, _T("Start Jiggling"));
            } else {
                AppendMenu(hMenu, MF_STRING, ID_TRAY_STOP, _T("Stop Jiggling"));
            }

            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, _T("Exit"));

            SetForegroundWindow(hDlg);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hDlg, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_CLOSE:
        if (g_nid.hWnd) {
            Shell_NotifyIcon(NIM_DELETE, &g_nid);
        }
        DestroyWindow(hDlg);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }

    return FALSE;
}

// Create single instance mutex
bool CreateSingleInstanceMutex() {
    HANDLE hMutex = CreateMutex(NULL, TRUE, _T("Global\\ArkaneSystems.MouseJiggler"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return false;
    }
    return true;
}

// Parse command line arguments
void ParseCommandLine() {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    for (int i = 1; i < argc; i++) {
        if (_tcscmp(argv[i], _T("-j")) == 0 || _tcscmp(argv[i], _T("--jiggle")) == 0) {
            g_Settings.startJiggling = true;
        }
        else if (_tcscmp(argv[i], _T("-m")) == 0 || _tcscmp(argv[i], _T("--minimized")) == 0) {
            g_Settings.minimizeOnStartup = true;
        }
        else if (_tcscmp(argv[i], _T("-z")) == 0 || _tcscmp(argv[i], _T("--zen")) == 0) {
            g_Settings.zenJiggle = true;
        }
        else if (_tcscmp(argv[i], _T("-s")) == 0 || _tcscmp(argv[i], _T("--seconds")) == 0) {
            if (i + 1 < argc) {
                int seconds = _ttoi(argv[i + 1]);
                if (seconds >= 1 && seconds <= 10800) {
                    g_Settings.jigglePeriod = seconds;
                }
                i++;
            }
        }
        else if (_tcscmp(argv[i], _T("-h")) == 0 || _tcscmp(argv[i], _T("--help")) == 0 || _tcscmp(argv[i], _T("-?")) == 0) {
            MessageBox(NULL,
                _T("Usage: MouseJiggler [options]\n\n")
                _T("Options:\n")
                _T("  -j, --jiggle               Start with jiggling enabled\n")
                _T("  -m, --minimized            Start minimized\n")
                _T("  -z, --zen                  Start with zen (invisible) jiggling enabled\n")
                _T("  -s, --seconds <seconds>    Set number of seconds for the jiggle interval\n")
                _T("  -?, -h, --help             Show help and usage information\n"),
                _T("Mouse Jiggler - Help"),
                MB_OK | MB_ICONINFORMATION);
            ExitProcess(0);
        }
    }

    if (argv) LocalFree(argv);
}

// Entry point
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    g_hInst = hInstance;

    // Check for single instance
    if (!CreateSingleInstanceMutex()) {
        MessageBox(NULL,
            _T("Mouse Jiggler is already running. Aborting."),
            _T("Mouse Jiggler"),
            MB_OK | MB_ICONWARNING);
        return 1;
    }

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Initialize INI file path and load settings
    InitializeIniPath();
    LoadSettings();

    // Parse command line
    ParseCommandLine();

    // Create main dialog
    HWND hDlg = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_MAINDIALOG), NULL, MainDialogProc, 0);

    if (!hDlg) {
        MessageBox(NULL, _T("Failed to create main dialog"), _T("Error"), MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hDlg, SW_SHOW);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}
