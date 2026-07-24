#pragma once
#include <string>
#include <vector>

struct AppConfig {
    float opacity = 0.52f;
    float ribbon_thickness = 9.0f;
    bool triangle_particles = false;
    bool not_when_cursor_hidden = true;
    std::vector<std::string> blacklisted_processes{"javaw.exe"};
    int trail_lifetime_ms = 800;
    float sample_min_distance = 1.6f;
    int max_points = 120;
    int fps_limit = 120;
    std::string toggle_hotkey = "F8";
    std::string quit_hotkey = "F9";
    // Base hue (0..1). Head color = base + time * rainbow_cycle_speed (wraps).
    float rainbow_hue_head = 0.0f;
    // How much hue shifts from head -> tail along one ribbon.
    float rainbow_hue_span = 0.85f;
    // Full rainbow cycles per second at the cursor head (0 = freeze).
    float rainbow_cycle_speed = 0.35f;
    float stroke_scale = 3.0f;

    static AppConfig load(const std::string& path);
    void save(const std::string& path) const;
};

int hotkey_to_vk(const std::string& name);