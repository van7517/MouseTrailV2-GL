#pragma once
#include <string>
#include <vector>

struct AppConfig {
    float opacity = 0.78f;
    float ribbon_thickness = 14.0f;
    bool triangle_particles = false;
    bool not_when_cursor_hidden = true;
    std::vector<std::string> blacklisted_processes{"javaw.exe"};
    int trail_lifetime_ms = 560;
    float sample_min_distance = 2.0f;
    int max_points = 80;
    int fps_limit = 120;
    std::string toggle_hotkey = "F8";
    std::string quit_hotkey = "F9";
    float rainbow_hue_head = 0.95f;
    float rainbow_hue_span = 0.95f;
    float stroke_scale = 0.95f;

    static AppConfig load(const std::string& path);
    void save(const std::string& path) const;
};

int hotkey_to_vk(const std::string& name);
