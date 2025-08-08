// main2.c - Tiny Win32 "Paint" (C99, Unicode, no shell32)
// Build with your script exactly:
// clang -std=c99 -g -Wall main2.c -o window.exe -luser32 -lgdi32

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>      // swprintf

static const wchar_t *CLASS_NAME = L"MiniPaintClass";

typedef struct {
    HBITMAP dib;
    void*   pixels;
    int     width;
    int     height;
    int     stride;   // bytes per row
    int     bpp;      // 32
} BackBuffer;

static BackBuffer g_bb = (BackBuffer){0};
static BOOL g_drawing = FALSE;
static BOOL g_erasing = FALSE;
static POINT g_last = {0};
static int g_brush = 6; // radius
static COLORREF g_color = RGB(30,144,255); // DodgerBlue

static const COLORREF PALETTE[] = {
    RGB( 30,144,255), // 1 blue
    RGB( 46,204,113), // 2 green
    RGB(231, 76, 60), // 3 red
    RGB(241,196, 15), // 4 yellow
    RGB(155, 89,182), // 5 purple
    RGB(236,240,241)  // 6 white
};
enum { PALETTE_N = (int)(sizeof(PALETTE)/sizeof(PALETTE[0])) };

static void UpdateTitle(HWND hwnd) {
    int idx = 1;
    for (int i=0;i<PALETTE_N;i++) if (PALETTE[i]==g_color) { idx=i+1; break; }
    wchar_t title[160];
    swprintf(title, 160, L"MiniPaint — Brush %d | Color #%d | LMB=draw RMB=erase | C=clear S=save (cwd)",
             g_brush, idx);
    SetWindowTextW(hwnd, title);
}

static void FreeBackBuffer(void) {
    if (g_bb.dib) { DeleteObject(g_bb.dib); g_bb.dib = NULL; }
    g_bb.pixels = NULL; g_bb.width = g_bb.height = g_bb.stride = g_bb.bpp = 0;
}

static BOOL CreateBackBuffer(HDC hdc, int w, int h) {
    FreeBackBuffer();
    if (w<=0 || h<=0) return FALSE;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;  // top-down DIB
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;  // BGRA
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) { if (dib) DeleteObject(dib); return FALSE; }

    g_bb.dib = dib;
    g_bb.pixels = bits;
    g_bb.width = w;
    g_bb.height = h;
    g_bb.bpp = 32;
    g_bb.stride = w * 4;

    // clear to white
    uint32_t* p = (uint32_t*)g_bb.pixels;
    uint32_t bg = 0x00FFFFFF;
    int count = w*h;
    for (int i=0;i<count;i++) p[i] = bg;

    return TRUE;
}

static inline void PutPixelSafe(int x, int y, uint32_t color) {
    if ((unsigned)x >= (unsigned)g_bb.width) return;
    if ((unsigned)y >= (unsigned)g_bb.height) return;
    uint8_t* row = (uint8_t*)g_bb.pixels + y * g_bb.stride;
    uint32_t* px = (uint32_t*)(row + x*4);
    *px = color;
}

static void DrawDisc(int cx, int cy, int radius, COLORREF c) {
    // COLORREF is 0x00BBGGRR; convert to BGRA 0x00RRGGBB
    uint32_t col = ((c & 0x0000FF) << 16) | (c & 0x00FF00) | ((c & 0xFF0000) >> 16);
    int r2 = radius*radius;
    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0<0) x0=0; if (y0<0) y0=0;
    if (x1>g_bb.width-1)  x1=g_bb.width-1;
    if (y1>g_bb.height-1) y1=g_bb.height-1;
    for (int y=y0; y<=y1; ++y) {
        int dy = y - cy;
        for (int x=x0; x<=x1; ++x) {
            int dx = x - cx;
            if (dx*dx + dy*dy <= r2) PutPixelSafe(x, y, col);
        }
    }
}

static void DrawLineDisc(POINT a, POINT b, int radius, COLORREF col) {
    int dx = b.x - a.x, dy = b.y - a.y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;
    if (steps <= 0) { DrawDisc(a.x, a.y, radius, col); return; }
    for (int i=0;i<=steps;i++) {
        int x = a.x + (dx*i)/steps;
        int y = a.y + (dy*i)/steps;
        DrawDisc(x, y, radius, col);
    }
}

