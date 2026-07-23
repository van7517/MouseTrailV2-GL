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

void enable_dpi_awareness();
VirtualScreen get_virtual_screen();
CursorState get_cursor_state();
bool is_key_down(int vk);
std::string get_foreground_process_name();
bool process_is_blacklisted(const std::string& name, const std::vector<std::string>& list);
void apply_overlay_styles(void* hwnd);
void force_topmost(void* hwnd);
