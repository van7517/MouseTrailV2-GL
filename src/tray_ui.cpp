#include "tray_ui.hpp"
#include <CommCtrl.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <string.h>
#include <vector>
#pragma comment(lib, "comctl32.lib")

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT ID_TRAY = 1;
constexpr UINT IDM_TOGGLE = 1001;
constexpr UINT IDM_SETTINGS = 1002;
constexpr UINT IDM_EXIT = 1003;

constexpr int IDC_OPACITY = 2001;
constexpr int IDC_THICK = 2002;
constexpr int IDC_SCALE = 2003;
constexpr int IDC_LIFE = 2004;
constexpr int IDC_PARTICLES = 2005;
constexpr int IDC_HIDE_CUR = 2006;
constexpr int IDC_BLACKLIST = 2007;
constexpr int IDC_APPLY = 2008;
constexpr int IDC_CLOSE = 2009;
constexpr int IDC_OPACITY_L = 2010;
constexpr int IDC_THICK_L = 2011;
constexpr int IDC_SCALE_L = 2012;
constexpr int IDC_LIFE_L = 2013;
constexpr int IDC_PROCLIST = 2014;
constexpr int IDC_ADD_PROC = 2015;
constexpr int IDC_REFRESH_PROC = 2016;
constexpr int IDC_AUTOSTART = 2017;
constexpr int IDC_TITLE = 2018;

AppControl* g_ctrl = nullptr;
NOTIFYICONDATAA g_nid{};
bool g_tray_ok = false;
HWND g_cfg_hwnd = nullptr;
HINSTANCE g_inst = nullptr;
HFONT g_font = nullptr;
HFONT g_font_title = nullptr;
std::vector<std::string> g_list_exes;

std::string exe_path() {
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;
}

bool is_autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    wchar_t val[MAX_PATH]{};
    DWORD type = 0, size = sizeof(val);
    const LONG r = RegQueryValueExW(key, L"MouseTrailV2", nullptr, &type, (LPBYTE)val, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_SZ && val[0] != 0;
}

void set_autostart(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    if (enable) {
        std::string path = exe_path();
        // quote path
        std::string q = "\"" + path + "\"";
        wchar_t w[MAX_PATH + 4]{};
        MultiByteToWideChar(CP_UTF8, 0, q.c_str(), -1, w, MAX_PATH + 4);
        RegSetValueExW(key, L"MouseTrailV2", 0, REG_SZ, (const BYTE*)w, (DWORD)((wcslen(w) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"MouseTrailV2");
    }
    RegCloseKey(key);
}

bool is_taskbar_window(HWND hwnd) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
    const LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    BOOL cloaked = FALSE;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        typedef HRESULT (WINAPI *Fn)(HWND, DWORD, PVOID, DWORD);
        auto fn = (Fn)GetProcAddress(dwm, "DwmGetWindowAttribute");
        if (fn) fn(hwnd, 14, &cloaked, sizeof(cloaked));
        FreeLibrary(dwm);
    }
    if (cloaked) return false;
    wchar_t title[2]{};
    GetWindowTextW(hwnd, title, 2);
    if (title[0] == 0 && !(ex & WS_EX_APPWINDOW)) {
        const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (!(style & WS_CAPTION) && !(style & WS_MINIMIZEBOX)) return false;
    }
    return true;
}

std::string process_name_from_pid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::string result;
    if (QueryFullProcessImageNameW(h, 0, path, &size)) {
        const wchar_t* base = path;
        for (const wchar_t* p = path; *p; ++p) if (*p == L'\\' || *p == L'/') base = p + 1;
        char name[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, base, -1, name, MAX_PATH, nullptr, nullptr);
        result = name;
    }
    CloseHandle(h);
    return result;
}

struct TaskItem { std::string exe; std::string label; };

