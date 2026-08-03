# #8 Glyph Two-Level Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 双表 LRU（OutlineClean + PlanarTess），直边改 depth/inflate 时 L1 命中不再三角化；HUD 可见命中率。

**Architecture:** 拆 `build_extruded_mesh` 为 outline → planar → 3d；`GlyphGeometryCache` 挂两张独立 LRU；`text_layout` 经 cache 构建；不做 L2 Mesh。

**Tech Stack:** C++17, 现有 mesh/text 模块, ImGui HUD

**Spec:** `docs/superpowers/specs/2026-07-29-glyph-two-level-cache-design.md`

## Global Constraints

- 主线程同步；#8 不要求 cache 线程安全
- L1 键用输入参数（strength==0 不含 depth）
- 行为与 #7 mesh_regress 一致

---

### Task 1: 拆分 planar / 3d API

**Files:**
- Modify: `src/mesh/mesh_extrude.h`, `src/mesh/mesh_extrude.cpp`
- Create: `src/mesh/glyph_planar.h`（结构体 + 声明，可先放 extrude 头里再拆）

**Produces:**
- `struct GlyphPlanar2D { cleaned, inner, cap_xy, cap_tris, bbox, applied_radius, ... }`
- `bool build_glyph_planar_2d(outline, ExtrudeOptions partial, GlyphPlanar2D&, BuildResult*)`
- `bool build_glyph_3d_from_planar(const GlyphPlanar2D&, ExtrudeOptions, Mesh&, BuildResult*)`
- `build_extruded_mesh` 变为薄包装（无 cache 时行为不变）

- [ ] 抽出 `GlyphPlanar2D` 与两个函数
- [ ] `build_extruded_mesh` 调用二者
- [ ] 编译通过；手测或 mesh_regress 无回归

---

### Task 2: GlyphGeometryCache（L0 + L1 LRU）

**Files:**
- Create: `src/mesh/glyph_geometry_cache.h`, `src/mesh/glyph_geometry_cache.cpp`
- Modify: `CMakeLists.txt`

**Produces:**
- `OutlineKey` / `PlanarKey`（量化 flatness/depth/strength）
- `class GlyphGeometryCache`：`get_or_build_outline`, `get_or_build_planar`, `stats()`, `set_byte_limits`
- 独立 LRU + 字节预算

- [ ] 实现双表 LRU
- [ ] 单元级：同键二次 lookup hit；改 depth 直边 L1 hit；fillet+depth L1 miss

---

### Task 3: 接入 layout + HUD

**Files:**
- Modify: `src/text/text_layout.h/cpp`, `src/main.cpp`, `src/perf/perf_stats.*`, `src/ui/debug_ui.*`

**Produces:**
- `LayoutOptions::cache` 指针
- rebuild 路径传入 cache
- HUD / 日志：outline/planar hit rate、bytes、entries

- [ ] layout 走 cache
- [ ] 直边拖 depth → stage_2d≈0、planar hit 上升
- [ ] 勾 Backlog #8 验收项

---

### Task 4: 文档与 Backlog

- [ ] 更新 `3D文字Demo优化Backlog.md` #8 勾选
- [ ] 必要时补一句 `文字渲染2.0.md`
