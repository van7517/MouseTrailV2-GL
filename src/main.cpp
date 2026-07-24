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

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

#include <cstring>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// GL FBO entry points (optional; fall back to window framebuffer if missing)
typedef void (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* ids);
typedef void (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint fb);
typedef void (APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* ids);
typedef void (APIENTRY *PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint* ids);
typedef void (APIENTRY *PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint rb);
typedef void (APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei w, GLsizei h);
typedef void (APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb);
typedef void (APIENTRY *PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint* ids);

static PFNGLGENFRAMEBUFFERSPROC pglGenFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFERPROC pglBindFramebuffer = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC pglFramebufferTexture2D = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC pglCheckFramebufferStatus = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC pglDeleteFramebuffers = nullptr;

static bool load_fbo_procs() {
    auto load = [](const char* name) -> void* {
        return reinterpret_cast<void*>(wglGetProcAddress(name));
    };
    pglGenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(load("glGenFramebuffers"));
    if (!pglGenFramebuffers) pglGenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(load("glGenFramebuffersEXT"));
    pglBindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(load("glBindFramebuffer"));
    if (!pglBindFramebuffer) pglBindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(load("glBindFramebufferEXT"));
    pglFramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(load("glFramebufferTexture2D"));
    if (!pglFramebufferTexture2D) pglFramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(load("glFramebufferTexture2DEXT"));
    pglCheckFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(load("glCheckFramebufferStatus"));
    if (!pglCheckFramebufferStatus) pglCheckFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(load("glCheckFramebufferStatusEXT"));
    pglDeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(load("glDeleteFramebuffers"));
    if (!pglDeleteFramebuffers) pglDeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(load("glDeleteFramebuffersEXT"));
    return pglGenFramebuffers && pglBindFramebuffer && pglFramebufferTexture2D &&
           pglCheckFramebufferStatus && pglDeleteFramebuffers;
}

struct OverlaySurface {
    HWND hwnd = nullptr;
    int x = 0, y = 0, w = 0, h = 0;

    HDC hdc_mem = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ old_bmp = nullptr;
    void* bits = nullptr;
    int dib_w = 0, dib_h = 0;

    GLuint fbo = 0;
    GLuint tex = 0;
    int fbo_w = 0;
    int fbo_h = 0;
};

static const wchar_t* kOverlayClass = L"MouseTrailV2Overlay";

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

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Fully passive overlay: never take focus / paint via GDI.
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool register_overlay_class(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // If already registered, ok.
    if (RegisterClassExW(&wc) == 0) {
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    return true;
}

static void free_dib(OverlaySurface& o) {
    if (o.hdc_mem) {
        if (o.old_bmp) SelectObject(o.hdc_mem, o.old_bmp);
        o.old_bmp = nullptr;
        if (o.dib) DeleteObject(o.dib);
        o.dib = nullptr;
        DeleteDC(o.hdc_mem);
        o.hdc_mem = nullptr;
    }
    o.bits = nullptr;
    o.dib_w = o.dib_h = 0;
}

static void free_fbo(OverlaySurface& o) {
    if (o.fbo && pglDeleteFramebuffers) {
        pglDeleteFramebuffers(1, &o.fbo);
        o.fbo = 0;
    }
    if (o.tex) {
        glDeleteTextures(1, &o.tex);
        o.tex = 0;
    }
    o.fbo_w = 0;
    o.fbo_h = 0;
}

static bool ensure_dib(OverlaySurface& o, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (o.hdc_mem && o.dib && o.bits && o.dib_w == w && o.dib_h == h) return true;
    free_dib(o);
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    o.hdc_mem = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!o.hdc_mem) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = h; // bottom-up matches glReadPixels
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    o.dib = CreateDIBSection(o.hdc_mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!o.dib || !bits) {
        free_dib(o);
        return false;
    }
    o.bits = bits;
    o.old_bmp = SelectObject(o.hdc_mem, o.dib);
    o.dib_w = w;
    o.dib_h = h;
    return true;
}

static bool ensure_fbo(OverlaySurface& o, int w, int h) {
    if (!pglGenFramebuffers) return false;
    if (o.fbo && o.tex && o.fbo_w == w && o.fbo_h == h) return true;
    free_fbo(o);

    glGenTextures(1, &o.tex);
    glBindTexture(GL_TEXTURE_2D, o.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    pglGenFramebuffers(1, &o.fbo);
    pglBindFramebuffer(GL_FRAMEBUFFER, o.fbo);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, o.tex, 0);
    const GLenum status = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        free_fbo(o);
        return false;
    }
    o.fbo_w = w;
    o.fbo_h = h;
    return true;
}

static HWND create_overlay_hwnd(HINSTANCE inst, const MonitorRect& m) {
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kOverlayClass,
        L"MouseTrailV2",
        WS_POPUP,
        m.x, m.y, m.w, m.h,
        nullptr, nullptr, inst, nullptr);
    if (!hwnd) return nullptr;
    // Do not call SetLayeredWindowAttributes — UpdateLayeredWindow owns the pixels.
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return hwnd;
}