static void ClearCanvas(void) {
    if (!g_bb.pixels) return;
    uint32_t* p = (uint32_t*)g_bb.pixels;
    uint32_t bg = 0x00FFFFFF;
    int count = g_bb.width * g_bb.height;
    for (int i=0;i<count;i++) p[i] = bg;
}

static BOOL SaveBMP(const wchar_t* path) {
    if (!g_bb.pixels || g_bb.bpp!=32) return FALSE;

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    ZeroMemory(&bfh, sizeof(bfh));
    ZeroMemory(&bih, sizeof(bih));

    int rowBytes = g_bb.width * 4;
    int imgSize = rowBytes * g_bb.height;

    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + imgSize;

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = g_bb.width;
    bih.biHeight = g_bb.height; // bottom-up in file
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = imgSize;

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written;
    BOOL ok = WriteFile(h, &bfh, sizeof(bfh), &written, NULL) &&
              WriteFile(h, &bih, sizeof(bih), &written, NULL);

    if (ok) {
        for (int y = g_bb.height-1; y >= 0; --y) {
            uint8_t* row = (uint8_t*)g_bb.pixels + y * g_bb.stride;
            if (!WriteFile(h, row, rowBytes, &written, NULL)) { ok = FALSE; break; }
        }
    }

    CloseHandle(h);
    return ok;
}

static void SaveToCwd(HWND hwnd) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH,
             L"MiniPaint_%04d-%02d-%02d_%02d-%02d-%02d.bmp",
             (int)st.wYear, (int)st.wMonth, (int)st.wDay,
             (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    if (SaveBMP(path)) {
        wchar_t msg[256];
        swprintf(msg, 256, L"Saved in current folder:\n%s", path);
        MessageBoxW(hwnd, msg, L"MiniPaint", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd, L"Save failed.", L"MiniPaint", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        RECT rc; GetClientRect(hwnd, &rc);
        CreateBackBuffer(hdc, rc.right - rc.left, rc.bottom - rc.top);
        ReleaseDC(hwnd, hdc);
        UpdateTitle(hwnd);
        return 0;
    }
    case WM_SIZE: {
        int w = (int)LOWORD(lParam), h = (int)HIWORD(lParam);
        HDC hdc = GetDC(hwnd);
        CreateBackBuffer(hdc, w, h);
        ReleaseDC(hwnd, hdc);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_drawing = TRUE; g_erasing = FALSE; SetCapture(hwnd);
        g_last.x = GET_X_LPARAM(lParam);
        g_last.y = GET_Y_LPARAM(lParam);
        DrawDisc(g_last.x, g_last.y, g_brush, g_color);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_RBUTTONDOWN:
        g_drawing = TRUE; g_erasing = TRUE; SetCapture(hwnd);
        g_last.x = GET_X_LPARAM(lParam);
        g_last.y = GET_Y_LPARAM(lParam);
        DrawDisc(g_last.x, g_last.y, g_brush, RGB(255,255,255));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (g_drawing) {
            POINT p; p.x = GET_X_LPARAM(lParam); p.y = GET_Y_LPARAM(lParam);
            DrawLineDisc(g_last, p, g_brush, g_erasing ? RGB(255,255,255) : g_color);
            g_last = p;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        g_drawing = FALSE; g_erasing = FALSE; ReleaseCapture();
        return 0;

    case WM_KEYDOWN:
        switch ((int)wParam) {
        case 'C': ClearCanvas(); InvalidateRect(hwnd, NULL, FALSE); break;
        case 'S': SaveToCwd(hwnd); break;
        case VK_OEM_PLUS: case VK_ADD:
            if (g_brush < 128) g_brush++; UpdateTitle(hwnd); break;
        case VK_OEM_MINUS: case VK_SUBTRACT:
            if (g_brush > 1) g_brush--; UpdateTitle(hwnd); break;
        case '1': case '2': case '3': case '4': case '5': case '6': {
            int idx = ((int)wParam) - '1';
            if (idx>=0 && idx<PALETTE_N) { g_color = PALETTE[idx]; UpdateTitle(hwnd); }
        } break;
        default: break;
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, g_bb.dib);
        BitBlt(hdc, 0, 0, g_bb.width, g_bb.height, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        FreeBackBuffer();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; // silence unused

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&wc)) return (int)GetLastError();

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = {0,0,960,600};
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"MiniPaint", style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right-rc.left, rc.bottom-rc.top,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return (int)GetLastError();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    for (;;) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        else if (r == 0) break;
        else return (int)GetLastError();
    }
    return (int)msg.wParam;
}
