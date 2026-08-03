# #12 GPU Mesh 共享 / 实例化 Implementation Plan

> **For agentic workers:** 本会话按 Inline Execution 推进；规格见 `docs/superpowers/specs/2026-08-03-gpu-mesh-instancing-design.md`。

**Goal:** 逐字路径同几何共用一份 GPU Mesh，并用 `glDrawElementsInstanced` 按几何合批绘制。

**Architecture:** `GpuMeshPool` 按 fingerprint 持有 VAO/VBO/EBO；`GpuGlyphSlot` 只引用几何；`main` 每帧传 `mat4[]`；`pbr.vert` 用 `uUseInstance` 双模式。

**Tech Stack:** C++17, OpenGL 3.3 Core, glm, 现有 Renderer

## Global Constraints

- 保留 #11 槽位 Skip；`up` = 唯一几何上传数
- 池第一版 ref=0 即删，无跨 apply LRU
- merged 整句路径行为不变
- OpenGL 仅主线程

---

### Task 1: 文档（spec FAQ + 3.0 #12）

- Modify: `docs/superpowers/specs/2026-08-03-gpu-mesh-instancing-design.md`
- Modify: `文字渲染3.0.md`

- [ ] Spec 增加「槽 vs Entry vs 矩阵」FAQ
- [ ] 3.0 写 #12 正文与手测；进度表勾上进行中/完成

### Task 2: GpuMeshPool + upload 去重

- Create: `src/render/gpu_mesh_pool.h`, `src/render/gpu_mesh_pool.cpp`
- Modify: `src/render/renderer.h`, `src/render/renderer.cpp`, `CMakeLists.txt`

- [ ] Pool：acquire / retain_only / clear
- [ ] Slot 无自有 VBO；upload_glyphs 按 fingerprint 去重
- [ ] GpuUploadStats 扩展 unique_meshes / meshes_uploaded

### Task 3: Instanced draw + shader + main

- Modify: `shaders/pbr.vert`, `renderer.*`, `main.cpp`

- [ ] vert：`uVP` + `aModel` + `uUseInstance`
- [ ] `draw_glyphs_instanced`；Glass 双 pass
- [ ] main 组装 matrices 并调用

### Task 4: HUD / 编译验证

- Modify: `perf_stats.*`, `debug_ui.cpp`, `main.cpp`

- [ ] HUD：`up/unique/DC`
- [ ] 编译通过
