# Glyph Overlap Clip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在挤出前对布局空间相邻重叠字形做 Clipper2 轮廓差集，消除共面 z-fighting，并保留逐字动画。

**Architecture:** `append_shaped_line` 拆成「取 L0 cleaned outline → AABB+Difference → planar/挤出」两阶段；未裁字继续走 L1/Mesh 池，被裁字跳过池写入。布尔封装在 `outline_boolean`。

**Tech Stack:** C++17、Clipper2（`Path64`）、现有 FreeType/HarfBuzz layout、CMake、`fetch_deps.py`

## Global Constraints

- 差集归属：buffer 下标 `i < j` 时 `j := Difference(j, i)`；多邻居 Union 裁刀一次差
- 裁刀微胀 `+0.25px`；AABB eps `0.5px`；邻域 `K=2`
- 被裁字不写 L1 / MeshPool；失败回退未裁 + `overlap_clip_failed`
- 本轮不改 inflate 连写语义
- 依赖实体不进 Git，只改 `deps.json` / CMake / README

---

## File Structure

| 文件 | 职责 |
|------|------|
| `deps.json` / `third_party/README.md` / `CMakeLists.txt` | 引入 Clipper2 |
| `src/mesh/outline_boolean.h/.cpp` | GlyphOutline ↔ Clipper、差集/相交面积 |
| `tests/outline_boolean_test.cpp` | 矩形差集冒烟可执行文件 |
| `src/text/text_layout.cpp` | 两阶段 layout + 重叠裁剪 |
| `3D文字Demo优化Backlog.md` | 勾选/新增条目 |
| `docs/superpowers/specs/2026-08-11-glyph-overlap-clip-design.md` | 状态改为已落地 |

---

### Task 1: 接入 Clipper2

**Files:**
- Modify: `deps.json`
- Modify: `CMakeLists.txt`
- Modify: `third_party/README.md`
- Test: `python3 scripts/fetch_deps.py --only clipper2` 后 marker 存在

**Interfaces:**
- Produces: CMake target `clipper2`；include `clipper2/clipper.h`

- [ ] **Step 1: deps.json 增加 clipper2**

```json
{
  "name": "clipper2",
  "version": "1.5.4",
  "type": "archive",
  "url": "https://github.com/AngusJohnson/Clipper2/archive/refs/tags/Clipper2_1.5.4.tar.gz",
  "strip_components": 1,
  "dest": "third_party/clipper2",
  "marker": "CPP/Clipper2Lib/include/clipper2/clipper.h"
}
```

（若 tag 404，改用仓库当时最新 `Clipper2_*` tag。）

- [ ] **Step 2: 拉取**

Run: `python3 scripts/fetch_deps.py --only clipper2`  
Expected: `ok clipper2 -> .../third_party/clipper2`

- [ ] **Step 3: CMake 静态库 + 链接 Text3DDemo**

```cmake
set(CLIPPER2_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/clipper2/CPP/Clipper2Lib)
add_library(clipper2 STATIC
  ${CLIPPER2_ROOT}/src/clipper.engine.cpp
  ${CLIPPER2_ROOT}/src/clipper.offset.cpp
  ${CLIPPER2_ROOT}/src/clipper.rectclip.cpp
)
target_include_directories(clipper2 PUBLIC ${CLIPPER2_ROOT}/include)
# Text3DDemo: 增加 src/mesh/outline_boolean.cpp；link clipper2
```

`_TEXT3D_REQUIRED_DEPS` 增加 marker 路径。

- [ ] **Step 4: README 表加一行 `clipper2/`**

- [ ] **Step 5: Commit**

```bash
git add deps.json CMakeLists.txt third_party/README.md
git commit -m "build: 接入 Clipper2 依赖"
```

---

### Task 2: outline_boolean + 冒烟测试

**Files:**
- Create: `src/mesh/outline_boolean.h`
- Create: `src/mesh/outline_boolean.cpp`
- Create: `tests/outline_boolean_test.cpp`
- Modify: `CMakeLists.txt`（`OutlineBooleanTest` 可执行文件）

