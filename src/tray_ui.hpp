#pragma once
#include "config.hpp"
#include <Windows.h>
#include <string>

// Global app control (set by main, used by tray/ui)
struct AppControl {
    bool* enabled = nullptr;
    bool* running = nullptr;
    AppConfig* cfg = nullptr;
    std::string cfg_path;
    HWND overlay_hwnd = nullptr;
    void (*on_config_saved)() = nullptr;
};

bool tray_init(HINSTANCE inst, AppControl* ctrl);
void tray_shutdown();
void tray_show_menu(HWND owner);
void tray_process_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Returns true if message was handled by config UI
bool config_ui_is_open();
void config_ui_open(HINSTANCE inst, HWND parent, AppControl* ctrl);
