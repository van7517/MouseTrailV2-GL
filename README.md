# MouseTrailV2

Windows 鼠标轨迹小工具：彩虹幽灵缎带效果。

**当前版本：v0.1.2**

- C++ + OpenGL + GLFW
- 托盘后台运行（不占任务栏、无控制台）
- 全屏透明叠加、点击穿透、始终置顶
- 中文设置界面
- 发布包仅一个 `MouseTrailV2.exe`

---

## 下载

1. 打开 [Releases](https://github.com/van7517/MouseTrailV2-GL/releases)
2. 下载 **v0.1.2** 的 `MouseTrailV2-windows-x64.zip`（或最新 Release）
3. 解压得到 **唯一文件** `MouseTrailV2.exe`，双击运行

也可在 [Actions](https://github.com/van7517/MouseTrailV2-GL/actions) 下载最新构建产物。

---

## 使用

| 操作 | 说明 |
|------|------|
| 运行 | 双击 `MouseTrailV2.exe`，图标出现在右下角托盘 |
| 托盘右键 | **暂停轨迹** / **继续轨迹**、**设置...**、**退出** |
| 双击托盘 | 打开设置 |

配置自动保存在：

`%AppData%\MouseTrailV2\config.json`

无需手动编辑；改完设置点「应用」即可。

---

## 功能

| 项目 | 说明 |
|------|------|
| 幽灵缎带 | 连续彩虹轨迹，按宽度逐渐变细消失 |
| 可视化设置 | 透明度、粗细、缩放、残留时间等 |
| 缩放 Scale | 范围 **1～10** |
| 残留时间 | 默认 **800 ms**（可调） |
| 进程黑名单 | 前台命中名单时不画轨迹；可从运行进程列表点选添加 |
| 光标隐藏时禁用 | 可选 |
| 三角形粒子 | 可选，默认关闭 |
| 开机自启 | 设置中可开关（写入当前用户启动项） |

---

## 设置项说明

| 项 | 含义 |
|----|------|
| 透明度 | 0%～100% |
| 粗细基准 | 丝带基础宽度 |
| 缩放 | 1～10，实际观感粗细 ≈ 粗细基准 × 缩放 |
| 残留 | 轨迹保留时长（毫秒） |
| 三角形粒子 | 移动时是否喷小三角 |
| 光标隐藏时禁用 | 系统隐藏光标时不新增轨迹 |
| 黑名单 | 进程名列表（`;` 分隔），如 `javaw.exe` |
| 开机自启 | 登录 Windows 后自动启动 |

---

## 本地编译

需要：Windows 10/11、CMake 3.20+、Visual Studio 2022（「使用 C++ 的桌面开发」）。

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：`build\Release\MouseTrailV2.exe`（单个可执行文件）

---

## 项目结构

```
MouseTrailV2-GL/
  CMakeLists.txt
  LICENSE
  README.md
  CHANGELOG.md
  .github/workflows/build.yml
  src/
    main.cpp           # 入口、叠加层主循环
    trail.cpp/.hpp     # 轨迹采样与绘制
    platform_win.cpp   # 光标 / 前台进程 / 置顶穿透
    tray_ui.cpp/.hpp   # 托盘与设置界面
    config.cpp/.hpp    # 配置读写
```

---

## 技术说明

- GLFW 透明帧缓冲 + 鼠标穿透 + 置顶
- OpenGL 2.1 绘制（GPU）
- 托盘图标驻留，设置窗为独立小窗口
- 黑名单匹配前台窗口所属进程名

---

## 许可证

[MIT License](LICENSE)

## 参与贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)

安全问题见 [SECURITY.md](SECURITY.md)

更新记录见 [CHANGELOG.md](CHANGELOG.md)