# 相邻字形重叠裁剪（消 z-fighting）设计

日期：2026-08-11  
工程：`3d-text-demo/`  
状态：设计待审  
依赖：现有 HB shape、contour clean、#8 两级缓存、#9 Mesh 池  
后续：inflate 连写整体化（另开）

## 1. 背景

连写文种（阿拉伯文等）在字体设计里故意让相邻字形墨水区重叠，2D 才能看成一体。当前管线按 shaped glyph **各自**取轮廓、三角化、挤出；重叠区变成共面双层帽面/侧墙 → **z-fighting**。

Cap Inflate 以单字轮廓为边界，连写缝处会被压成高度 0，鼓包不像整块——**本轮不做**，另开。

## 2. 目标 / 非目标

### 目标

- 布局空间检测到相邻轮廓墨水重叠时，用 **2D 多边形差集**去掉共面墨水，再各自挤出。
- **所有文种默认走同一管线**：先做便宜 AABB 门闩；仅相交才跑布尔。
- 保持 **一个 shaped glyph = 一个可动 `GlyphInstance`**（阿语逐字动画不变）。
- `layout_text`（merged）与 `layout_text_glyphs`（per-glyph）都受益：裁剪在挤出之前。

### 非目标

- inflate 连写「一整块鼓包」。
- 整词合成单个 mesh / 修改 HarfBuzz advance。
- 完美无缝焊接拓扑（允许极细接缝）。

### 成功标准

- `مرحبا` + SF Arabic / System Arabic：`inflate=0`，静止转相机与 Appear Spin 逐字动画下接缝不再闪。
- `Hello` / 单字 `A`：视觉无回归；无重叠时仅多 O(n) bbox 成本。

## 3. 方案摘要

采用 **Clipper2** 对布局坐标下的折线轮廓做 **Difference**：

```text
后字轮廓 := Difference(后字轮廓, 先字轮廓)
```

AABB 只负责发现候选对；真正裁的是字形轮廓多边形，不是包围盒本身。

## 4. 管线位置

裁剪插在 **clean 之后、planar / 三角化 / 挤出之前**：

```text
FreeType 曲线 → 折线（flatness）
  → contour clean
  → 布局摆位 + AABB 门闩 + Clipper2 Difference   ← 新增
  → planar（bevel/fillet 内缩等）+ 三角化
  → 挤出 / 侧墙 /（现有 inflate，本轮不改语义）
```

插入点：`text_layout.cpp` 的 `append_shaped_line`，拆成两阶段（准备轮廓 → 裁剪 → 挤出）。

## 5. 重叠检测

| 项 | 规则 |
|----|------|
| 范围 | **同一行**内；跨行不裁 |
| 候选 | 默认 `|i−j| ≤ K`，**K=2**（覆盖连写 + 近邻变音/叠加） |
| 门闩 | 布局空间 AABB 相交；可选膨胀 **eps ≈ 0.5px** |
| 精判 | bbox 相交后再用 Clipper Intersection；面积低于阈值则视为无叠（建议相对较小字面积，如 `< 1e-2` 字体单位²） |

未相交 → 与现网路径完全一致（含 L1 / Mesh 池）。

## 6. 差集归属规则

对相交对 `(i, j)` 且 `i < j`（HarfBuzz **buffer 下标**，与 LTR/RTL 视觉左右无关）：

```text
subject = glyph[j]
clip    = glyph[i]
glyph[j] := Difference(glyph[j], glyph[i])
glyph[i] 不变
```

多邻居：对每个 `j`，将所有 `i < j` 且相交的 `i` 的轮廓 **Union** 成一把裁刀，再对 `j` 做一次 Difference（等价于依次差，一次差更稳）。

**共边防闪（本轮做）**：裁刀先做微胀 `+delta`（建议约 **0.25px**）再 Difference，避免共面边残留。

若差完后无有效外环：回退为未裁轮廓，并写 `fallback_reason=overlap_clip_failed`（宁闪不丢字）。

## 7. Clipper2 依赖

工程当前无布尔库，需新增：

