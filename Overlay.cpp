#include "Overlay.hpp"
#include "VisualAtmosphere.hpp"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <atomic>
#include <string>
#include <cstdio>
#include <cmath>

namespace VA
{
    static const int WINDOW_WIDTH = 480;
    static const int WINDOW_HEIGHT = 640;

    enum ControlIDs {
        IDC_CHECK_ENABLE = 1001,
        IDC_COMBO_PRESET,
        IDC_TRACK_RED,
        IDC_TRACK_GREEN,
        IDC_TRACK_BLUE,
        IDC_TRACK_SAT,
        IDC_TRACK_CONTRAST,
        IDC_TRACK_SHADOW,
        IDC_TRACK_VIGNETTE,
        IDC_LABEL_RED_VAL,
        IDC_LABEL_GREEN_VAL,
        IDC_LABEL_BLUE_VAL,
        IDC_LABEL_SAT_VAL,
        IDC_LABEL_CONTRAST_VAL,
        IDC_LABEL_SHADOW_VAL,
        IDC_LABEL_VIGNETTE_VAL,
        IDC_BTN_SAVE_CUSTOM,
        IDC_BTN_SAVE_INI,
        IDC_BTN_RESET,
        IDC_BTN_CLOSE
    };

    static HMODULE g_hOverlayModule = NULL;
    static HWND g_hOverlayWnd = NULL;
    static HWND g_hEldenRingWnd = NULL;
    static HFONT g_hFont = NULL;
    static HFONT g_hFontBold = NULL;
    static HBRUSH g_hBgBrush = NULL;
    static COLORREF g_TextColor = RGB(225, 225, 230);
    static COLORREF g_GoldColor = RGB(220, 185, 120);

    // Control handles
    static HWND hCheckEnable = NULL;
    static HWND hComboPreset = NULL;
    static HWND hTrackRed = NULL, hLabelRedVal = NULL;
    static HWND hTrackGreen = NULL, hLabelGreenVal = NULL;
    static HWND hTrackBlue = NULL, hLabelBlueVal = NULL;
    static HWND hTrackSat = NULL, hLabelSatVal = NULL;
    static HWND hTrackCont = NULL, hLabelContVal = NULL;
    static HWND hTrackShadow = NULL, hLabelShadowVal = NULL;
    static HWND hTrackVignette = NULL, hLabelVignetteVal = NULL;

    static std::atomic<bool> g_OverlayRunning{ false };
    static std::atomic<bool> g_MenuOpen{ false };
    static HANDLE g_hOverlayThread = NULL;

    bool IsMenuOpen()
    {
        return g_MenuOpen.load();
    }

