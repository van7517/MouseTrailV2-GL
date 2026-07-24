#include "config.hpp"
#include "platform_win.hpp"
#include "trail.hpp"
#include "tray_ui.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct OverlayWindow {
    GLFWwindow* window = nullptr;
    HWND hwnd = nullptr;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

static std::string config_path_appdata() {
    char appdata[MAX_PATH]{};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata))) {
        return "config.json";
    }
    fs::path dir = fs::path(appdata) / "MouseTrailV2";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "config.json").string();
}

static void hide_from_taskbar(HWND hwnd) {
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST;
    ex &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    // Re-apply topmost without activating
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void apply_glfw_overlay_hints() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    // Color-key transparency (WS_EX_LAYERED + LWA_COLORKEY) is used instead of FB alpha.
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(GLFW_MOUSE_PASSTHROUGH)
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
}

static void setup_overlay_hwnd(OverlayWindow& o) {
    o.hwnd = glfwGetWin32Window(o.window);
    apply_overlay_styles(o.hwnd);
    hide_from_taskbar(o.hwnd);
    enable_window_transparency(o.hwnd);
}

// One transparent overlay per physical monitor.
// Spanning the whole virtual desktop with a single OpenGL window is a common
// cause of solid black screens on multi-monitor setups (DWM alpha composition fails).
static std::vector<OverlayWindow> create_overlays(const std::vector<MonitorRect>& monitors) {
    std::vector<OverlayWindow> out;
    out.reserve(monitors.size());
    GLFWwindow* share = nullptr;

    for (const auto& m : monitors) {
        if (m.w <= 0 || m.h <= 0) continue;

        apply_glfw_overlay_hints();
        GLFWwindow* win = glfwCreateWindow(m.w, m.h, "MouseTrailV2", nullptr, share);
        if (!win) continue;
        if (!share) share = win;

        OverlayWindow o;
        o.window = win;
        o.x = m.x;
        o.y = m.y;
        o.w = m.w;
        o.h = m.h;
        glfwSetWindowPos(o.window, o.x, o.y);
        glfwShowWindow(o.window);
        setup_overlay_hwnd(o);

        // First context: disable vsync; later contexts inherit preference when made current.
        glfwMakeContextCurrent(o.window);
        glfwSwapInterval(0);

        out.push_back(o);
    }
    return out;
}

static void destroy_overlays(std::vector<OverlayWindow>& overlays) {
    // First window is the GL share parent; destroy shared children first, parent last.
    glfwMakeContextCurrent(nullptr);
    for (size_t i = overlays.size(); i-- > 0;) {
        if (overlays[i].window) {
            glfwDestroyWindow(overlays[i].window);
            overlays[i].window = nullptr;
            overlays[i].hwnd = nullptr;
        }
    }
    overlays.clear();
}

static bool monitors_match(const std::vector<OverlayWindow>& overlays,
                           const std::vector<MonitorRect>& monitors) {
    if (overlays.size() != monitors.size()) return false;
    for (size_t i = 0; i < overlays.size(); ++i) {
        if (overlays[i].x != monitors[i].x || overlays[i].y != monitors[i].y ||
            overlays[i].w != monitors[i].w || overlays[i].h != monitors[i].h) {
            return false;
        }
    }
    return true;
}

