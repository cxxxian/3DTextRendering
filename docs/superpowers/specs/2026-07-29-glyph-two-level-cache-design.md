# #8 二维 / 3D 两级缓存 — 设计

日期：2026-07-29  
工程：`3d-text-demo/`  
状态：已落地（见 `文字渲染2.0.md` §8）

## 目标

深度 / 倒角 / 膨胀变化时尽量复用二维结果；有内存上限与淘汰；HUD/日志可见命中率。

## 架构：双表 LRU（不做 L2 Mesh 缓存）

```
L0 OutlineCache  键: font + glyph + flatness
                 值: clean 后轮廓

L1 PlanarCache   键: L0 字段 + tess_mode +（有圆角时）depth_q + strength_q + edge_mode
                 值: inner + cap_xy + cap_tris + bbox + applied_radius

3D               始终现算：L1 + depth/bevel/fillet/inflate → Mesh
```

### 命中预期

| 操作 | L0 | L1 | 3D |
|------|----|----|-----|
| 直边只改 depth / inflate | hit | hit | 重算 |
| 有 fillet 只改 inflate | hit | hit | 重算 |
| 有 fillet 改 depth / 强度 | hit | miss | 重算 |
| 换 tess | hit | miss | 重算 |
| 换字 / 换字体 | miss | miss | 重算 |

### L1 键规则

- `strength == 0`（bevel=fillet=0）：键**不含** depth / strength
- `strength > 0`：键含量化 `depth_q`、`strength_q`、`edge_mode`（Bevel|Fillet）
- 用输入参数组键，不用算完才有的 `applied_R`

### LRU

两表独立 LRU + 字节预算；满则踢最久未用条目。#8 同步主线程，不要求线程安全。

### 非目标

- L2 CPU Mesh 缓存、整句缓存、GPU 缓存、异步（#9/#10/#11）

## 接缝

- 拆 `build_extruded_mesh` → outline clean / planar tess / 3d extrude
- `text_layout` 经 `GlyphGeometryCache` 查 L0/L1
- `PerfStats` / HUD 增加 outline/planar hit 统计
