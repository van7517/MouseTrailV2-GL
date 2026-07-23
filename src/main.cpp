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

namespace fs = std::filesystem;

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

static int run_app(HINSTANCE inst) {
    enable_dpi_awareness();

    const std::string cfg_path = config_path_appdata();
    AppConfig cfg = AppConfig::load(cfg_path);

    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(GLFW_MOUSE_PASSTHROUGH)
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif

    VirtualScreen vs = get_virtual_screen();
    GLFWwindow* window = glfwCreateWindow(vs.w, vs.h, "MouseTrailV2", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwSetWindowPos(window, vs.x, vs.y);
    glfwShowWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    HWND hwnd = glfwGetWin32Window(window);
    apply_overlay_styles(hwnd);
    hide_from_taskbar(hwnd);

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
    ctrl.overlay_hwnd = hwnd;
    ctrl.on_config_saved = nullptr;

    tray_init(inst, &ctrl);

    auto t_prev = std::chrono::steady_clock::now();

    while (running && !glfwWindowShouldClose(window)) {
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

        const auto t_now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        if (dt < 0.f) dt = 0.f;
        if (dt > 0.1f) dt = 0.1f;
        const double now = std::chrono::duration<double>(t_now.time_since_epoch()).count();

        glfwPollEvents();
        ++frame;

        if (frame % 12 == 0) {
            force_topmost(hwnd);
            hide_from_taskbar(hwnd);
        }


        if (frame % 60 == 0) {
            vs = get_virtual_screen();
            int w = 0, h = 0, wx = 0, wy = 0;
            glfwGetWindowSize(window, &w, &h);
            glfwGetWindowPos(window, &wx, &wy);
            if (w != vs.w || h != vs.h || wx != vs.x || wy != vs.y) {
                glfwSetWindowPos(window, vs.x, vs.y);
                glfwSetWindowSize(window, vs.w, vs.h);
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

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        int win_w = 0, win_h = 0, win_x = 0, win_y = 0;
        glfwGetWindowSize(window, &win_w, &win_h);
        glfwGetWindowPos(window, &win_x, &win_y);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(fbw), 0.0, static_cast<double>(fbh), -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        const float sx = (win_w > 0) ? static_cast<float>(fbw) / static_cast<float>(win_w) : 1.f;
        const float sy = (win_h > 0) ? static_cast<float>(fbh) / static_cast<float>(win_h) : 1.f;
        glScalef(sx, sy, 1.f);

        if (enabled) {
            trail.draw(static_cast<float>(win_x), static_cast<float>(win_y),
                       static_cast<float>(win_h), now, cfg);
        }

        glfwSwapBuffers(window);

        const double target = 1.0 / static_cast<double>(cfg.fps_limit > 0 ? cfg.fps_limit : 60);
        for (;;) {
            const auto t2 = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(t2 - t_now).count();
            if (elapsed >= target) break;
            if (target - elapsed > 0.002) Sleep(1);
        }
    }

    tray_shutdown();
    glfwDestroyWindow(window);
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
