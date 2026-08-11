# Windows 构建支持设计

日期：2026-08-11  
范围：让 `3d-text-demo` 在 Windows（MSVC / 常见 GPU 驱动）上可用同一套 CMake 流程编译运行。

## 目标

- Windows 上 `fetch_deps` → `cmake -B build` → `cmake --build` → 运行 `Text3DDemo` 可行
- 不破坏现有 macOS 流程

## 非目标

- 不引入 vcpkg 强制依赖进仓；文档给出可选安装方式即可
- 不移植 `run_perf_matrix.sh` 为 PowerShell（可用手动命令）
- 不保证远程桌面 / 无硬件 OpenGL 环境

## 改动

1. **CMake**：全平台 `find_package(OpenGL)` 并链接 `OpenGL::GL`（覆盖 `opengl32`）
2. **路径宏**：`TEXT3D_SHADER_DIR` / `TEXT3D_ASSETS_DIR` 统一正斜杠，避免 Windows 反斜杠打断字符串字面量
3. **默认字体**：按平台设置 Arial / 阿拉伯文字体路径；启动时跳过不存在的系统字体文件
4. **MSVC**：`/utf-8`，保证源码中文注释与字符串
5. **README**：补充 Windows（vcpkg GLFW）步骤

## 验收

- macOS 原流程仍可配置编译
- CMake 在 `WIN32` 时链接 OpenGL；默认字体宏为 `C:/Windows/Fonts/...`
- 缺少某系统字体时程序仍能靠 `assets/fonts` 启动
