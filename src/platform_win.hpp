#pragma once
#include <string>
#include <vector>

struct CursorState {
    int x = 0;
    int y = 0;
    bool visible = true;
    bool ok = false;
};

struct VirtualScreen {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Single physical monitor rectangle in virtual-desktop coordinates.
struct MonitorRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

void enable_dpi_awareness();
VirtualScreen get_virtual_screen();
// Enumerate all connected monitors. Falls back to primary/virtual screen if empty.
std::vector<MonitorRect> get_monitors();
CursorState get_cursor_state();
bool is_key_down(int vk);
std::string get_foreground_process_name();
bool process_is_blacklisted(const std::string& name, const std::vector<std::string>& list);
void apply_overlay_styles(void* hwnd);
void force_topmost(void* hwnd);
// Enable per-pixel alpha composition for the overlay HWND (DWM).
void enable_window_transparency(void* hwnd);
