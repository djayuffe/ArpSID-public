// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: Windows window.
//
// A top-level window with VSTGUI's HWND frame as its only child. The window
// is DPI aware (per monitor): the frame's zoom is the user's size times the
// monitor scale. Resizing keeps the content's aspect ratio.

#include "standalone/arpsid_standalone_app.h"
#include "standalone/arpsid_standalone_platform.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/vstguiinit.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace ArpSID::Standalone {

namespace {

constexpr wchar_t kWindowClass[] = L"ArpSIDStandaloneWindow";
constexpr UINT_PTR kTitleTimer = 1;

struct WindowState {
    App* app = nullptr;
    VSTGUI::CFrame* frame = nullptr;
    double dpiScale = 1.0;
    std::string title;
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

double dpiScaleOf(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    const UINT dpi = (fn && hwnd) ? fn(hwnd) : 96u;
    return dpi > 0 ? dpi / 96.0 : 1.0;
}

void enableDpiAwareness() {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    if (auto fn = reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))) {
        fn(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        return;
    }
    SetProcessDPIAware();
}

// Outer window size for a client area of <w> x <h>.
SIZE windowSizeFor(HWND hwnd, int w, int h) {
    RECT r{0, 0, w, h};
    AdjustWindowRectEx(&r, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
    return SIZE{r.right - r.left, r.bottom - r.top};
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    const double cw = App::contentWidth(), ch = App::contentHeight();
    switch (msg) {
        case WM_SIZE:
            if (st && st->frame && wp != SIZE_MINIMIZED) {
                const int w = LOWORD(lp), h = HIWORD(lp);
                const double z = std::clamp(std::min(w / cw, h / ch), 0.25, 6.0);
                st->frame->setZoom(z);
                st->app->setZoom(z / st->dpiScale);
            }
            return 0;
        case WM_SIZING: {
            // Keep the aspect ratio while the user drags an edge.
            auto* r = reinterpret_cast<RECT*>(lp);
            const SIZE frameExtra = windowSizeFor(hwnd, 0, 0);
            const int w = (r->right - r->left) - frameExtra.cx;
            const int h = (r->bottom - r->top) - frameExtra.cy;
            if (wp == WMSZ_TOP || wp == WMSZ_BOTTOM) {
                r->right = r->left + static_cast<LONG>(std::lround(h * cw / ch)) + frameExtra.cx;
            } else {
                const LONG nh = static_cast<LONG>(std::lround(w * ch / cw)) + frameExtra.cy;
                if (wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) r->top = r->bottom - nh;
                else r->bottom = r->top + nh;
            }
            return TRUE;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            const double s = st ? st->dpiScale : 1.0;
            const SIZE min = windowSizeFor(hwnd, static_cast<int>(cw * 0.5 * s), static_cast<int>(ch * 0.5 * s));
            mm->ptMinTrackSize.x = min.cx;
            mm->ptMinTrackSize.y = min.cy;
            return 0;
        }
        case 0x02E0: { // WM_DPICHANGED: move to the suggested rect for the new scale
            if (st) st->dpiScale = HIWORD(wp) / 96.0;
            const auto* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SETFOCUS:
            // Keys go to the VSTGUI frame (the editor's keyboard).
            if (HWND child = GetWindow(hwnd, GW_CHILD)) SetFocus(child);
            return 0;
        case WM_TIMER:
            if (wp == kTitleTimer && st && st->app) {
                const std::string t = st->app->windowTitle();
                if (t != st->title) {
                    st->title = t;
                    SetWindowTextW(hwnd, widen(t).c_str());
                }
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            // Take the frame down while its window still exists.
            if (st && st->frame) {
                st->app->requestQuit = nullptr;
                st->app->detach();
                st->frame->forget();
                st->frame = nullptr;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTitleTimer);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int runWindow(App& app) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    enableDpiAwareness();
    HINSTANCE inst = GetModuleHandleW(nullptr);
    VSTGUI::init(inst);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    WindowState st;
    st.app = &app;
    st.title = app.windowTitle();
    const DWORD style = WS_OVERLAPPEDWINDOW;
    HWND hwnd = CreateWindowExW(0, kWindowClass, widen(st.title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                1200, 834, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        VSTGUI::exit();
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
    st.dpiScale = dpiScaleOf(hwnd);

    const double cw = App::contentWidth(), ch = App::contentHeight();
    // Fit the saved size on the work area.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    double zoom = app.zoom() * st.dpiScale;
    zoom = std::min(zoom, std::min((work.right - work.left) * 0.95 / cw, (work.bottom - work.top) * 0.9 / ch));
    zoom = std::max(zoom, 0.25);
    const SIZE outer = windowSizeFor(hwnd, static_cast<int>(std::lround(cw * zoom)), static_cast<int>(std::lround(ch * zoom)));
    SetWindowPos(hwnd, nullptr, 0, 0, outer.cx, outer.cy, SWP_NOMOVE | SWP_NOZORDER);

    auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, cw, ch), nullptr);
    frame->setTransparency(false);
    if (!frame->open(hwnd, VSTGUI::PlatformType::kHWND)) {
        frame->forget();
        DestroyWindow(hwnd);
        VSTGUI::exit();
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
    st.frame = frame;
    app.attach(frame);
    frame->setZoom(zoom);
    app.requestQuit = [hwnd]() { PostMessageW(hwnd, WM_CLOSE, 0, 0); };
    SetTimer(hwnd, kTitleTimer, 500, nullptr);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (st.frame) {
        app.requestQuit = nullptr;
        app.detach();
        st.frame->forget();
        st.frame = nullptr;
    }
    VSTGUI::exit();
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

} // namespace ArpSID::Standalone
