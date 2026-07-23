# 贡献指南

感谢关注 MouseTrailV2。欢迎提交 Issue 与 Pull Request。

## 开发环境

- Windows 10/11
- CMake 3.20+
- Visual Studio 2022（「使用 C++ 的桌面开发」工作负载）

## 本地构建

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：`build\Release\MouseTrailV2.exe`

## 提交建议

1. 先从最新 `main` 开分支
2. 改动尽量小而清晰，相关改动放在同一 PR
3. 提交说明用中文或英文均可，写清「改了什么 / 为什么」
4. 若改动绘制或输入逻辑，请在 PR 里说明测试方式（例如：分辨率、多显示器、全屏游戏等）

## 代码风格

- C++17
- 保持现有目录结构：`src/` 源码，配置文件放仓库根目录
- 不引入与本工具无关的大依赖；新依赖请在 PR 中说明原因

## Issue

请尽量提供：

- 系统版本
- 显卡 / 驱动概况（如方便）
- 复现步骤
- 预期行为与实际行为
- 相关日志或截图（如有）

## 行为准则

请保持友善、就事论事。不接受骚扰、人身攻击或恶意破坏讨论区的行为。