std::vector<TaskItem> enumerate_taskbar_processes() {
    std::vector<TaskItem> items;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* out = reinterpret_cast<std::vector<TaskItem>*>(lp);
        if (!is_taskbar_window(hwnd)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return TRUE;
        std::string exe = process_name_from_pid(pid);
        if (exe.empty()) return TRUE;
        for (const auto& it : *out) if (_stricmp(it.exe.c_str(), exe.c_str()) == 0) return TRUE;
        wchar_t title_w[256]{};
        GetWindowTextW(hwnd, title_w, 256);
        char title[256]{};
        WideCharToMultiByte(CP_UTF8, 0, title_w, -1, title, 256, nullptr, nullptr);
        TaskItem item;
        item.exe = exe;
        item.label = title[0] ? (std::string(title) + "  -  " + exe) : exe;
        if (item.label.size() > 100) item.label = item.label.substr(0, 97) + "...";
        out->push_back(std::move(item));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&items));
    std::sort(items.begin(), items.end(), [](const TaskItem& a, const TaskItem& b) {
        return _stricmp(a.exe.c_str(), b.exe.c_str()) < 0;
    });
    return items;
}

void fill_process_list(HWND hwnd) {
    HWND lb = GetDlgItem(hwnd, IDC_PROCLIST);
    if (!lb) return;
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    g_list_exes.clear();
    for (const auto& it : enumerate_taskbar_processes()) {
        wchar_t w[512]{};
        MultiByteToWideChar(CP_UTF8, 0, it.label.c_str(), -1, w, 512);
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)w);
        g_list_exes.push_back(it.exe);
    }
}

void add_selected_process_to_blacklist(HWND hwnd) {
    HWND lb = GetDlgItem(hwnd, IDC_PROCLIST);
    if (!lb) return;
    int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_list_exes.size()) return;
    const std::string name = g_list_exes[sel];
    if (name.empty()) return;
    wchar_t cur[1024]{};
    GetDlgItemTextW(hwnd, IDC_BLACKLIST, cur, 1024);
    char cur_a[1024]{};
    WideCharToMultiByte(CP_UTF8, 0, cur, -1, cur_a, 1024, nullptr, nullptr);
    std::string s = cur_a;
    std::string ln = name, ls = s;
    for (char& c : ln) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    for (char& c : ls) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (ls.find(ln) != std::string::npos) return;
    if (!s.empty() && s.back() != ';' && s.back() != ',') s += ";";
    s += name;
    wchar_t out[1024]{};
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out, 1024);
    SetDlgItemTextW(hwnd, IDC_BLACKLIST, out);
}

void update_tray_tip() {
    if (!g_tray_ok || !g_ctrl || !g_ctrl->enabled) return;
    std::snprintf(g_nid.szTip, sizeof(g_nid.szTip), "MouseTrailV2 - %s", (*g_ctrl->enabled) ? "运行中" : "已暂停");
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

void apply_font(HWND hwnd) {
    if (!g_font) return;
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)g_font);
}

void read_dialog_to_cfg(HWND hwnd, AppConfig& cfg) {
    cfg.opacity = ((int)SendDlgItemMessageW(hwnd, IDC_OPACITY, TBM_GETPOS, 0, 0)) / 100.0f;
    cfg.ribbon_thickness = (float)(int)SendDlgItemMessageW(hwnd, IDC_THICK, TBM_GETPOS, 0, 0);
    cfg.stroke_scale = (float)(int)SendDlgItemMessageW(hwnd, IDC_SCALE, TBM_GETPOS, 0, 0);
    if (cfg.stroke_scale < 1.f) cfg.stroke_scale = 1.f;
    if (cfg.stroke_scale > 10.f) cfg.stroke_scale = 10.f;
    cfg.trail_lifetime_ms = (int)SendDlgItemMessageW(hwnd, IDC_LIFE, TBM_GETPOS, 0, 0);
    cfg.triangle_particles = IsDlgButtonChecked(hwnd, IDC_PARTICLES) == BST_CHECKED;
    cfg.not_when_cursor_hidden = IsDlgButtonChecked(hwnd, IDC_HIDE_CUR) == BST_CHECKED;
    set_autostart(IsDlgButtonChecked(hwnd, IDC_AUTOSTART) == BST_CHECKED);

    wchar_t wbuf[1024]{}; GetDlgItemTextW(hwnd, IDC_BLACKLIST, wbuf, 1024);
    char buf[1024]{}; WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, 1024, nullptr, nullptr);
    cfg.blacklisted_processes.clear();
    std::string s = buf; size_t start = 0;
    while (start < s.size()) {
        size_t comma = s.find_first_of(";,", start);
        if (comma == std::string::npos) comma = s.size();
        std::string item = s.substr(start, comma - start);
        while (!item.empty() && (item.front()==' '||item.front()=='\t')) item.erase(item.begin());
        while (!item.empty() && (item.back()==' '||item.back()=='\t')) item.pop_back();
        if (!item.empty()) cfg.blacklisted_processes.push_back(item);
        start = comma + 1;
    }
    if (cfg.blacklisted_processes.empty()) cfg.blacklisted_processes.push_back("javaw.exe");
}

