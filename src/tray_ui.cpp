#include "tray_ui.hpp"
#include <CommCtrl.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
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
constexpr int IDC_BL_HINT = 2017;
AppControl* g_ctrl = nullptr;
NOTIFYICONDATAA g_nid{};
bool g_tray_ok = false;
HWND g_cfg_hwnd = nullptr;
HINSTANCE g_inst = nullptr;

std::vector<std::string> enumerate_processes() {
    std::set<std::string> names;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            char name[MAX_PATH]{};
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, MAX_PATH, nullptr, nullptr);
            if (name[0]) names.insert(name);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return std::vector<std::string>(names.begin(), names.end());
}

void fill_process_list(HWND hwnd) {
    HWND lb = GetDlgItem(hwnd, IDC_PROCLIST);
    if (!lb) return;
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    auto procs = enumerate_processes();
    for (const auto& p : procs) {
        wchar_t w[MAX_PATH]{};
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w, MAX_PATH);
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)w);
    }
}

void add_selected_process_to_blacklist(HWND hwnd) {
    HWND lb = GetDlgItem(hwnd, IDC_PROCLIST);
    if (!lb) return;
    int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    wchar_t w[MAX_PATH]{};
    SendMessageW(lb, LB_GETTEXT, sel, (LPARAM)w);
    char name[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, name, MAX_PATH, nullptr, nullptr);
    if (!name[0]) return;

    wchar_t cur[1024]{};
    GetDlgItemTextW(hwnd, IDC_BLACKLIST, cur, 1024);
    char cur_a[1024]{};
    WideCharToMultiByte(CP_UTF8, 0, cur, -1, cur_a, 1024, nullptr, nullptr);
    std::string s = cur_a;
    // already present?
    std::string lower_name = name;
    for (char& c : lower_name) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    std::string lower_s = s;
    for (char& c : lower_s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (lower_s.find(lower_name) != std::string::npos) return;

    if (!s.empty() && s.back() != ';' && s.back() != ',') s += ";";
    s += name;
    wchar_t out[1024]{};
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out, 1024);
    SetDlgItemTextW(hwnd, IDC_BLACKLIST, out);
}