static void present_surface(OverlaySurface& o) {
    if (!o.hwnd || !o.bits || o.dib_w <= 0 || o.dib_h <= 0) return;
    POINT pt_pos{o.x, o.y};
    POINT pt_src{0, 0};
    SIZE size{o.dib_w, o.dib_h};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    UpdateLayeredWindow(o.hwnd, screen, &pt_pos, &size, o.hdc_mem, &pt_src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

static void present_transparent_clear(OverlaySurface& o) {
    if (!ensure_dib(o, o.w, o.h) || !o.bits) return;
    const size_t bytes = static_cast<size_t>(o.dib_w) * static_cast<size_t>(o.dib_h) * 4;
    memset(o.bits, 0, bytes);
    present_surface(o);
}

static std::vector<OverlaySurface> create_surfaces(HINSTANCE inst, const std::vector<MonitorRect>& monitors) {
    std::vector<OverlaySurface> out;
    out.reserve(monitors.size());
    for (const auto& m : monitors) {
        if (m.w <= 0 || m.h <= 0) continue;
        OverlaySurface o;
        o.x = m.x; o.y = m.y; o.w = m.w; o.h = m.h;
        o.hwnd = create_overlay_hwnd(inst, m);
        if (!o.hwnd) continue;
        present_transparent_clear(o);
        out.push_back(o);
    }
    return out;
}

static void destroy_surfaces(std::vector<OverlaySurface>& surfaces) {
    for (auto& o : surfaces) {
        free_fbo(o);
        free_dib(o);
        if (o.hwnd) {
            DestroyWindow(o.hwnd);
            o.hwnd = nullptr;
        }
    }
    surfaces.clear();
}

static bool monitors_match(const std::vector<OverlaySurface>& surfaces,
                           const std::vector<MonitorRect>& monitors) {
    if (surfaces.size() != monitors.size()) return false;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].x != monitors[i].x || surfaces[i].y != monitors[i].y ||
            surfaces[i].w != monitors[i].w || surfaces[i].h != monitors[i].h) {
            return false;
        }
    }
    return true;
}

static void render_surface(OverlaySurface& o, GLFWwindow* glwin, bool use_fbo, const TrailSystem& trail, bool enabled,
                           double now, const AppConfig& cfg) {
    if (!o.hwnd) return;
    if (!ensure_dib(o, o.w, o.h)) return;

    bool bound_fbo = false;
    if (use_fbo && ensure_fbo(o, o.w, o.h)) {
        pglBindFramebuffer(GL_FRAMEBUFFER, o.fbo);
        bound_fbo = true;
    } else {
        // Fallback: resize hidden GL window so default FB is large enough to read.
        glfwSetWindowSize(glwin, o.w, o.h);
        int fbw=0, fbh=0;
        glfwGetFramebufferSize(glwin, &fbw, &fbh);
        if (fbw < o.w || fbh < o.h) return;
    }

    glViewport(0, 0, o.w, o.h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(o.w), 0.0, static_cast<double>(o.h), -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (enabled) {
        trail.draw(static_cast<float>(o.x), static_cast<float>(o.y),
                   static_cast<float>(o.h), now, cfg);
    }

    glFinish();
    glReadPixels(0, 0, o.w, o.h, GL_BGRA, GL_UNSIGNED_BYTE, o.bits);

    if (bound_fbo) {
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    present_surface(o);
}

static int run_app(HINSTANCE inst) {
    enable_dpi_awareness();
    if (!register_overlay_class(inst)) return 1;

    const std::string cfg_path = config_path_appdata();
    AppConfig cfg = AppConfig::load(cfg_path);

    if (!glfwInit()) return 1;

    // Hidden GL context only — never used as the visible overlay surface.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    // Request alpha-capable pixel format for glReadPixels A channel.
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);

    GLFWwindow* glwin = glfwCreateWindow(64, 64, "MouseTrailV2GL", nullptr, nullptr);
    if (!glwin) {
        // Retry without transparent FB hint
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
        glwin = glfwCreateWindow(64, 64, "MouseTrailV2GL", nullptr, nullptr);
    }
    if (!glwin) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(glwin);
    glfwSwapInterval(0);
    const bool use_fbo = load_fbo_procs();

    std::vector<MonitorRect> monitors = get_monitors();
    std::vector<OverlaySurface> surfaces = create_surfaces(inst, monitors);
    if (surfaces.empty()) {
        glfwDestroyWindow(glwin);
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
    ctrl.overlay_hwnd = surfaces.front().hwnd;
    ctrl.on_config_saved = nullptr;

    tray_init(inst, &ctrl);
    auto t_prev = std::chrono::steady_clock::now();

    while (running) {
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
        if (glfwWindowShouldClose(glwin)) break;

        const auto t_now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        if (dt < 0.f) dt = 0.f;
        if (dt > 0.1f) dt = 0.1f;
        const double now = std::chrono::duration<double>(t_now.time_since_epoch()).count();

        glfwPollEvents();
        ++frame;

        if (frame % 12 == 0) {
            for (auto& o : surfaces) force_topmost(o.hwnd);
        }

        if (frame % 60 == 0) {
            monitors = get_monitors();
            if (!monitors_match(surfaces, monitors)) {
                destroy_surfaces(surfaces);
                surfaces = create_surfaces(inst, monitors);
                if (surfaces.empty()) {
                    running = false;
                    break;
                }
                ctrl.overlay_hwnd = surfaces.front().hwnd;
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

        glfwMakeContextCurrent(glwin);
        for (auto& o : surfaces) {
            render_surface(o, glwin, use_fbo, trail, enabled, now, cfg);
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
    destroy_surfaces(surfaces);
    glfwDestroyWindow(glwin);
    glfwTerminate();
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    return run_app(inst);
}

#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif