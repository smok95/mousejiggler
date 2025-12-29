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
UINT g_uTaskbarCreated = 0;  // TaskbarCreated message

// Settings
struct Settings {
    bool minimizeOnStartup;
    bool zenJiggle;
    int jigglePeriod;  // in seconds
    bool startJiggling;

    // Time restriction
    bool enableTimeRestriction;
    int startHour;      // 0-23
    int startMinute;    // 0-59
    int endHour;        // 0-23
    int endMinute;      // 0-59
    bool enabledDays[7];  // 0=Sun, 1=Mon, ..., 6=Sat (matches SYSTEMTIME.wDayOfWeek)
} g_Settings = { false, false, 60, false, false, 9, 0, 18, 0, { true, true, true, true, true, true, true } };

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
void CreateTrayIcon();
void UpdatePeriodLabel(HWND hDlg);
void MinimizeToTray();
void RestoreFromTray();
void PerformJiggle(int delta);
void StartJiggling();
void StopJiggling();
bool CreateSingleInstanceMutex();
bool IsWithinTimeRange();
void UpdateJigglingButton(HWND hDlg);
void GetTimeRangeString(TCHAR* buffer, size_t bufferSize);
void GetActiveDaysString(TCHAR* buffer, size_t bufferSize);
void DrawPlayPauseButton(LPDRAWITEMSTRUCT pDIS);

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

    // Load time restriction settings
    g_Settings.enableTimeRestriction = GetPrivateProfileInt(_T("Settings"), _T("EnableTimeRestriction"), 0, g_IniFilePath) != 0;
    g_Settings.startHour = GetPrivateProfileInt(_T("Settings"), _T("StartHour"), 9, g_IniFilePath);
    g_Settings.startMinute = GetPrivateProfileInt(_T("Settings"), _T("StartMinute"), 0, g_IniFilePath);
    g_Settings.endHour = GetPrivateProfileInt(_T("Settings"), _T("EndHour"), 18, g_IniFilePath);
    g_Settings.endMinute = GetPrivateProfileInt(_T("Settings"), _T("EndMinute"), 0, g_IniFilePath);

    // Load weekday settings (default: all days enabled)
    TCHAR enabledDaysStr[256];
    GetPrivateProfileString(_T("Settings"), _T("EnabledDays"),
                           _T("Sun,Mon,Tue,Wed,Thu,Fri,Sat"),
                           enabledDaysStr, 256, g_IniFilePath);

    // Initialize all days to false first
    for (int i = 0; i < 7; i++) {
        g_Settings.enabledDays[i] = false;
    }

    // Parse comma-separated day names
    static const TCHAR* dayNames[7] = {
        _T("Sun"), _T("Mon"), _T("Tue"), _T("Wed"),
        _T("Thu"), _T("Fri"), _T("Sat")
    };

    TCHAR* context = NULL;
    TCHAR* token = _tcstok_s(enabledDaysStr, _T(","), &context);
    while (token != NULL) {
        // Skip leading spaces
        while (*token == _T(' ')) token++;

        // Find matching day name (case-insensitive)
        for (int i = 0; i < 7; i++) {
            if (_tcsicmp(token, dayNames[i]) == 0) {
                g_Settings.enabledDays[i] = true;
                break;
            }
        }
        token = _tcstok_s(NULL, _T(","), &context);
    }

    // Validate jiggle period
    if (g_Settings.jigglePeriod < 1) g_Settings.jigglePeriod = 1;
    if (g_Settings.jigglePeriod > 10800) g_Settings.jigglePeriod = 10800;

    // Validate time values
    if (g_Settings.startHour < 0 || g_Settings.startHour > 23) g_Settings.startHour = 9;
    if (g_Settings.startMinute < 0 || g_Settings.startMinute > 59) g_Settings.startMinute = 0;
    if (g_Settings.endHour < 0 || g_Settings.endHour > 23) g_Settings.endHour = 18;
    if (g_Settings.endMinute < 0 || g_Settings.endMinute > 59) g_Settings.endMinute = 0;
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

    // Save time restriction settings
    _stprintf_s(buffer, 32, _T("%d"), g_Settings.enableTimeRestriction ? 1 : 0);
    WritePrivateProfileString(_T("Settings"), _T("EnableTimeRestriction"), buffer, g_IniFilePath);

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.startHour);
    WritePrivateProfileString(_T("Settings"), _T("StartHour"), buffer, g_IniFilePath);

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.startMinute);
    WritePrivateProfileString(_T("Settings"), _T("StartMinute"), buffer, g_IniFilePath);

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.endHour);
    WritePrivateProfileString(_T("Settings"), _T("EndHour"), buffer, g_IniFilePath);

    _stprintf_s(buffer, 32, _T("%d"), g_Settings.endMinute);
    WritePrivateProfileString(_T("Settings"), _T("EndMinute"), buffer, g_IniFilePath);

    // Save weekday settings
    TCHAR enabledDaysStr[256] = { 0 };
    static const TCHAR* dayNames[7] = {
        _T("Sun"), _T("Mon"), _T("Tue"), _T("Wed"),
        _T("Thu"), _T("Fri"), _T("Sat")
    };

    bool first = true;
    for (int i = 0; i < 7; i++) {
        if (g_Settings.enabledDays[i]) {
            if (!first) {
                _tcscat_s(enabledDaysStr, 256, _T(","));
            }
            _tcscat_s(enabledDaysStr, 256, dayNames[i]);
            first = false;
        }
    }

    // Save comma-separated day list (empty string if no days enabled)
    WritePrivateProfileString(_T("Settings"), _T("EnabledDays"), enabledDaysStr, g_IniFilePath);
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

