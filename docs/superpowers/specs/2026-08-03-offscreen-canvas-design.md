# #14 离屏画布测试模式（方案 A）设计

日期：2026-08-03  
工程：`3d-text-demo/`  
状态：已落地（方案 A：空白画布）  
依赖：#10、#11（#12/#13 已有 draw 路径沿用）  
Backlog：`3D文字Demo优化Backlog.md` #14  

## 目标

把「窗口直绘」改成可测的**空白画布离屏链路**：

```text
Clear 空白画布 FBO → 3D 文字 → Screen Present → 窗口 → SwapBuffers
```

覆盖验收：每帧重来无拖影、同尺寸 FBO 复用、无 Readback / 无常规 `glFinish`、分阶段耗时。

本轮**不做**：底图输入、双 FBO 文字层合成、强制 gamma（Screen 预留开关即可）。

## 与现状对比

| | 现在 | #14 方案 A |
|--|------|------------|
| 字画到哪 | 默认 FB(0) | 离屏 canvas FBO |
| 每帧起点 | `glClear` 窗口 | `glClear` **画布**（不读上一帧输出） |
| 显示 | 直接 Swap | Screen Pass 采样 canvas → 窗口，再 Swap |
| SwapBuffers | 要 | **仍要** |

「输入帧」在本 Demo 退化为：**空白 Clear**（纯色底）。不是回灌上一帧输出。

## 每帧流程

```text
1. ensure_size(canvas_w, canvas_h)   // 同尺寸复用；变尺寸才重建附件
2. bind canvas FBO
3. viewport = canvas 尺寸
4. Clear COLOR|DEPTH              // 空白输入；禁止用上一帧 canvas 当底再叠
5. renderer.draw / draw_glyphs_* // 文字 Draw（旧 Mesh 语义不变）
6. bind FB(0)，viewport = 窗口
7. Clear 窗口（可选，避免未覆盖区域脏）
8. screen pass：全屏三角/四边形采样 canvas 色附件
9. ImGui
10. SwapBuffers
```

默认画布：**1280×720**；UI 可切 **1920×1080**。与窗口大小脱钩。

## 模块

### `OffscreenCanvas`（新建）

职责：FBO + 色附件 + 深度（或深度模板）附件；`ensure(w,h)` 复用；`begin()` / `end()` 绑定。

- 色附件：`GL_RGBA8` 纹理  
- 深度：`GL_DEPTH24_STENCIL8` renderbuffer（或同规格纹理）  
- 尺寸未变：不删建、不 `glTexImage` 重分配  
- 析构释放 GL 对象  

### Screen Pass（新建 shader + 小 helper）

- `shaders/screen.vert` + `shaders/screen.frag`  
- 全屏三角形（或 NDC quad），采样 `u_tex`  
- 第一版 **passthrough**；`u_gamma` 预留（默认 1.0 = 关闭矫正；以后可改 2.2）  
- 绘制时关深度测试，避免影响 ImGui  

### `main.cpp`

- 画布模式默认 **开**；HUD 可关回直绘，便于对比  
- 开：字只画进 canvas，再 present  
- 关：保持现有直绘路径  
- 投影 `aspect` 在画布模式下用 **画布宽高比**，不用窗口比（避免 FBO 与相机不一致）  

### 性能埋点

拆开 Backlog 要求的四段（空白画布下 compose≈空操作）：

| 字段 | 含义 |
|------|------|
| `text_draw_ms` | 已有：canvas 上的文字 draw |
| `offscreen_pass_ms` | 新建：bind/clear/ensure 等离屏准备 |
| `offscreen_compose_ms` | 本轮保持 0（无底图合成）；字段保留 |
| `present_ms` | 新建：Screen Pass 贴到窗口 |

`render_submit_ms` 仍包住「文字 + 离屏相关 + present + ImGui」或维持现语义并在 HUD 展示分项。  
日志 `[perf]` frame 行带上上述字段。

**禁止**：为计时调用 `glFinish`；不 `glReadPixels`。

## 验收对照

| 验收项 | 如何满足 |
|--------|----------|
| 输入→画布→文字→合成→输出 | Clear 作输入；单 FBO 画布；compose 本轮恒等；Screen=输出到窗口 |
| 不叠上一帧 | 每帧 Clear canvas |
| 移动无拖影 | 同上；手测转相机/动画 |
| 新 Mesh 前显示旧 Mesh | 不改 #10 apply/poll |
| FBO 复用 | `ensure` 同尺寸跳过重建 |
| 无 Readback | 不读回 CPU |
| 无常规 glFinish | 不计时用 finish |
| 分阶段耗时 | HUD + 日志 |

## 文件

- 新建：`src/render/offscreen_canvas.h/.cpp`  
- 新建：`shaders/screen.vert`、`shaders/screen.frag`  
- 新建或并入：`src/render/screen_pass.h/.cpp`（编译 screen 程序 + fullscreen draw）  
- 改：`main.cpp`、`perf_stats.*`、`debug_ui.*`、`CMakeLists.txt`  
- 可选：`文字渲染3.0.md` §8 补 #14 人话说明（实现后）  

## 非目标（本轮不做）

- 底图 / 视频输入帧  
- 文字层与底图双 FBO 合成  
- 强制 sRGB / gamma 开启（仅预留）  
- #15 固定场景跑分脚本  
