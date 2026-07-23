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
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    // Keep click-through; GLFW may set layered for transparency already.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void force_topmost(void* hwnd_void) {
    HWND hwnd = static_cast<HWND>(hwnd_void);
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