static void render_overlay(OverlayWindow& o, const TrailSystem& trail, bool enabled,
                           double now, const AppConfig& cfg) {
    if (!o.window) return;
    glfwMakeContextCurrent(o.window);

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(o.window, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) return;

    glViewport(0, 0, fbw, fbh);
    glClearColor(0.f, 0.f, 0.f, 1.f); // pure black = color-key transparent
    glClear(GL_COLOR_BUFFER_BIT);

    int win_w = 0, win_h = 0;
    glfwGetWindowSize(o.window, &win_w, &win_h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(fbw), 0.0, static_cast<double>(fbh), -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    const float sx = (win_w > 0) ? static_cast<float>(fbw) / static_cast<float>(win_w) : 1.f;
    const float sy = (win_h > 0) ? static_cast<float>(fbh) / static_cast<float>(win_h) : 1.f;
    glScalef(sx, sy, 1.f);

    if (enabled) {
        // origin = this monitor's top-left in virtual-desktop coords
        trail.draw(static_cast<float>(o.x), static_cast<float>(o.y),
                   static_cast<float>(o.h), now, cfg);
    }

    glfwSwapBuffers(o.window);
}

static int run_app(HINSTANCE inst) {
    enable_dpi_awareness();

    const std::string cfg_path = config_path_appdata();
    AppConfig cfg = AppConfig::load(cfg_path);

    if (!glfwInit()) return 1;

    std::vector<MonitorRect> monitors = get_monitors();
    std::vector<OverlayWindow> overlays = create_overlays(monitors);
    if (overlays.empty()) {
        glfwTerminate();
        return 1;
    }

    TrailSystem trail;
    bool enabled = true;
    bool running = true;
    int frame = 0;
    bool blocked = false;
    CursorState last{};
    last.ok = false;

    AppControl ctrl;
    ctrl.enabled = &enabled;
    ctrl.running = &running;
    ctrl.cfg = &cfg;
    ctrl.cfg_path = cfg_path;
    ctrl.overlay_hwnd = overlays.front().hwnd;
    ctrl.on_config_saved = nullptr;

    tray_init(inst, &ctrl);

    auto t_prev = std::chrono::steady_clock::now();

    while (running) {
        // Pump Win32 messages for tray/settings
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        bool any_should_close = false;
        for (const auto& o : overlays) {
            if (o.window && glfwWindowShouldClose(o.window)) {
                any_should_close = true;
                break;
            }
        }
        if (any_should_close) break;

        const auto t_now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        if (dt < 0.f) dt = 0.f;
        if (dt > 0.1f) dt = 0.1f;
        const double now = std::chrono::duration<double>(t_now.time_since_epoch()).count();

        glfwPollEvents();
        ++frame;

        if (frame % 12 == 0) {
            for (auto& o : overlays) {
                force_topmost(o.hwnd);
                hide_from_taskbar(o.hwnd);
            }
        }

        // Hot-plug / resolution / layout changes: rebuild per-monitor overlays
        if (frame % 60 == 0) {
            monitors = get_monitors();
            if (!monitors_match(overlays, monitors)) {
                destroy_overlays(overlays);
                overlays = create_overlays(monitors);
                if (overlays.empty()) {
                    running = false;
                    break;
                }
                ctrl.overlay_hwnd = overlays.front().hwnd;
            } else {
                // Keep position/size in sync if OS nudged the windows
                for (size_t i = 0; i < overlays.size(); ++i) {
                    auto& o = overlays[i];
                    int w = 0, h = 0, wx = 0, wy = 0;
                    glfwGetWindowSize(o.window, &w, &h);
                    glfwGetWindowPos(o.window, &wx, &wy);
                    if (w != o.w || h != o.h || wx != o.x || wy != o.y) {
                        glfwSetWindowPos(o.window, o.x, o.y);
                        glfwSetWindowSize(o.window, o.w, o.h);
                        enable_window_transparency(o.hwnd);
                    }
                }
            }
        }

        if (frame % 10 == 0) {
            blocked = process_is_blacklisted(get_foreground_process_name(), cfg.blacklisted_processes);
        }

        CursorState cur = get_cursor_state();
        const bool hide = cfg.not_when_cursor_hidden && cur.ok && !cur.visible;

        if (enabled && cur.ok && !blocked && !hide) {
            float speed = 0.f;
            if (last.ok) {
                speed = std::hypot(static_cast<float>(cur.x - last.x), static_cast<float>(cur.y - last.y)) /
                        std::max(dt, 1e-4f);
            }
            trail.push(static_cast<float>(cur.x), static_cast<float>(cur.y), now, speed, cfg);
            last = cur;
        } else {
            last = cur;
            if (!enabled) trail.clear();
        }

        trail.update(now, dt, cfg);

        for (auto& o : overlays) {
            render_overlay(o, trail, enabled, now, cfg);
        }

        const double target = 1.0 / static_cast<double>(cfg.fps_limit > 0 ? cfg.fps_limit : 60);
        for (;;) {
            const auto t2 = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(t2 - t_now).count();
            if (elapsed >= target) break;
            if (target - elapsed > 0.002) Sleep(1);
        }
    }

    tray_shutdown();
    destroy_overlays(overlays);
    glfwTerminate();
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    return run_app(inst);
}

// Keep console-less entry for MSVC
#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif
