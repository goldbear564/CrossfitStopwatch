#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <stdio.h>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

#define IDC_EDIT_COUNTDOWN   1001
#define IDC_EDIT_TARGET      1002
#define IDC_BTN_START        1003
#define IDC_BTN_STOP         1004
#define IDC_BTN_RESUME       1005
#define IDC_BTN_RESET        1006
#define IDC_LBL_COUNTDOWN    1007
#define IDC_LBL_TARGET       1008

#define UI_TIMER_ID          1
#define UI_TIMER_MS          16

typedef enum { MODE_IDLE = 0, MODE_COUNTDOWN, MODE_RUNNING, MODE_PAUSED } RUN_MODE;

static RUN_MODE g_mode = MODE_IDLE;
static HWND g_hWnd = NULL, g_editCountdown = NULL, g_editTarget = NULL;
static HWND g_btnStart = NULL, g_btnStop = NULL, g_btnResume = NULL, g_btnReset = NULL;
static HWND g_lblCountdown = NULL, g_lblTarget = NULL;
static HFONT g_uiFont = NULL, g_bigFont = NULL;
static LARGE_INTEGER g_qpf = { 0 }, g_tStart = { 0 }, g_tCountdownStart = { 0 };
static LONGLONG g_countdownDurMs = 5000, g_targetDurMs = 0, g_pausedAccumMs = 0, g_lastShownMs = 0;

static LONGLONG NowMs(void) { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (LONGLONG)((n.QuadPart * 1000.0) / (double)g_qpf.QuadPart); }
static LONGLONG DiffMs(LARGE_INTEGER a, LARGE_INTEGER b) { LONGLONG d = a.QuadPart - b.QuadPart; return (LONGLONG)((d * 1000.0) / (double)g_qpf.QuadPart); }

static void MsToString(LONGLONG ms, WCHAR* out, size_t cch) {
    if (ms < 0) ms = 0;
    int hund = (int)((ms / 10) % 100), sec = (int)((ms / 1000) % 60), min = (int)(ms / 60000);
    _snwprintf_s(out, cch, _TRUNCATE, L"%02d:%02d.%02d", min, sec, hund);
}

static LONGLONG ParseFlexibleTimeMs(const WCHAR* s) {
    int mm = 0, ss = 0, hund = 0;
    if (swscanf_s(s, L"%d:%d.%d", &mm, &ss, &hund) == 3) return (LONGLONG)mm * 60000 + (LONGLONG)ss * 1000 + (LONGLONG)hund * 10;
    if (swscanf_s(s, L"%d:%d", &mm, &ss) == 2)          return (LONGLONG)mm * 60000 + (LONGLONG)ss * 1000;
    if (swscanf_s(s, L"%d.%d", &ss, &hund) == 2)        return (LONGLONG)ss * 1000 + (LONGLONG)hund * 10;
    if (swscanf_s(s, L"%d", &ss) == 1)                 return (LONGLONG)ss * 1000;
    return 0;
}

static void ReadInputs(void) {
    WCHAR buf[128];
    GetWindowTextW(g_editCountdown, buf, 128); g_countdownDurMs = max(0, ParseFlexibleTimeMs(buf));
    GetWindowTextW(g_editTarget, buf, 128);    g_targetDurMs = max(0, ParseFlexibleTimeMs(buf));
}

static void RebuildBigFont(HDC) {
    if (g_bigFont) { DeleteObject(g_bigFont); g_bigFont = NULL; }
    RECT rc; GetClientRect(g_hWnd, &rc);
    int displayH = max(50, (rc.bottom - rc.top) - 160);
    int fontPx = max(20, (int)(displayH * 0.70));
    LOGFONTW lf = { 0 }; lf.lfHeight = -fontPx; lf.lfWeight = FW_BOLD; lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_bigFont = CreateFontIndirectW(&lf);
}
static void EnsureUIFont(void) {
    if (!g_uiFont) { LOGFONTW lf = { 0 }; lf.lfHeight = -18; lf.lfWeight = FW_SEMIBOLD; wcscpy_s(lf.lfFaceName, L"Segoe UI"); g_uiFont = CreateFontIndirectW(&lf); }
}

static void UpdateButtons(void) {
    EnableWindow(g_btnStart, (g_mode == MODE_IDLE || g_mode == MODE_PAUSED));
    EnableWindow(g_btnStop, (g_mode == MODE_RUNNING || g_mode == MODE_COUNTDOWN));
    EnableWindow(g_btnResume, (g_mode == MODE_PAUSED));
    EnableWindow(g_btnReset, TRUE);
}

