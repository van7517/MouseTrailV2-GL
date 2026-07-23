#include "config.hpp"
#include "platform_win.hpp"
#include "trail.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::string exe_dir() {
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return ".";
    return fs::path(buf).parent_path().string();
}

int main() {
    enable_dpi_awareness();

    const std::string base = exe_dir();
    const std::string cfg_path = (fs::path(base) / "config.json").string();
    AppConfig cfg = AppConfig::load(cfg_path);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
#if defined(GLFW_MOUSE_PASSTHROUGH)
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif

    VirtualScreen vs = get_virtual_screen();
    GLFWwindow* window = glfwCreateWindow(vs.w, vs.h, "MouseTrailV2", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwSetWindowPos(window, vs.x, vs.y);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    void* hwnd = glfwGetWin32Window(window);
    apply_overlay_styles(hwnd);

    TrailSystem trail;
    bool enabled = true;
    bool prev_toggle = false;
    bool prev_quit = false;
    int frame = 0;
    bool blocked = false;
    CursorState last{};
    last.ok = false;

    const int toggle_vk = hotkey_to_vk(cfg.toggle_hotkey);
    const int quit_vk = hotkey_to_vk(cfg.quit_hotkey);

    std::printf("MouseTrailV2 (OpenGL)\n");
    std::printf("  config: %s\n", cfg_path.c_str());
    std::printf("  %s=toggle  %s=quit\n", cfg.toggle_hotkey.c_str(), cfg.quit_hotkey.c_str());
    std::printf("  opacity=%.2f thickness=%.1f fps=%d\n", cfg.opacity, cfg.ribbon_thickness, cfg.fps_limit);

    auto t_prev = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        const auto t_now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        if (dt < 0.f) dt = 0.f;
        if (dt > 0.1f) dt = 0.1f;
        const double now = std::chrono::duration<double>(t_now.time_since_epoch()).count();

        glfwPollEvents();
        ++frame;

        if (frame % 12 == 0) force_topmost(hwnd);

        const bool td = is_key_down(toggle_vk);
        const bool qd = is_key_down(quit_vk);
        if (td && !prev_toggle) {
            enabled = !enabled;
            if (!enabled) trail.clear();
            std::printf("%s\n", enabled ? "[ON] Enabled" : "[OFF] Disabled");
        }
        if (qd && !prev_quit) glfwSetWindowShouldClose(window, 1);
        prev_toggle = td;
        prev_quit = qd;

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

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