1. `deps.json` 增加 Clipper2  
2. `python3 scripts/fetch_deps.py` → `third_party/clipper2`  
3. `CMakeLists.txt` / `third_party/README.md` 接入  

新模块建议：`src/mesh/outline_boolean.h` / `.cpp`

- `GlyphOutline` ↔ Clipper 路径（外环/孔绕序与现有 clean：外 CW、孔 CCW 对齐转换）
- API：
  - `bool outlines_aabb_overlap(...)`（或布局层自算 AABB）
  - `bool outlines_overlap_area(a, b, min_area)`
  - `bool difference_outline(subject, clip, out, float clip_inflate_delta)`

坐标：优先 `Path64` + 固定 scale（如 ×100）再还原 float，避免纯 float 布尔不稳。

## 8. Layout 两阶段

### Phase A — 准备与裁剪

1. `shape_text`  
2. 每字：取/建 **L0 cleaned outline**（未裁）；记录 `layout_origin = (pen_x+x_offset, pen_y+y_offset)`、AABB、`glyph_index`  
3. 建相交对 / 裁刀集合  
4. 对被裁索引执行 Difference → `final_outline`；标记 `was_clipped`

### Phase B — 挤出

- `was_clipped == false`：现有 L1 / Mesh 池 / 3D 路径  
- `was_clipped == true`：
  - **不写入** 仅按裸 `glyph_index` 键控的 L1 / Mesh 池（邻域相关；本轮不对裁后结果做池缓存）
  - 用 `final_outline` 走 planar → 3D  
  - `rest` 仍用 `pen + offset + bbox_center(裁后 mesh)`；**不改 advance**

`layout_text` 继续「glyphs → translate → merge」，自动无叠面。

## 9. 缓存策略

| 层 | 未裁字 | 被裁字 |
|----|--------|--------|
| L0 OutlineClean | 命中未裁 outline | 仍用未裁 L0 作差集输入；裁后 **不入** L0 |
| L1 Planar | 照旧 | 本轮 **不缓存** |
| MeshPool | 照旧 | 本轮 **不缓存** |

阿语短句每次 rebuild 对被裁字多付 planar+挤出，可接受。后续可用「邻居 glyph_index + 量化相对 origin」扩展 `MeshKey`（非本轮必须）。

## 10. 错误与回退

- Clipper 失败 / 空结果 → 该字用未裁 outline，`fallback_reason=overlap_clip_failed`  
- clean / planar / 挤出失败 → 与现网相同：跳过该字、推进 pen  
- 单字裁剪失败不导致整句失败（与现 partial glyph 策略一致）

## 11. 动画与渲染

- 不改 `GlyphInstance` 语义、renderer、动画模块  
- 裁后顶点数可变；Appear Spin 仍按实例变换  
- 验收：动画过程接缝不闪即可

## 12. 测试 / 验收

### 手工

1. `مرحبا` + SF/System Arabic，depth 适中，inflate=0：转相机无闪  
2. Appear Spin 播放无闪  
3. `Hello` / `A`：无视觉回归；日志无大量 clip  

### 实现期建议

- `outline_boolean` 单测：两矩形重叠 → 差后面积正确、无交  
- Debug UI 显示 `clipped_glyphs=N`（可选、低优先级）

### Bench

本轮不强制改性能矩阵；需要时再加阿语场景。

## 13. 改动面

| 区域 | 改动 |
|------|------|
| `deps.json` / CMake / `third_party/README` | Clipper2 |
| `src/mesh/outline_boolean.*` | 新建 |
| `src/text/text_layout.cpp`（及必要时 `.h`） | 两阶段 + 裁剪 |
| `3D文字Demo优化Backlog.md` | 新条目（重叠裁剪消 z-fighting） |

不改：inflate 算法、renderer、HB shaper（仅 layout 消费方式变化）。

## 14. 后续（切开）

- inflate 连写：裁缝当非边界，或 run 级距离场  
- 被裁字的 L1/Mesh 邻域缓存  
- 远距叠字全对 AABB（默认仍 K=2）