**Interfaces:**
- Produces:
  - `struct Aabb2 { float min_x,min_y,max_x,max_y; };`
  - `Aabb2 compute_outline_aabb(const GlyphOutline&);`
  - `bool aabb_overlaps(const Aabb2&, const Aabb2&, float eps);`
  - `double outline_intersection_area(const GlyphOutline& a, const GlyphOutline& b);`
  - `bool difference_outlines(const GlyphOutline& subject, const std::vector<const GlyphOutline*>& clips, GlyphOutline& out, float clip_inflate_delta);`
- Scale: 内部 `kClipScale = 100`（float×100 → int64）

- [ ] **Step 1: 写测试（先失败）**

两个轴对齐矩形：A `[0,0]-[10,10]`，B `[5,0]-[15,10]`。  
`difference_outlines(B,{&A},out,0)` 后 `outline_intersection_area(A,out) < 1e-2`，且 out 面积约 50。

- [ ] **Step 2: 实现 outline_boolean**

要点：
- 外环 CW / 孔 CCW → 送 Clipper 前反转成 CCW 外 / CW 孔；结果再转回工程约定后 `clean_glyph_outline`
- `clip_inflate_delta>0` 时对裁刀 `InflatePaths`（Clipper2 offset）
- 多 clip：先 Union 再 Difference
- 无有效外环 → return false，不改写调用方可用的失败语义

- [ ] **Step 3: 跑测试 PASS**

Run: `cmake --build build --target OutlineBooleanTest && ./build/OutlineBooleanTest`  
Expected: exit 0

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(mesh): outline_boolean 基于 Clipper2 的轮廓差集"
```

---

### Task 3: layout 两阶段重叠裁剪

**Files:**
- Modify: `src/text/text_layout.cpp`
- Modify: `src/mesh/outline_boolean.h`（若需 translate helper）

**Interfaces:**
- Consumes: Task 2 API
- Produces: 重叠字挤出前已差集；日志可含 `clipped=N`

- [ ] **Step 1: Phase A 结构**

对每个 shaped glyph：
1. 取 L0 cleaned outline（与现逻辑相同，失败则 skip + advance）
2. `origin = (pen_x+x_offset, pen_y+y_offset)`（**尚未** scale；与现 rest 计算一致，scale 在 mesh/落点后处理）
3. 轮廓平移副本到 layout 空间存 `layout_outline`
4. `aabb = compute_outline_aabb(layout_outline)`；`pen_x += advance`

- [ ] **Step 2: 裁剪**

对每个 `j`，收集 `i` 满足 `0 < j-i <= 2` 且 `aabb_overlaps(..., 0.5f)` 且 `intersection_area > 1e-2` 的 `layout_outline[i]`，Union 差到 `j`（`clip_inflate_delta=0.25f`）。失败则保留原 outline + fallback 字符串。

差完后把 `layout_outline[j]` **移回本地**：`translate(-origin)` 得到挤出用 outline；`was_clipped=true`。

- [ ] **Step 3: Phase B 挤出**

- 未裁：现有 mesh_pool / L1 / 3D  
- 已裁：`build_glyph_planar_from_cleaned` → `build_glyph_3d_from_planar`（或无 cache 时 `build_extruded_mesh`），**不** `put` planar/mesh_pool；`rest` 公式不变

- [ ] **Step 4: 编译 Demo**

Run: `cmake --build build --target Text3DDemo`  
Expected: success

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(layout): 相邻重叠字形轮廓差集消 z-fighting"
```

---

### Task 4: 文档收尾

**Files:**
- Modify: `3D文字Demo优化Backlog.md`
- Modify: `docs/superpowers/specs/2026-08-11-glyph-overlap-clip-design.md`（状态：已落地）

- [ ] **Step 1: Backlog 增加已完成条目（重叠裁剪）**
- [ ] **Step 2: Spec 状态更新**
- [ ] **Step 3: Commit**

```bash
git commit -m "docs: 标记字形重叠裁剪设计已落地"
```

---

## Spec coverage

| Spec | Task |
|------|------|
| Clipper2 依赖 | 1 |
| outline_boolean API / 微胀 / Path64 | 2 |
| AABB K=2、归属、两阶段、缓存策略 | 3 |
| Backlog / 验收说明 | 4 |
| inflate 连写 | 明确不做 |

## Manual verify（实现后）

1. Demo：`مرحبا` + SF Arabic，inflate=0，转相机 / Appear Spin 不闪  
2. `Hello` 无异常 clip 日志洪泛