// Check if current time is within allowed time range
bool IsWithinTimeRange() {
    if (!g_Settings.enableTimeRestriction) {
        return true;  // No restriction = always allowed
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    // First check if current day is enabled (st.wDayOfWeek: 0=Sun, 1=Mon, ..., 6=Sat)
    if (!g_Settings.enabledDays[st.wDayOfWeek]) {
        return false;  // Day is disabled, regardless of time
    }

    // Then check time range
    int currentMinutes = st.wHour * 60 + st.wMinute;
    int startMinutes = g_Settings.startHour * 60 + g_Settings.startMinute;
    int endMinutes = g_Settings.endHour * 60 + g_Settings.endMinute;

    // Handle overnight time range (e.g., 22:00 - 06:00)
    if (startMinutes > endMinutes) {
        return currentMinutes >= startMinutes || currentMinutes < endMinutes;
    } else {
        return currentMinutes >= startMinutes && currentMinutes < endMinutes;
    }
}

// Update jiggling button (trigger repaint)
void UpdateJigglingButton(HWND hDlg) {
    HWND hButton = GetDlgItem(hDlg, IDC_CHECK_JIGGLING);
    InvalidateRect(hButton, NULL, TRUE);
}

// Draw play/pause button with GDI
void DrawPlayPauseButton(LPDRAWITEMSTRUCT pDIS) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;

    // Use global state variable directly
    bool isJiggling = g_IsJiggling;
    bool isHot = (pDIS->itemState & ODS_FOCUS) || (pDIS->itemState & ODS_HOTLIGHT);

    // Draw button background
    HBRUSH hBrush;
    if (isJiggling) {
        hBrush = CreateSolidBrush(RGB(240, 240, 240));  // Light gray when active
    } else {
        hBrush = CreateSolidBrush(RGB(255, 255, 255));  // White when inactive
    }
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);

    // Draw border
    HPEN hPen = CreatePen(PS_SOLID, isHot ? 2 : 1, RGB(0, 120, 215));  // Blue border
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hPen);

    // Calculate center position for icon
    int centerX = (rc.left + rc.right) / 2;
    int centerY = (rc.top + rc.bottom) / 2;
    int iconSize = 12;

    // Draw icon
    if (isJiggling) {
        // Draw PAUSE icon (two vertical bars) in RED
        HBRUSH hIconBrush = CreateSolidBrush(RGB(220, 50, 50));  // Red

        RECT bar1 = { centerX - 6, centerY - iconSize/2, centerX - 2, centerY + iconSize/2 };
        RECT bar2 = { centerX + 2, centerY - iconSize/2, centerX + 6, centerY + iconSize/2 };

        FillRect(hdc, &bar1, hIconBrush);
        FillRect(hdc, &bar2, hIconBrush);

        DeleteObject(hIconBrush);

        // Draw text "Jiggling..." to the right
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 50, 50));
        HFONT hFont = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        RECT textRc = rc;
        textRc.left = centerX + 15;
        DrawText(hdc, _T("Jiggling..."), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    } else {
        // Draw PLAY icon (right-pointing triangle) in GREEN
        HBRUSH hIconBrush = CreateSolidBrush(RGB(50, 180, 50));  // Green
        HBRUSH hOldIconBrush = (HBRUSH)SelectObject(hdc, hIconBrush);

        POINT triangle[3];
        triangle[0].x = centerX - 5;
        triangle[0].y = centerY - iconSize/2;
        triangle[1].x = centerX - 5;
        triangle[1].y = centerY + iconSize/2;
        triangle[2].x = centerX + 7;
        triangle[2].y = centerY;

        SetPolyFillMode(hdc, WINDING);
        Polygon(hdc, triangle, 3);

        SelectObject(hdc, hOldIconBrush);
        DeleteObject(hIconBrush);

        // Draw text "Click to Start" to the right
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(50, 180, 50));
        HFONT hFont = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        RECT textRc = rc;
        textRc.left = centerX + 15;
        DrawText(hdc, _T("Click to Start"), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }
}

// Get time range string for tray tooltip
void GetTimeRangeString(TCHAR* buffer, size_t bufferSize) {
    _stprintf_s(buffer, bufferSize, _T("%02d:%02d - %02d:%02d"),
                g_Settings.startHour, g_Settings.startMinute,
                g_Settings.endHour, g_Settings.endMinute);
}

// Get active days string for display
void GetActiveDaysString(TCHAR* buffer, size_t bufferSize) {
    static const TCHAR* dayAbbrevs[7] = {
        _T("Sun"), _T("Mon"), _T("Tue"), _T("Wed"),
        _T("Thu"), _T("Fri"), _T("Sat")
    };

    buffer[0] = _T('\0');
    int count = 0;

    for (int i = 0; i < 7; i++) {
        if (g_Settings.enabledDays[i]) {
            if (count > 0) {
                _tcscat_s(buffer, bufferSize, _T(","));
            }
            _tcscat_s(buffer, bufferSize, dayAbbrevs[i]);
            count++;
        }
    }

    // Handle edge cases
    if (count == 0) {
        _tcscpy_s(buffer, bufferSize, _T("None"));
    } else if (count == 7) {
        _tcscpy_s(buffer, bufferSize, _T("Every day"));
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
        TCHAR text[128];

        if (!g_IsJiggling) {
            if (g_Settings.enableTimeRestriction) {
                TCHAR timeRange[64];
                TCHAR days[64];
                GetTimeRangeString(timeRange, 64);
                GetActiveDaysString(days, 64);
                _stprintf_s(text, 128, _T("Not jiggling. %s (%s)"), timeRange, days);
                _tcscpy_s(g_nid.szTip, 128, text);
            } else {
                _tcscpy_s(g_nid.szTip, 128, _T("Not jiggling the mouse."));
            }
        } else {
            if (g_Settings.enableTimeRestriction) {
                TCHAR timeRange[64];
                TCHAR days[64];
                GetTimeRangeString(timeRange, 64);
                GetActiveDaysString(days, 64);
                _stprintf_s(text, 128, _T("Jiggling %d s, %s Zen. %s (%s)"),
                    g_Settings.jigglePeriod,
                    g_Settings.zenJiggle ? _T("with") : _T("without"),
                    timeRange,
                    days);
            } else {
                _stprintf_s(text, 128, _T("Jiggling %d s, %s Zen."),
                    g_Settings.jigglePeriod,
                    g_Settings.zenJiggle ? _T("with") : _T("without"));
            }
            _tcscpy_s(g_nid.szTip, 128, text);
        }
        Shell_NotifyIcon(NIM_MODIFY, &g_nid);
    }
}

