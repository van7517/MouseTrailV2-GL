# MouseTrailV2

Windows 独立鼠标轨迹小工具：彩虹色幽灵缎带效果。

- C++ + OpenGL + GLFW
- 全屏透明叠加、鼠标点击穿透、始终置顶
- 彩虹渐变轨迹
- GitHub Actions 自动编译 Windows x64

---

## 功能

| 项目 | 说明 |
|------|------|
| 幽灵缎带 | 细长连续彩虹轨迹 |
| 透明度 / 粗细 | 在 `config.json` 中调整 |
| 三角形粒子 | 可选，默认关闭 |
| 进程黑名单 | 默认 `javaw.exe` 前台时不画轨迹 |
| 光标隐藏时禁用 | 默认开启 |
| 快捷键 | `F8` 开关 / `F9` 退出 |

---

## 下载使用

1. 打开本仓库 [Releases](https://github.com/van7517/MouseTrailV2-GL/releases)
2. 下载 `MouseTrailV2-windows-x64.zip`
3. 解压后运行 `MouseTrailV2.exe`（请与 `config.json` 放在同一目录）

也可在 **Actions** 中下载最新构建产物。

---

## 配置说明（`config.json`）

```json
{
  "opacity": 0.55,
  "ribbon_thickness": 10.0,
  "triangle_particles": false,
  "not_when_cursor_hidden": true,
  "blacklisted_processes": ["javaw.exe"],
  "trail_lifetime_ms": 480,
  "sample_min_distance": 2.0,
  "max_points": 64,
  "fps_limit": 120,
  "toggle_hotkey": "F8",
  "quit_hotkey": "F9",
  "rainbow_hue_head": 0.95,
  "rainbow_hue_span": 0.95,
  "stroke_scale": 0.55
}
```

| 字段 | 含义 |
|------|------|
| `opacity` | 整体透明度 0~1 |
| `ribbon_thickness` | 缎带粗细基准 |
| `stroke_scale` | 粗细缩放（实际线宽 ≈ thickness × scale） |
| `triangle_particles` | 是否开启三角粒子 |
| `not_when_cursor_hidden` | 系统隐藏光标时不新增轨迹 |
| `blacklisted_processes` | 前台进程名黑名单（不区分大小写） |
| `trail_lifetime_ms` | 轨迹残留时长（毫秒） |
| `fps_limit` | 帧率上限 |
| `toggle_hotkey` / `quit_hotkey` | 开关 / 退出热键（F1~F12） |

修改配置后需**重启程序**生效。

---

## 本地编译

需要：Windows、CMake、Visual Studio 2022（含「使用 C++ 的桌面开发」）。

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物路径：`build\Release\MouseTrailV2.exe`  
构建后会自动复制 `config.json` 到同目录。

---

## 项目结构

```
MouseTrailV2-GL/
  CMakeLists.txt
  config.json
  README.md
  .github/workflows/build.yml
  src/
    main.cpp           # 入口、窗口、主循环
    trail.cpp/.hpp     # 轨迹采样与 OpenGL 绘制
    platform_win.cpp   # 光标、进程黑名单、置顶穿透
    config.cpp/.hpp    # 配置读写
```

---

## 技术说明

- 使用 GLFW 透明帧缓冲 + 鼠标穿透 + 置顶悬浮窗
- OpenGL 2.1 画线（GPU 渲染）
- 光标位置：`GetCursorInfo` / `GetCursorPos`
- 黑名单：读取前台窗口进程名进行匹配

---

## 许可证

本项目使用 [MIT License](LICENSE)。

## 参与贡献

请阅读 [贡献指南](CONTRIBUTING.md)。

安全问题请参见 [安全策略](SECURITY.md)。

更新记录见 [CHANGELOG](CHANGELOG.md)。