void update_tray_tip() {
    if (!g_tray_ok || !g_ctrl || !g_ctrl->enabled) return;
    std::snprintf(g_nid.szTip, sizeof(g_nid.szTip), "MouseTrailV2 - %s", (*g_ctrl->enabled) ? "Running" : "Paused");
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}
void read_dialog_to_cfg(HWND hwnd, AppConfig& cfg) {
    cfg.opacity = ((int)SendDlgItemMessageW(hwnd, IDC_OPACITY, TBM_GETPOS, 0, 0)) / 100.0f;
    cfg.ribbon_thickness = (float)(int)SendDlgItemMessageW(hwnd, IDC_THICK, TBM_GETPOS, 0, 0);
    cfg.stroke_scale = ((int)SendDlgItemMessageW(hwnd, IDC_SCALE, TBM_GETPOS, 0, 0)) / 100.0f;
    cfg.trail_lifetime_ms = (int)SendDlgItemMessageW(hwnd, IDC_LIFE, TBM_GETPOS, 0, 0);
    cfg.triangle_particles = IsDlgButtonChecked(hwnd, IDC_PARTICLES) == BST_CHECKED;
    cfg.not_when_cursor_hidden = IsDlgButtonChecked(hwnd, IDC_HIDE_CUR) == BST_CHECKED;
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
    swprintf(b,128,L"Opacity: %d%%",op); SetDlgItemTextW(hwnd,IDC_OPACITY_L,b);
    swprintf(b,128,L"Thickness: %d",th); SetDlgItemTextW(hwnd,IDC_THICK_L,b);
    swprintf(b,128,L"Scale: %.2f",sc/100.0); SetDlgItemTextW(hwnd,IDC_SCALE_L,b);
    swprintf(b,128,L"Life: %d ms",life); SetDlgItemTextW(hwnd,IDC_LIFE_L,b);
}
LRESULT CALLBACK CfgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
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
    return CreateWindowW(L"STATIC", text, WS_CHILD|WS_VISIBLE, x,y,w,h, parent, (HMENU)(intptr_t)id, g_inst, nullptr);
}
HWND make_track(HWND parent, int id, int x, int y, int w, int minv, int maxv, int pos) {
    HWND t = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS, x,y,w,30, parent, (HMENU)(intptr_t)id, g_inst, nullptr);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(minv, maxv));
    SendMessageW(t, TBM_SETPOS, TRUE, pos);
    return t;
}
void build_config_ui(HWND hwnd, const AppConfig& cfg) {
    int y=12;
    make_label(hwnd,IDC_OPACITY_L,L"Opacity",16,y,180,18); y+=18;
    make_track(hwnd,IDC_OPACITY,16,y,250,5,100,(int)(cfg.opacity*100)); y+=34;
    make_label(hwnd,IDC_THICK_L,L"Thickness",16,y,180,18); y+=18;
    make_track(hwnd,IDC_THICK,16,y,250,1,30,(int)cfg.ribbon_thickness); y+=34;
    make_label(hwnd,IDC_SCALE_L,L"Scale",16,y,180,18); y+=18;
    make_track(hwnd,IDC_SCALE,16,y,250,10,500,(int)(cfg.stroke_scale*100)); y+=34;
    make_label(hwnd,IDC_LIFE_L,L"Life ms",16,y,180,18); y+=18;
    make_track(hwnd,IDC_LIFE,16,y,250,100,2000,cfg.trail_lifetime_ms); y+=36;
    CreateWindowW(L"BUTTON",L"Triangle particles",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,16,y,200,22,hwnd,(HMENU)IDC_PARTICLES,g_inst,nullptr);
    CheckDlgButton(hwnd,IDC_PARTICLES,cfg.triangle_particles?BST_CHECKED:BST_UNCHECKED); y+=24;
    CreateWindowW(L"BUTTON",L"Disable when cursor hidden",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,16,y,240,22,hwnd,(HMENU)IDC_HIDE_CUR,g_inst,nullptr);
    CheckDlgButton(hwnd,IDC_HIDE_CUR,cfg.not_when_cursor_hidden?BST_CHECKED:BST_UNCHECKED); y+=28;

    make_label(hwnd,IDC_BL_HINT,L"Blacklist (pick process or edit)",16,y,260,18); y+=18;
    std::string bl; for(size_t i=0;i<cfg.blacklisted_processes.size();++i){ if(i) bl+=";"; bl+=cfg.blacklisted_processes[i]; }
    wchar_t wbl[1024]{}; MultiByteToWideChar(CP_UTF8,0,bl.c_str(),-1,wbl,1024);
    CreateWindowW(L"EDIT",wbl,WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,16,y,250,22,hwnd,(HMENU)IDC_BLACKLIST,g_inst,nullptr); y+=28;

    make_label(hwnd,0,L"Running processes (double-click to add)",16,y,280,18); y+=18;
    CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,16,y,250,140,hwnd,(HMENU)IDC_PROCLIST,g_inst,nullptr);
    CreateWindowW(L"BUTTON",L"Add",WS_CHILD|WS_VISIBLE,276,y,90,26,hwnd,(HMENU)IDC_ADD_PROC,g_inst,nullptr);
    CreateWindowW(L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE,276,y+32,90,26,hwnd,(HMENU)IDC_REFRESH_PROC,g_inst,nullptr);
    y += 150;

    CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,146,y,90,28,hwnd,(HMENU)IDC_APPLY,g_inst,nullptr);
    CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE,250,y,90,28,hwnd,(HMENU)IDC_CLOSE,g_inst,nullptr);
    fill_process_list(hwnd);
    refresh_labels(hwnd);
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
}
bool tray_init(HINSTANCE inst, AppControl* ctrl) {
    g_inst = inst; g_ctrl = ctrl;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=TrayWndProc; wc.hInstance=inst; wc.lpszClassName=L"MouseTrailV2Tray";
    RegisterClassExW(&wc);
    HWND tray_hwnd = CreateWindowExW(0,L"MouseTrailV2Tray",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,inst,nullptr);
    if (!tray_hwnd) return false;
    ZeroMemory(&g_nid,sizeof(g_nid));
    g_nid.cbSize=sizeof(g_nid); g_nid.hWnd=tray_hwnd; g_nid.uID=ID_TRAY;
    g_nid.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; g_nid.uCallbackMessage=WM_TRAY;
    g_nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    std::snprintf(g_nid.szTip,sizeof(g_nid.szTip),"MouseTrailV2 - Running");
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
    AppendMenuW(menu,MF_STRING,IDM_TOGGLE,on?L"Pause":L"Resume");
    AppendMenuW(menu,MF_STRING,IDM_SETTINGS,L"Settings...");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,IDM_EXIT,L"Exit");
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
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName=L"MouseTrailV2Config"; RegisterClassExW(&wc);
    g_cfg_hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"MouseTrailV2Config",L"MouseTrailV2 Settings",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,400,620,parent,nullptr,inst,nullptr);
    if (!g_cfg_hwnd) return;
    build_config_ui(g_cfg_hwnd,*ctrl->cfg);
    ShowWindow(g_cfg_hwnd,SW_SHOW); UpdateWindow(g_cfg_hwnd);
}
