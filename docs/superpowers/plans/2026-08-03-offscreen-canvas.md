# #14 Offscreen Canvas Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 空白画布离屏链路：Clear canvas FBO → 画 3D 字 → Screen Present → Swap；同尺寸 FBO 复用；分阶段埋点。

**Architecture:** `OffscreenCanvas` 管 FBO；`ScreenPass` 全屏采样贴窗；`main` 默认开画布模式，可关回直绘；aspect 用画布比。

**Tech Stack:** C++17, OpenGL 3.3, GLFW, ImGui, 现有 Renderer

**Spec:** `docs/superpowers/specs/2026-08-03-offscreen-canvas-design.md`

## Global Constraints

- 每帧 Clear canvas，不回灌上一帧输出
- 无 Readback、无常规 `glFinish`
- 本轮无底图；compose 耗时恒 0
- Screen 默认 passthrough；`u_gamma` 预留

---

### Task 1: OffscreenCanvas + ScreenPass

**Files:**
- Create: `src/render/offscreen_canvas.h`, `src/render/offscreen_canvas.cpp`
- Create: `src/render/screen_pass.h`, `src/render/screen_pass.cpp`
- Create: `shaders/screen.vert`, `shaders/screen.frag`
- Modify: `CMakeLists.txt`

**Produces:**
- `OffscreenCanvas::{ensure, begin, end, color_tex, width, height, valid}`
- `ScreenPass::{init, draw(tex), shutdown}`；`u_tex` + `u_gamma`（默认 1.0）

- [x] 实现 FBO 同尺寸复用
- [x] Screen 全屏三角 passthrough
- [x] 编进 Text3DDemo

---

### Task 2: Perf + HUD + main 接入

**Files:**
- Modify: `src/perf/perf_stats.h`, `src/perf/perf_stats.cpp`
- Modify: `src/ui/debug_ui.h`, `src/ui/debug_ui.cpp`
- Modify: `src/main.cpp`

**Produces:**
- `offscreen_pass_ms` / `present_ms`；`offscreen_compose_ms` 保持 0
- Edit：`use_offscreen_canvas`、`canvas_size_index`（720p/1080p）
- 画布模式：ensure → begin/clear → draw → present；aspect=画布比

- [x] 默认开画布；可关直绘
- [x] HUD 显示分项耗时
- [x] 编译运行：移动/动画无拖影

---

### Task 3: Backlog / 文档

- [x] 勾选 `3D文字Demo优化Backlog.md` #14
- [x] 更新 spec 状态为已落地
- [x] 可选：`文字渲染3.0.md` §8 补一句
