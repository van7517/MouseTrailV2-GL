#include "platform_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cctype>

void enable_dpi_awareness() {
    HMODULE user32 = LoadLibraryW(L"User32.dll");
    if (user32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContext_t)(HANDLE);
        auto fn = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4
            if (fn(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) {
                FreeLibrary(user32);
                return;
            }
        }
        FreeLibrary(user32);
    }

    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        typedef HRESULT(WINAPI* SetProcessDpiAwareness_t)(int);
        auto fn = reinterpret_cast<SetProcessDpiAwareness_t>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn) {
            fn(2); // PROCESS_PER_MONITOR_DPI_AWARE
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }
    SetProcessDPIAware();
}

VirtualScreen get_virtual_screen() {
    VirtualScreen s;
    s.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    s.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    s.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (s.w <= 0 || s.h <= 0) {
        s.x = 0;
        s.y = 0;
        s.w = GetSystemMetrics(SM_CXSCREEN);
        s.h = GetSystemMetrics(SM_CYSCREEN);
    }
    return s;
}

static BOOL CALLBACK monitor_enum_proc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<MonitorRect>*>(lParam);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;
    MonitorRect r;
    r.x = mi.rcMonitor.left;
    r.y = mi.rcMonitor.top;
    r.w = mi.rcMonitor.right - mi.rcMonitor.left;
    r.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (r.w > 0 && r.h > 0) list->push_back(r);
    return TRUE;
}

std::vector<MonitorRect> get_monitors() {
    std::vector<MonitorRect> list;
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc, reinterpret_cast<LPARAM>(&list));
    if (list.empty()) {
        const VirtualScreen vs = get_virtual_screen();
        list.push_back({vs.x, vs.y, vs.w, vs.h});
    }
    std::sort(list.begin(), list.end(), [](const MonitorRect& a, const MonitorRect& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        if (a.w != b.w) return a.w < b.w;
        return a.h < b.h;
    });
    return list;
}

CursorState get_cursor_state() {
    CursorState c;
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci)) {
        c.x = ci.ptScreenPos.x;
        c.y = ci.ptScreenPos.y;
        c.visible = (ci.flags & CURSOR_SHOWING) != 0;
        c.ok = true;
        return c;
    }
    POINT pt{};
    if (GetCursorPos(&pt)) {
        c.x = pt.x;
        c.y = pt.y;
        c.visible = true;
        c.ok = true;
    }
    return c;
}

bool is_key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

std::string get_foreground_process_name() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[1024];
    DWORD size = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    std::string result;
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        std::wstring w(buf);
        size_t pos = w.find_last_of(L"\\/");
        std::wstring leaf = (pos == std::wstring::npos) ? w : w.substr(pos + 1);
        result.resize(leaf.size());
        for (size_t i = 0; i < leaf.size(); ++i)
            result[i] = static_cast<char>(leaf[i] < 128 ? leaf[i] : '?');
    }
    CloseHandle(h);
    return result;
}

static std::string to_lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool process_is_blacklisted(const std::string& name, const std::vector<std::string>& list) {
    if (name.empty()) return false;
    std::string n = to_lower(name);
    for (const auto& item : list) {
        if (item.empty()) continue;
        if (to_lower(item) == n) return true;
    }
    return false;
}

void apply_overlay_styles(void* hwnd_void) {
    HWND hwnd = static_cast<HWND>(hwnd_void);
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
    ex &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void force_topmost(void* hwnd_void) {
    HWND hwnd = static_cast<HWND>(hwnd_void);
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void enable_window_transparency(void* hwnd_void) {
    // Pixels come from UpdateLayeredWindow in main.cpp. Keep layered/click-through styles only.
    apply_overlay_styles(hwnd_void);
}