void refresh_labels(HWND hwnd) {
    int op=(int)SendDlgItemMessageW(hwnd,IDC_OPACITY,TBM_GETPOS,0,0);
    int th=(int)SendDlgItemMessageW(hwnd,IDC_THICK,TBM_GETPOS,0,0);
    int sc=(int)SendDlgItemMessageW(hwnd,IDC_SCALE,TBM_GETPOS,0,0);
    int life=(int)SendDlgItemMessageW(hwnd,IDC_LIFE,TBM_GETPOS,0,0);
    wchar_t b[128];
    swprintf(b,128,L"透明度  %d%%",op); SetDlgItemTextW(hwnd,IDC_OPACITY_L,b);
    swprintf(b,128,L"粗细基准  %d",th); SetDlgItemTextW(hwnd,IDC_THICK_L,b);
    swprintf(b,128,L"缩放  %d",sc); SetDlgItemTextW(hwnd,IDC_SCALE_L,b);
    swprintf(b,128,L"残留时间  %d ms",life); SetDlgItemTextW(hwnd,IDC_LIFE_L,b);
}

LRESULT CALLBACK CfgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(32, 32, 32));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return 1;
    }
    case WM_HSCROLL: if (g_cfg_hwnd) refresh_labels(g_cfg_hwnd); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_REFRESH_PROC) { fill_process_list(hwnd); return 0; }
        if (id == IDC_ADD_PROC) { add_selected_process_to_blacklist(hwnd); return 0; }
        if (id == IDC_PROCLIST && HIWORD(wParam) == LBN_DBLCLK) { add_selected_process_to_blacklist(hwnd); return 0; }
        if (id == IDC_APPLY || id == IDC_CLOSE) {
            if (g_ctrl && g_ctrl->cfg) {
                read_dialog_to_cfg(hwnd, *g_ctrl->cfg);
                g_ctrl->cfg->save(g_ctrl->cfg_path);
                if (g_ctrl->on_config_saved) g_ctrl->on_config_saved();
                update_tray_tip();
            }
            if (id == IDC_CLOSE) DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_cfg_hwnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND make_label(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND ctl = CreateWindowW(L"STATIC", text, WS_CHILD|WS_VISIBLE, x,y,w,h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    if (g_font) SendMessageW(ctl, WM_SETFONT, (WPARAM)g_font, TRUE);
    return ctl;
}
HWND make_track(HWND parent, int id, int x, int y, int w, int minv, int maxv, int pos) {
    HWND t = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS|TBS_TOOLTIPS, x,y,w,36, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(minv, maxv));
    SendMessageW(t, TBM_SETPOS, TRUE, pos);
    if (g_font) SendMessageW(t, WM_SETFONT, (WPARAM)g_font, TRUE);
    return t;
}
HWND make_btn(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, bool primary=false) {
    DWORD style = WS_CHILD|WS_VISIBLE|(primary?BS_DEFPUSHBUTTON:BS_PUSHBUTTON);
    HWND b = CreateWindowW(L"BUTTON", text, style, x,y,w,h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    if (g_font) SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    return b;
}

void build_config_ui(HWND hwnd, const AppConfig& cfg) {
    if (!g_font) {
        g_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_font_title = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
    }

    int y = 20;
    HWND title = CreateWindowW(L"STATIC", L"轨迹设置", WS_CHILD|WS_VISIBLE, 28, y, 300, 28, hwnd, (HMENU)IDC_TITLE, g_inst, nullptr);
    if (g_font_title) SendMessageW(title, WM_SETFONT, (WPARAM)g_font_title, TRUE);
    y += 40;

    make_label(hwnd,IDC_OPACITY_L,L"透明度",28,y,260,22); y+=24;
    make_track(hwnd,IDC_OPACITY,28,y,500,5,100,(int)(cfg.opacity*100)); y+=48;
    make_label(hwnd,IDC_THICK_L,L"粗细基准",28,y,260,22); y+=24;
    make_track(hwnd,IDC_THICK,28,y,500,1,30,(int)cfg.ribbon_thickness); y+=48;
    make_label(hwnd,IDC_SCALE_L,L"缩放",28,y,260,22); y+=24;
    int sc = (int)std::lround(cfg.stroke_scale); if (sc<1) sc=1; if (sc>10) sc=10;
    make_track(hwnd,IDC_SCALE,28,y,500,1,10,sc); y+=48;
    make_label(hwnd,IDC_LIFE_L,L"残留时间",28,y,260,22); y+=24;
    make_track(hwnd,IDC_LIFE,28,y,500,100,2000,cfg.trail_lifetime_ms); y+=52;

    HWND c1 = CreateWindowW(L"BUTTON",L"三角形粒子",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,28,y,200,26,hwnd,(HMENU)IDC_PARTICLES,g_inst,nullptr);
    CheckDlgButton(hwnd,IDC_PARTICLES,cfg.triangle_particles?BST_CHECKED:BST_UNCHECKED);
    if (g_font) SendMessageW(c1, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 32;
    HWND c2 = CreateWindowW(L"BUTTON",L"光标隐藏时禁用",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,28,y,220,26,hwnd,(HMENU)IDC_HIDE_CUR,g_inst,nullptr);
    CheckDlgButton(hwnd,IDC_HIDE_CUR,cfg.not_when_cursor_hidden?BST_CHECKED:BST_UNCHECKED);
    if (g_font) SendMessageW(c2, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 32;
    HWND c3 = CreateWindowW(L"BUTTON",L"开机自启",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,28,y,200,26,hwnd,(HMENU)IDC_AUTOSTART,g_inst,nullptr);
    CheckDlgButton(hwnd,IDC_AUTOSTART,is_autostart_enabled()?BST_CHECKED:BST_UNCHECKED);
    if (g_font) SendMessageW(c3, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 40;

    make_label(hwnd,0,L"黑名单（点选任务栏窗口进程，或手动编辑）",28,y,480,22); y+=24;
    std::string bl; for(size_t i=0;i<cfg.blacklisted_processes.size();++i){ if(i) bl+=";"; bl+=cfg.blacklisted_processes[i]; }
    wchar_t wbl[1024]{}; MultiByteToWideChar(CP_UTF8,0,bl.c_str(),-1,wbl,1024);
    HWND ed = CreateWindowW(L"EDIT",wbl,WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,28,y,500,30,hwnd,(HMENU)IDC_BLACKLIST,g_inst,nullptr);
    if (g_font) SendMessageW(ed, WM_SETFONT, (WPARAM)g_font, TRUE);
    y += 40;

    make_label(hwnd,0,L"任务栏窗口进程（双击添加）",28,y,400,22); y+=24;
    HWND lb = CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,28,y,500,200,hwnd,(HMENU)IDC_PROCLIST,g_inst,nullptr);
    if (g_font) SendMessageW(lb, WM_SETFONT, (WPARAM)g_font, TRUE);
    make_btn(hwnd,IDC_ADD_PROC,L"添加",548,y,110,34);
    make_btn(hwnd,IDC_REFRESH_PROC,L"刷新",548,y+44,110,34);
    y += 220;

    make_btn(hwnd,IDC_APPLY,L"应用",380,y,120,40,true);
    make_btn(hwnd,IDC_CLOSE,L"关闭",520,y,120,40,false);

    fill_process_list(hwnd);
    refresh_labels(hwnd);
    apply_font(hwnd);
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAY) {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) { tray_show_menu(hwnd); return 0; }
        if (lParam == WM_LBUTTONDBLCLK) { if (g_inst && g_ctrl) config_ui_open(g_inst, hwnd, g_ctrl); return 0; }
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        if (id == IDM_TOGGLE && g_ctrl && g_ctrl->enabled) { *g_ctrl->enabled = !*g_ctrl->enabled; update_tray_tip(); return 0; }
        if (id == IDM_SETTINGS && g_inst && g_ctrl) { config_ui_open(g_inst, hwnd, g_ctrl); return 0; }
        if (id == IDM_EXIT && g_ctrl && g_ctrl->running) { *g_ctrl->running = false; PostQuitMessage(0); return 0; }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
} // namespace

bool tray_init(HINSTANCE inst, AppControl* ctrl) {
    g_inst = inst; g_ctrl = ctrl;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=TrayWndProc; wc.hInstance=inst; wc.lpszClassName=L"MouseTrailV2Tray";
    RegisterClassExW(&wc);
    HWND tray_hwnd = CreateWindowExW(0,L"MouseTrailV2Tray",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,inst,nullptr);
    if (!tray_hwnd) return false;
    ZeroMemory(&g_nid,sizeof(g_nid));
    g_nid.cbSize=sizeof(g_nid); g_nid.hWnd=tray_hwnd; g_nid.uID=ID_TRAY;
    g_nid.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; g_nid.uCallbackMessage=WM_TRAY;
    g_nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    std::snprintf(g_nid.szTip,sizeof(g_nid.szTip),"MouseTrailV2 - 运行中");
    g_tray_ok = Shell_NotifyIconA(NIM_ADD,&g_nid)==TRUE;
    return g_tray_ok;
}
void tray_shutdown() {
    if (g_tray_ok) { Shell_NotifyIconA(NIM_DELETE,&g_nid); g_tray_ok=false; }
    if (g_cfg_hwnd) { DestroyWindow(g_cfg_hwnd); g_cfg_hwnd=nullptr; }
}
void tray_show_menu(HWND owner) {
    POINT pt; GetCursorPos(&pt);
    HMENU menu=CreatePopupMenu();
    bool on = g_ctrl && g_ctrl->enabled && *g_ctrl->enabled;
    AppendMenuW(menu,MF_STRING,IDM_TOGGLE,on?L"暂停轨迹":L"继续轨迹");
    AppendMenuW(menu,MF_STRING,IDM_SETTINGS,L"设置...");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,IDM_EXIT,L"退出");
    SetForegroundWindow(owner);
    TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,pt.x,pt.y,0,owner,nullptr);
    DestroyMenu(menu); PostMessageW(owner,WM_NULL,0,0);
}
void tray_process_message(HWND,UINT,WPARAM,LPARAM) {}
bool config_ui_is_open() { return g_cfg_hwnd != nullptr; }
void config_ui_open(HINSTANCE inst, HWND parent, AppControl* ctrl) {
    if (g_cfg_hwnd) { SetForegroundWindow(g_cfg_hwnd); return; }
    g_inst=inst; g_ctrl=ctrl;
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=CfgWndProc; wc.hInstance=inst;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName=L"MouseTrailV2Config"; RegisterClassExW(&wc);
    g_cfg_hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"MouseTrailV2Config",L"MouseTrailV2 设置",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,700,860,parent,nullptr,inst,nullptr);
    if (!g_cfg_hwnd) return;
    build_config_ui(g_cfg_hwnd,*ctrl->cfg);
    ShowWindow(g_cfg_hwnd,SW_SHOW); UpdateWindow(g_cfg_hwnd);
}