// Add or recreate tray icon
void CreateTrayIcon() {
    if (g_nid.hWnd == NULL) {
        ZeroMemory(&g_nid, sizeof(g_nid));
        g_nid.cbSize = sizeof(NOTIFYICONDATA);
        g_nid.hWnd = g_hMainDlg;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_TRAYICON));
        UpdateTrayIcon();
    }

    BOOL result = Shell_NotifyIcon(NIM_ADD, &g_nid);
    if (!result) {
        DWORD error = GetLastError();
        TCHAR msg[256];
        _stprintf_s(msg, 256, _T("Failed to add tray icon: error 0x%08X"), error);
        OutputDebugString(msg);
    }
}

// Minimize to system tray
void MinimizeToTray() {
    ShowWindow(g_hMainDlg, SW_HIDE);
    CreateTrayIcon();
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

            // Initialize time restriction controls
            CheckDlgButton(hDlg, IDC_CHECK_ENABLE_TIME,
                          g_Settings.enableTimeRestriction ? BST_CHECKED : BST_UNCHECKED);

            // Setup spin controls and edit controls for time input
            // Set edit control values first
            SetDlgItemInt(hDlg, IDC_EDIT_START_HOUR, g_Settings.startHour, FALSE);
            SetDlgItemInt(hDlg, IDC_EDIT_START_MINUTE, g_Settings.startMinute, FALSE);
            SetDlgItemInt(hDlg, IDC_EDIT_END_HOUR, g_Settings.endHour, FALSE);
            SetDlgItemInt(hDlg, IDC_EDIT_END_MINUTE, g_Settings.endMinute, FALSE);

            // Then setup spin controls
            HWND hSpinStartHour = GetDlgItem(hDlg, IDC_SPIN_START_HOUR);
            SendMessage(hSpinStartHour, UDM_SETRANGE, 0, MAKELPARAM(23, 0));
            SendMessage(hSpinStartHour, UDM_SETPOS, 0, g_Settings.startHour);

            HWND hSpinStartMinute = GetDlgItem(hDlg, IDC_SPIN_START_MINUTE);
            SendMessage(hSpinStartMinute, UDM_SETRANGE, 0, MAKELPARAM(59, 0));
            SendMessage(hSpinStartMinute, UDM_SETPOS, 0, g_Settings.startMinute);

            HWND hSpinEndHour = GetDlgItem(hDlg, IDC_SPIN_END_HOUR);
            SendMessage(hSpinEndHour, UDM_SETRANGE, 0, MAKELPARAM(23, 0));
            SendMessage(hSpinEndHour, UDM_SETPOS, 0, g_Settings.endHour);

            HWND hSpinEndMinute = GetDlgItem(hDlg, IDC_SPIN_END_MINUTE);
            SendMessage(hSpinEndMinute, UDM_SETRANGE, 0, MAKELPARAM(59, 0));
            SendMessage(hSpinEndMinute, UDM_SETPOS, 0, g_Settings.endMinute);

            // Enable/disable time controls based on checkbox
            BOOL enableTimeControls = g_Settings.enableTimeRestriction;
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_START_HOUR), enableTimeControls);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_START_MINUTE), enableTimeControls);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_HOUR), enableTimeControls);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_MINUTE), enableTimeControls);

            // Initialize weekday checkboxes
            static const int weekdayControls[7] = {
                IDC_CHECK_SUNDAY, IDC_CHECK_MONDAY, IDC_CHECK_TUESDAY,
                IDC_CHECK_WEDNESDAY, IDC_CHECK_THURSDAY, IDC_CHECK_FRIDAY,
                IDC_CHECK_SATURDAY
            };

            for (int i = 0; i < 7; i++) {
                CheckDlgButton(hDlg, weekdayControls[i],
                               g_Settings.enabledDays[i] ? BST_CHECKED : BST_UNCHECKED);
                EnableWindow(GetDlgItem(hDlg, weekdayControls[i]), enableTimeControls);
            }

            // Start jiggling if requested
            if (g_Settings.startJiggling) {
                StartJiggling();
            }

            // Update button icon
            UpdateJigglingButton(hDlg);

            // Start time check timer if restriction enabled
            if (g_Settings.enableTimeRestriction) {
                SetTimer(hDlg, TIMER_TIME_CHECK, 1000, NULL);  // Check every second
            }

            // Minimize on startup if requested
            if (g_Settings.minimizeOnStartup) {
                PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_MINIMIZE, BN_CLICKED), 0);
            }

            return TRUE;
        }

    default:
        // Handle TaskbarCreated message (explorer.exe restart)
        if (message == g_uTaskbarCreated && g_uTaskbarCreated != 0) {
            // Recreate tray icon if window is hidden
            if (!IsWindowVisible(hDlg) && g_nid.hWnd != NULL) {
                OutputDebugString(_T("TaskbarCreated: Recreating tray icon"));
                CreateTrayIcon();
            }
            return TRUE;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHECK_JIGGLING:
            // Toggle jiggling state
            if (g_IsJiggling) {
                StopJiggling();
            } else {
                StartJiggling();
            }
            UpdateJigglingButton(hDlg);
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

        case IDC_CHECK_ENABLE_TIME:
            g_Settings.enableTimeRestriction = IsDlgButtonChecked(hDlg, IDC_CHECK_ENABLE_TIME) == BST_CHECKED;
            SaveSettings();

            // Enable/disable time input controls
            {
                BOOL enableTimeControls = g_Settings.enableTimeRestriction;
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_START_HOUR), enableTimeControls);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_START_MINUTE), enableTimeControls);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_HOUR), enableTimeControls);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_END_MINUTE), enableTimeControls);

                // Enable/disable weekday checkboxes
                static const int weekdayControls[7] = {
                    IDC_CHECK_SUNDAY, IDC_CHECK_MONDAY, IDC_CHECK_TUESDAY,
                    IDC_CHECK_WEDNESDAY, IDC_CHECK_THURSDAY, IDC_CHECK_FRIDAY,
                    IDC_CHECK_SATURDAY
                };

                for (int i = 0; i < 7; i++) {
                    EnableWindow(GetDlgItem(hDlg, weekdayControls[i]), enableTimeControls);
                }

                // Start/stop time check timer
                if (g_Settings.enableTimeRestriction) {
                    SetTimer(hDlg, TIMER_TIME_CHECK, 1000, NULL);
                    // Immediate check
                    PostMessage(hDlg, WM_TIMER, TIMER_TIME_CHECK, 0);
                } else {
                    KillTimer(hDlg, TIMER_TIME_CHECK);
                }
            }

            UpdateTrayIcon();
            break;

        case IDC_EDIT_START_HOUR:
        case IDC_EDIT_START_MINUTE:
        case IDC_EDIT_END_HOUR:
        case IDC_EDIT_END_MINUTE:
            if (HIWORD(wParam) == EN_KILLFOCUS) {
                // Update when user leaves the field (avoids initialization issues)
                g_Settings.startHour = GetDlgItemInt(hDlg, IDC_EDIT_START_HOUR, NULL, FALSE);
                g_Settings.startMinute = GetDlgItemInt(hDlg, IDC_EDIT_START_MINUTE, NULL, FALSE);
                g_Settings.endHour = GetDlgItemInt(hDlg, IDC_EDIT_END_HOUR, NULL, FALSE);
                g_Settings.endMinute = GetDlgItemInt(hDlg, IDC_EDIT_END_MINUTE, NULL, FALSE);

                // Clamp values
                if (g_Settings.startHour > 23) g_Settings.startHour = 23;
                if (g_Settings.startMinute > 59) g_Settings.startMinute = 59;
                if (g_Settings.endHour > 23) g_Settings.endHour = 23;
                if (g_Settings.endMinute > 59) g_Settings.endMinute = 59;

                UpdateTrayIcon();
            }
            break;

        case IDC_CHECK_SUNDAY:
        case IDC_CHECK_MONDAY:
        case IDC_CHECK_TUESDAY:
        case IDC_CHECK_WEDNESDAY:
        case IDC_CHECK_THURSDAY:
        case IDC_CHECK_FRIDAY:
        case IDC_CHECK_SATURDAY:
            {
                // Map control ID to day index (0=Sun, 1=Mon, ..., 6=Sat)
                int dayIndex = LOWORD(wParam) - IDC_CHECK_SUNDAY;
                g_Settings.enabledDays[dayIndex] =
                    IsDlgButtonChecked(hDlg, LOWORD(wParam)) == BST_CHECKED;
                SaveSettings();

                // Immediate time check to auto-start/stop if needed
                if (g_Settings.enableTimeRestriction) {
                    PostMessage(hDlg, WM_TIMER, TIMER_TIME_CHECK, 0);
                }

                UpdateTrayIcon();
            }
            break;

        case IDC_BUTTON_ABOUT:
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTDIALOG), hDlg, AboutDialogProc);
            break;

        case IDC_BUTTON_MINIMIZE:
            // Exit button - confirm and close
            if (MessageBox(hDlg,
                _T("Are you sure you want to exit Mouse Jiggler?"),
                _T("Confirm Exit"),
                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SendMessage(hDlg, WM_DESTROY, 0, 0);
            }
            break;

        case ID_TRAY_OPEN:
            RestoreFromTray();
            break;

        case ID_TRAY_START:
            StartJiggling();
            UpdateJigglingButton(hDlg);
            break;

        case ID_TRAY_STOP:
            StopJiggling();
            UpdateJigglingButton(hDlg);
            break;

        case ID_TRAY_EXIT:
            // Confirm and exit
            if (MessageBox(hDlg,
                _T("Are you sure you want to exit Mouse Jiggler?"),
                _T("Confirm Exit"),
                MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SendMessage(hDlg, WM_DESTROY, 0, 0);
            }
            break;
        }
        break;

    case WM_DRAWITEM:
        if (wParam == IDC_CHECK_JIGGLING) {
            DrawPlayPauseButton((LPDRAWITEMSTRUCT)lParam);
            return TRUE;
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
        else if (wParam == TIMER_TIME_CHECK) {
            // Check if we should auto-start or auto-stop
            if (g_Settings.enableTimeRestriction) {
                bool shouldBeJiggling = IsWithinTimeRange();

                if (shouldBeJiggling && !g_IsJiggling) {
                    // Auto-start: We're in time range but not jiggling
                    StartJiggling();
                    UpdateJigglingButton(hDlg);
                }
                else if (!shouldBeJiggling && g_IsJiggling) {
                    // Auto-stop: We're outside time range but still jiggling
                    StopJiggling();
                    UpdateJigglingButton(hDlg);
                }
            }
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
        // X button minimizes to tray instead of closing
        MinimizeToTray();
        break;

    case WM_DESTROY:
        // Save all settings
        SaveSettings();

        // Kill timers
        if (g_IsJiggling) {
            KillTimer(hDlg, TIMER_JIGGLE);
        }
        if (g_Settings.enableTimeRestriction) {
            KillTimer(hDlg, TIMER_TIME_CHECK);
        }

        // Remove tray icon
        if (g_nid.hWnd) {
            Shell_NotifyIcon(NIM_DELETE, &g_nid);
        }

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

        // Try to find and activate the existing window
        HWND hExistingWnd = FindWindow(NULL, _T("Mouse Jiggler"));
        if (hExistingWnd) {
            // If window is hidden (minimized to tray), restore it
            if (!IsWindowVisible(hExistingWnd)) {
                // Send message to restore from tray
                SendMessage(hExistingWnd, WM_COMMAND, MAKEWPARAM(ID_TRAY_OPEN, 0), 0);
            } else {
                // If visible but minimized, restore it
                if (IsIconic(hExistingWnd)) {
                    ShowWindow(hExistingWnd, SW_RESTORE);
                }
                // Bring to foreground
                SetForegroundWindow(hExistingWnd);
            }

            // Flash the window to draw user's attention
            FlashWindow(hExistingWnd, TRUE);
        }

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

    // Register TaskbarCreated message for explorer.exe restart detection
    g_uTaskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));

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