static COLORREF PickColorForDisplay(void) {
    if (g_mode == MODE_RUNNING || g_mode == MODE_PAUSED) {
        if (g_targetDurMs > 0 && g_lastShownMs >= g_targetDurMs) return RGB(220, 50, 50);
        return RGB(40, 160, 60);
    }
    if (g_mode == MODE_COUNTDOWN) return RGB(230, 140, 30);
    return RGB(90, 90, 90);
}

static void PaintBigDisplay(HDC hdc) {
    RECT rc; GetClientRect(g_hWnd, &rc);

    // 배경 (부모가 자식 위를 칠하지 않도록 WS_CLIPCHILDREN 추가해야 할듯?)
    HBRUSH bg = CreateSolidBrush(RGB(15, 15, 18));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // 상단 컨트롤 영역 경계선
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(50, 50, 55)); HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, 150, NULL); LineTo(hdc, rc.right, 150); SelectObject(hdc, oldPen); DeleteObject(pen);

    RECT rcDisp = rc; rcDisp.top += 150;

    WCHAR text[64] = L"00:00.00";
    if (g_mode == MODE_COUNTDOWN) {
        LONGLONG passed = NowMs() - (LONGLONG)((g_tCountdownStart.QuadPart * 1000.0) / (double)g_qpf.QuadPart);
        LONGLONG remain = max(0, g_countdownDurMs - passed);
        g_lastShownMs = remain; MsToString(remain, text, 64);
    }
    else if (g_mode == MODE_RUNNING || g_mode == MODE_PAUSED) {
        MsToString(g_lastShownMs, text, 64);
    }
    else { g_lastShownMs = 0; MsToString(0, text, 64); }

    HFONT oldF = (HFONT)SelectObject(hdc, g_bigFont ? g_bigFont : GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, PickColorForDisplay());
    DrawTextW(hdc, text, -1, &rcDisp, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    if (g_targetDurMs > 0) {
        WCHAR sub[64], label[80]; MsToString(g_targetDurMs, sub, 64);
        _snwprintf_s(label, 80, _TRUNCATE, L"Target: %s", sub);
        RECT r = rc; r.left = r.right - 300; r.top = r.bottom - 28;
        SetTextColor(hdc, RGB(180, 180, 180)); SelectObject(hdc, g_uiFont);
        DrawTextW(hdc, label, -1, &r, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
    }
}

static void DoLayout(void) {
    RECT rc; GetClientRect(g_hWnd, &rc); int w = rc.right - rc.left;
    const int m = 12, rowH = 32, lblW = 120, editW = 160, btnW = 110, gap = 8;
    int x = m, y = m;
    MoveWindow(g_lblCountdown, x, y, lblW, rowH, TRUE); x += lblW + gap;
    MoveWindow(g_editCountdown, x, y, editW, rowH, TRUE);
    int rx = w - m;
    MoveWindow(g_btnStart, rx - btnW * 4 - gap * 3, y, btnW, rowH, TRUE);
    MoveWindow(g_btnStop, rx - btnW * 3 - gap * 2, y, btnW, rowH, TRUE);
    MoveWindow(g_btnResume, rx - btnW * 2 - gap * 1, y, btnW, rowH, TRUE);
    MoveWindow(g_btnReset, rx - btnW * 1 - 0, y, btnW, rowH, TRUE);
    x = m; y += rowH + gap;
    MoveWindow(g_lblTarget, x, y, lblW, rowH, TRUE); x += lblW + gap;
    MoveWindow(g_editTarget, x, y, editW, rowH, TRUE);
}

static void StartCountdown(void) { ReadInputs(); QueryPerformanceCounter(&g_tCountdownStart); g_mode = MODE_COUNTDOWN; g_pausedAccumMs = 0; g_lastShownMs = g_countdownDurMs; UpdateButtons(); }
static void BeginRunningNow(void) { QueryPerformanceCounter(&g_tStart); g_mode = MODE_RUNNING; g_lastShownMs = 0; UpdateButtons(); }
static void StopRunning(void) {
    if (g_mode == MODE_RUNNING) {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        LONGLONG runMs = DiffMs(now, g_tStart) + g_pausedAccumMs;
        g_pausedAccumMs = runMs; g_lastShownMs = runMs; g_mode = MODE_PAUSED; UpdateButtons();
    }
    else if (g_mode == MODE_COUNTDOWN) {
        LONGLONG passed = NowMs() - (LONGLONG)((g_tCountdownStart.QuadPart * 1000.0) / (double)g_qpf.QuadPart);
        g_countdownDurMs = max(0, g_countdownDurMs - passed); g_lastShownMs = g_countdownDurMs; g_mode = MODE_PAUSED; UpdateButtons();
    }
}
static void ResumeRunning(void) { if (g_mode == MODE_PAUSED) { QueryPerformanceCounter(&g_tStart); g_mode = MODE_RUNNING; UpdateButtons(); } }
static void ResetAll(void) { g_mode = MODE_IDLE; g_pausedAccumMs = 0; g_lastShownMs = 0; UpdateButtons(); }

static BOOL HandleTargetAutoStop(LONGLONG runMs) {
    if (g_targetDurMs > 0 && runMs >= g_targetDurMs) {
        g_pausedAccumMs = g_targetDurMs;
        g_lastShownMs = g_targetDurMs;
        g_mode = MODE_PAUSED;
        UpdateButtons();
        return TRUE;
    }
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd; EnsureUIFont(); QueryPerformanceFrequency(&g_qpf);

        g_lblCountdown = CreateWindowW(L"STATIC", L"Countdown", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_COUNTDOWN, NULL, NULL);
        g_editCountdown = CreateWindowW(L"EDIT", L"5", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_COUNTDOWN, NULL, NULL);
        g_lblTarget = CreateWindowW(L"STATIC", L"Target", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_LBL_TARGET, NULL, NULL);
        g_editTarget = CreateWindowW(L"EDIT", L"00:30.00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT_TARGET, NULL, NULL);
        g_btnStart = CreateWindowW(L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_START, NULL, NULL);
        g_btnStop = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_STOP, NULL, NULL);
        g_btnResume = CreateWindowW(L"BUTTON", L"Resume", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_RESUME, NULL, NULL);
        g_btnReset = CreateWindowW(L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_RESET, NULL, NULL);

        HWND ctrls[] = { g_lblCountdown,g_editCountdown,g_lblTarget,g_editTarget,g_btnStart,g_btnStop,g_btnResume,g_btnReset };
        for (int i = 0; i < (int)(sizeof(ctrls) / sizeof(ctrls[0])); ++i) { SendMessageW(ctrls[i], WM_SETFONT, (WPARAM)g_uiFont, TRUE); }

        SetTimer(hWnd, UI_TIMER_ID, UI_TIMER_MS, NULL);
        DoLayout();
        HDC hdc = GetDC(hWnd); RebuildBigFont(hdc); ReleaseDC(hWnd, hdc);
        UpdateButtons();
    }break;

    case WM_SIZE: {
        DoLayout();
        HDC hdc = GetDC(hWnd); RebuildBigFont(hdc); ReleaseDC(hWnd, hdc);
        InvalidateRect(hWnd, NULL, FALSE); // FALSE -> TRUE되면 배경 지움
    }break;

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_BTN_START:  StartCountdown(); break;
        case IDC_BTN_STOP:   StopRunning();    break;
        case IDC_BTN_RESUME: ResumeRunning();  break;
        case IDC_BTN_RESET:  ResetAll();       break;
        }
    }break;

    case WM_TIMER: {
        if (wParam == UI_TIMER_ID) {
            if (g_mode == MODE_COUNTDOWN) {
                LONGLONG startMs = (LONGLONG)((g_tCountdownStart.QuadPart * 1000.0) / (double)g_qpf.QuadPart);
                LONGLONG passed = NowMs() - startMs;
                if (passed >= g_countdownDurMs) { BeginRunningNow(); }
                else { g_lastShownMs = max(0, g_countdownDurMs - passed); }
            }
            else if (g_mode == MODE_RUNNING) {
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                LONGLONG runMs = DiffMs(now, g_tStart) + g_pausedAccumMs;
                if (HandleTargetAutoStop(runMs)) { InvalidateRect(hWnd, NULL, FALSE); break; }
                g_lastShownMs = runMs;
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }break;

    case WM_ERASEBKGND:
        return 1; // 배경지우기 방지(깜빡임 감소)

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);

        // 더블 버퍼
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        PaintBigDisplay(memDC);

        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp); DeleteObject(memBmp); DeleteDC(memDC);
        EndPaint(hWnd, &ps);
    }break;

    case WM_DESTROY:
        KillTimer(hWnd, UI_TIMER_ID);
        if (g_bigFont) { DeleteObject(g_bigFont); g_bigFont = NULL; }
        if (g_uiFont) { DeleteObject(g_uiFont);  g_uiFont = NULL; }
        PostQuitMessage(0);
        break;

    default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    const WCHAR* CLASS_NAME = L"CFitStopwatchWnd";
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // WM_ERASEBKGND에서 배경 지우기 차단

    if (!RegisterClassW(&wc)) return 0;

    HWND hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Stopwatch for crossfitter (Insta: @linchpin.ff; GitHub: @goldbear564)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, // 자식 영역 보호
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 500,
        NULL, NULL, hInst, NULL);

    if (!hWnd) return 0;

    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