    static HWND FindEldenRingWindow()
    {
        DWORD currentPid = GetCurrentProcessId();
        HWND hwnd = NULL;
        while ((hwnd = FindWindowExA(NULL, hwnd, NULL, NULL)) != NULL)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == currentPid)
            {
                char cls[256];
                GetClassNameA(hwnd, cls, sizeof(cls));
                if (strcmp(cls, "GRDirect3D12Window") == 0)
                    return hwnd;

                char title[256];
                GetWindowTextA(hwnd, title, sizeof(title));
                if (strstr(title, "ELDEN RING"))
                    return hwnd;
            }
        }
        hwnd = FindWindowA("GRDirect3D12Window", NULL);
        if (hwnd) return hwnd;
        hwnd = FindWindowA(NULL, "ELDEN RING™");
        if (!hwnd) hwnd = FindWindowA(NULL, "ELDEN RING");
        return hwnd;
    }

    static void SyncControlsFromConfig()
    {
        if (!g_hOverlayWnd) return;

        SendMessage(hCheckEnable, BM_SETCHECK, g_Config.enableAtmosphere ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(hComboPreset, CB_SETCURSEL, g_Config.activePreset, 0);

        SendMessage(hTrackRed, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.scaleR * 100.0f));
        SendMessage(hTrackGreen, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.scaleG * 100.0f));
        SendMessage(hTrackBlue, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.scaleB * 100.0f));
        SendMessage(hTrackSat, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.saturation * 100.0f));
        SendMessage(hTrackCont, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.contrast * 100.0f));
        SendMessage(hTrackShadow, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.shadowDepth * 1000.0f));
        SendMessage(hTrackVignette, TBM_SETPOS, TRUE, (LPARAM)round(g_Config.vignetteIntensity * 100.0f));

        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", g_Config.scaleR); SetWindowTextA(hLabelRedVal, buf);
        snprintf(buf, sizeof(buf), "%.3f", g_Config.scaleG); SetWindowTextA(hLabelGreenVal, buf);
        snprintf(buf, sizeof(buf), "%.3f", g_Config.scaleB); SetWindowTextA(hLabelBlueVal, buf);
        snprintf(buf, sizeof(buf), "%.2f", g_Config.saturation); SetWindowTextA(hLabelSatVal, buf);
        snprintf(buf, sizeof(buf), "%.2f", g_Config.contrast); SetWindowTextA(hLabelContVal, buf);
        snprintf(buf, sizeof(buf), "%.3f", g_Config.shadowDepth); SetWindowTextA(hLabelShadowVal, buf);
        snprintf(buf, sizeof(buf), "%.2f", g_Config.vignetteIntensity); SetWindowTextA(hLabelVignetteVal, buf);
    }

    static const UINT WM_VA_TOGGLE_MENU = WM_USER + 101;

    static void DoToggleMenu()
    {
        if (!g_hOverlayWnd || !IsWindow(g_hOverlayWnd)) return;

        bool next = !g_MenuOpen.load();
        g_MenuOpen.store(next);

        if (next)
        {
            if (!g_hEldenRingWnd || !IsWindow(g_hEldenRingWnd))
                g_hEldenRingWnd = FindEldenRingWindow();

            int posX = 100, posY = 100;
            if (g_hEldenRingWnd && IsWindow(g_hEldenRingWnd))
            {
                RECT rc;
                GetWindowRect(g_hEldenRingWnd, &rc);
                posX = rc.left + ((rc.right - rc.left) - WINDOW_WIDTH) / 2;
                posY = rc.top + ((rc.bottom - rc.top) - WINDOW_HEIGHT) / 2;
            }

            SyncControlsFromConfig();
            ClipCursor(NULL);
            while (ShowCursor(TRUE) < 0);

            SetWindowPos(g_hOverlayWnd, HWND_TOPMOST, posX, posY, WINDOW_WIDTH, WINDOW_HEIGHT, SWP_SHOWWINDOW);

            DWORD dwCur = GetCurrentThreadId();
            HWND hFg = GetForegroundWindow();
            DWORD dwFg = hFg ? GetWindowThreadProcessId(hFg, NULL) : 0;
            if (dwFg && dwFg != dwCur) AttachThreadInput(dwCur, dwFg, TRUE);

            SetForegroundWindow(g_hOverlayWnd);
            SetFocus(g_hOverlayWnd);
            SetActiveWindow(g_hOverlayWnd);

            if (dwFg && dwFg != dwCur) AttachThreadInput(dwCur, dwFg, FALSE);
        }
        else
        {
            ShowWindow(g_hOverlayWnd, SW_HIDE);
            if (g_hEldenRingWnd && IsWindow(g_hEldenRingWnd))
            {
                DWORD dwCur = GetCurrentThreadId();
                DWORD dwTarget = GetWindowThreadProcessId(g_hEldenRingWnd, NULL);
                if (dwTarget && dwTarget != dwCur) AttachThreadInput(dwCur, dwTarget, TRUE);

                SetForegroundWindow(g_hEldenRingWnd);
                SetFocus(g_hEldenRingWnd);
                SetActiveWindow(g_hEldenRingWnd);

                if (dwTarget && dwTarget != dwCur) AttachThreadInput(dwCur, dwTarget, FALSE);
            }
        }
    }

    void ToggleMenu()
    {
        if (g_hOverlayWnd && IsWindow(g_hOverlayWnd))
        {
            PostMessageA(g_hOverlayWnd, WM_VA_TOGGLE_MENU, 0, 0);
        }
    }

    static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
            InitCommonControlsEx(&icex);

            g_hBgBrush = CreateSolidBrush(RGB(22, 22, 26));

            g_hFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

            g_hFontBold = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            // 1. Title / Header
            HWND hTitle = CreateWindowA("STATIC", "VISUAL ATMOSPHERE  |  Dark Fantasy Engine v2.3",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                15, 12, 435, 22, hWnd, NULL, hInst, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            // 2. Enable Checkbox
            hCheckEnable = CreateWindowA("BUTTON", "Включить атмосферу (Enable Atmosphere)",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                20, 42, 420, 24, hWnd, (HMENU)IDC_CHECK_ENABLE, hInst, NULL);
            SendMessage(hCheckEnable, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            // 3. Preset Combo
            HWND hPresetLabel = CreateWindowA("STATIC", "Предустановки (Presets):",
                WS_CHILD | WS_VISIBLE,
                20, 75, 200, 18, hWnd, NULL, hInst, NULL);
            SendMessage(hPresetLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            hComboPreset = CreateWindowA("COMBOBOX", NULL,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                20, 96, 420, 150, hWnd, (HMENU)IDC_COMBO_PRESET, hInst, NULL);
            SendMessage(hComboPreset, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            SendMessageA(hComboPreset, CB_ADDSTRING, 0, (LPARAM)"0. Bloodborne / Yharnam Noir");
            SendMessageA(hComboPreset, CB_ADDSTRING, 0, (LPARAM)"1. Dark Souls 3 / Lothric Ashen");
            SendMessageA(hComboPreset, CB_ADDSTRING, 0, (LPARAM)"2. Demon's Souls / Boletaria Deep Cold");
            SendMessageA(hComboPreset, CB_ADDSTRING, 0, (LPARAM)"3. Cold Slate (Кинематографичный)");
            SendMessageA(hComboPreset, CB_ADDSTRING, 0, (LPARAM)"4. Пользовательский (Custom)");

            // 4. Color Balance Section
            HWND hSec1 = CreateWindowA("STATIC", "--- Цветовой баланс (RGB Balance) ---",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 134, 420, 18, hWnd, NULL, hInst, NULL);
            SendMessage(hSec1, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            auto CreateSliderRow = [&](const char* labelText, int y, int ctrlId, int valId, int minVal, int maxVal, HWND& outTrack, HWND& outLabel) {
                HWND lbl = CreateWindowA("STATIC", labelText, WS_CHILD | WS_VISIBLE, 20, y + 2, 125, 20, hWnd, NULL, hInst, NULL);
                SendMessage(lbl, WM_SETFONT, (WPARAM)g_hFont, TRUE);

                outTrack = CreateWindowA(TRACKBAR_CLASSA, NULL, WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
                    150, y, 225, 24, hWnd, (HMENU)ctrlId, hInst, NULL);
                SendMessage(outTrack, TBM_SETRANGE, TRUE, MAKELPARAM(minVal, maxVal));

                outLabel = CreateWindowA("STATIC", "0.00", WS_CHILD | WS_VISIBLE | SS_RIGHT, 380, y + 2, 55, 20, hWnd, (HMENU)valId, hInst, NULL);
                SendMessage(outLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            };

            CreateSliderRow("Красный (Red):", 160, IDC_TRACK_RED, IDC_LABEL_RED_VAL, 50, 150, hTrackRed, hLabelRedVal);
            CreateSliderRow("Зеленый (Green):", 192, IDC_TRACK_GREEN, IDC_LABEL_GREEN_VAL, 50, 150, hTrackGreen, hLabelGreenVal);
            CreateSliderRow("Синий (Blue):", 224, IDC_TRACK_BLUE, IDC_LABEL_BLUE_VAL, 50, 150, hTrackBlue, hLabelBlueVal);

            // 5. Tonal Grading Section
            HWND hSec2 = CreateWindowA("STATIC", "--- Тональная градация (Tonal Grading) ---",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 262, 420, 18, hWnd, NULL, hInst, NULL);
            SendMessage(hSec2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            CreateSliderRow("Насыщенность:", 288, IDC_TRACK_SAT, IDC_LABEL_SAT_VAL, 0, 200, hTrackSat, hLabelSatVal);
            CreateSliderRow("Контрастность:", 320, IDC_TRACK_CONTRAST, IDC_LABEL_CONTRAST_VAL, 50, 200, hTrackCont, hLabelContVal);
            CreateSliderRow("Глубина теней:", 352, IDC_TRACK_SHADOW, IDC_LABEL_SHADOW_VAL, 0, 150, hTrackShadow, hLabelShadowVal);
            CreateSliderRow("Виньетирование:", 384, IDC_TRACK_VIGNETTE, IDC_LABEL_VIGNETTE_VAL, 0, 50, hTrackVignette, hLabelVignetteVal);

            // 6. Action Buttons
            HWND btnCustom = CreateWindowA("BUTTON", "Сохранить как Custom",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, 430, 200, 30, hWnd, (HMENU)IDC_BTN_SAVE_CUSTOM, hInst, NULL);
            SendMessage(btnCustom, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND btnSaveIni = CreateWindowA("BUTTON", "Сохранить в INI",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                240, 430, 200, 30, hWnd, (HMENU)IDC_BTN_SAVE_INI, hInst, NULL);
            SendMessage(btnSaveIni, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND btnReset = CreateWindowA("BUTTON", "Сбросить к Bloodborne",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, 470, 200, 30, hWnd, (HMENU)IDC_BTN_RESET, hInst, NULL);
            SendMessage(btnReset, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND btnClose = CreateWindowA("BUTTON", "Закрыть (F5)",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                240, 470, 200, 30, hWnd, (HMENU)IDC_BTN_CLOSE, hInst, NULL);
            SendMessage(btnClose, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND hHint = CreateWindowA("STATIC", "Горячая клавиша: F5 | Закрытие: F5, ESC или кнопка",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 516, 420, 20, hWnd, NULL, hInst, NULL);
            SendMessage(hHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            SyncControlsFromConfig();
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_TextColor);
            return (INT_PTR)g_hBgBrush;
        }

        case WM_CTLCOLORBTN:
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_TextColor);
            return (INT_PTR)g_hBgBrush;
        }

        case WM_HSCROLL:
        {
            HWND hTrack = (HWND)lParam;
            int pos = (int)SendMessage(hTrack, TBM_GETPOS, 0, 0);
            char buf[32];

            if (hTrack == hTrackRed)
            {
                g_Config.scaleR = pos / 100.0f;
                snprintf(buf, sizeof(buf), "%.3f", g_Config.scaleR);
                SetWindowTextA(hLabelRedVal, buf);
            }
            else if (hTrack == hTrackGreen)
            {
                g_Config.scaleG = pos / 100.0f;
                snprintf(buf, sizeof(buf), "%.3f", g_Config.scaleG);
                SetWindowTextA(hLabelGreenVal, buf);
            }
            else if (hTrack == hTrackBlue)
            {
                g_Config.scaleB = pos / 100.0f;
                snprintf(buf, sizeof(buf), "%.3f", g_Config.scaleB);
                SetWindowTextA(hLabelBlueVal, buf);
            }
            else if (hTrack == hTrackSat)
            {
                g_Config.saturation = pos / 100.0f;
                snprintf(buf, sizeof(buf), "%.2f", g_Config.saturation);
                SetWindowTextA(hLabelSatVal, buf);
            }
            else if (hTrack == hTrackCont)
            {
                g_Config.contrast = pos / 100.0f;
                snprintf(buf, sizeof(buf), "%.2f", g_Config.contrast);
                SetWindowTextA(hLabelContVal, buf);
            }
            else if (hTrack == hTrackShadow)
            {
                g_Config.shadowDepth = pos / 1000.0f;
                snprintf(buf, sizeof(buf), "%.3f", g_Config.shadowDepth);
                SetWindowTextA(hLabelShadowVal, buf);
            }
            else if (hTrack == hTrackVignette)
            {
                g_Config.vignetteIntensity = pos / 100.0f;
                snprintf(buf, sizeof(buf), "%.2f", g_Config.vignetteIntensity);
                SetWindowTextA(hLabelVignetteVal, buf);
            }

            if (g_Config.activePreset != 4)
            {
                g_Config.activePreset = 4;
                SendMessage(hComboPreset, CB_SETCURSEL, 4, 0);
            }
            return 0;
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == IDC_CHECK_ENABLE && code == BN_CLICKED)
            {
                bool enabled = (SendMessage(hCheckEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_Config.enableAtmosphere = enabled;
                g_Enabled = enabled;
                VA_SaveConfig();
            }
            else if (id == IDC_COMBO_PRESET && code == CBN_SELCHANGE)
            {
                int sel = (int)SendMessage(hComboPreset, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel <= 4)
                {
                    ApplyPreset(sel);
                    SyncControlsFromConfig();
                    VA_SaveConfig();
                }
            }
            else if (id == IDC_BTN_SAVE_CUSTOM && code == BN_CLICKED)
            {
                g_Config.customScaleR = g_Config.scaleR;
                g_Config.customScaleG = g_Config.scaleG;
                g_Config.customScaleB = g_Config.scaleB;
                g_Config.customSaturation = g_Config.saturation;
                g_Config.customContrast = g_Config.contrast;
                g_Config.customShadowDepth = g_Config.shadowDepth;
                g_Config.customVignetteIntensity = g_Config.vignetteIntensity;
                g_Config.activePreset = 4;
                SendMessage(hComboPreset, CB_SETCURSEL, 4, 0);
                VA_SaveConfig();
                MessageBoxA(hWnd, "Настройки сохранены в качестве Пользовательского пресета (Custom)!", "Visual Atmosphere", MB_OK | MB_ICONINFORMATION);
            }
            else if (id == IDC_BTN_SAVE_INI && code == BN_CLICKED)
            {
                VA_SaveConfig();
                MessageBoxA(hWnd, "Конфигурация записана в VisualAtmosphere.ini!", "Visual Atmosphere", MB_OK | MB_ICONINFORMATION);
            }
            else if (id == IDC_BTN_RESET && code == BN_CLICKED)
            {
                ApplyPreset(0);
                SyncControlsFromConfig();
                VA_SaveConfig();
            }
            else if (id == IDC_BTN_CLOSE && code == BN_CLICKED)
            {
                ToggleMenu();
            }
            return 0;
        }

        case WM_VA_TOGGLE_MENU:
            DoToggleMenu();
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_F5 || wParam == VK_ESCAPE)
            {
                DoToggleMenu();
                return 0;
            }
            break;

        case WM_CLOSE:
            DoToggleMenu();
            return 0;

        case WM_DESTROY:
            if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
            if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
            if (g_hBgBrush) { DeleteObject(g_hBgBrush); g_hBgBrush = NULL; }
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    static DWORD WINAPI OverlayThreadProc(LPVOID lpParam)
    {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icex);

        WNDCLASSEXA wc = {
            sizeof(WNDCLASSEXA),
            CS_HREDRAW | CS_VREDRAW,
            OverlayWndProc,
            0L, 0L,
            g_hOverlayModule ? g_hOverlayModule : GetModuleHandleA(NULL),
            NULL,
            LoadCursor(NULL, IDC_ARROW),
            (HBRUSH)(COLOR_WINDOW + 1),
            NULL,
            "VisualAtmosphereOverlayClass",
            NULL
        };
        RegisterClassExA(&wc);

        g_hOverlayWnd = CreateWindowExA(
            WS_EX_TOPMOST,
            "VisualAtmosphereOverlayClass",
            "Visual Atmosphere — Настройки",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            100, 100, WINDOW_WIDTH, WINDOW_HEIGHT,
            NULL, NULL, wc.hInstance, NULL
        );

        if (!g_hOverlayWnd)
        {
            Log("ERROR: CreateWindowExA failed for Overlay window!");
            return 1;
        }

        // Enable Windows 10/11 native immersive dark titlebar and frame
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(g_hOverlayWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

        ShowWindow(g_hOverlayWnd, SW_HIDE);
        Log("Overlay initialized successfully (Native zero-conflict GUI window: 0x%p)", g_hOverlayWnd);

        // Standard Win32 Message Loop
        MSG msg;
        while (g_OverlayRunning.load() && GetMessageA(&msg, NULL, 0, 0))
        {
            if (msg.message == WM_KEYDOWN && (msg.wParam == (WPARAM)g_Config.menuKey || msg.wParam == VK_ESCAPE))
            {
                ToggleMenu();
                continue;
            }

            if (g_hOverlayWnd && IsDialogMessageA(g_hOverlayWnd, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (g_hOverlayWnd)
        {
            DestroyWindow(g_hOverlayWnd);
            g_hOverlayWnd = NULL;
        }
        UnregisterClassA("VisualAtmosphereOverlayClass", wc.hInstance);

        Log("Overlay thread shutdown cleanly");
        return 0;
    }

    bool InitOverlay(HMODULE hModule)
    {
        g_hOverlayModule = hModule;
        g_OverlayRunning.store(true);
        g_hOverlayThread = CreateThread(NULL, 0, OverlayThreadProc, NULL, 0, NULL);
        return g_hOverlayThread != NULL;
    }

    void ShutdownOverlay()
    {
        g_OverlayRunning.store(false);
        if (g_hOverlayWnd)
        {
            PostMessageA(g_hOverlayWnd, WM_CLOSE, 0, 0);
        }
        if (g_hOverlayThread)
        {
            WaitForSingleObject(g_hOverlayThread, 1500);
            CloseHandle(g_hOverlayThread);
            g_hOverlayThread = NULL;
        }
    }
}
