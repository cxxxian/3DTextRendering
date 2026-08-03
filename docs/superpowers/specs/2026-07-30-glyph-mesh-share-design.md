# #9 CPU Mesh 池 + 同字共享 — 设计

日期：2026-07-30  
工程：`3d-text-demo/`  
状态：已落地（见 `文字渲染2.0.md` §9）

## 目标

- 同句重复 glyph 共享一份 CPU `Mesh`（`shared_ptr`）
- 跨次 rebuild（如 `aaa`→`aab`）未变 glyph 类型不重挤 3D
- HUD 可见 `mesh_hit`，对照 #8

## 架构

```
MeshKey = OutlineKey + tess + edge_mode
        + depth_q + strength_q + inflate_q + scale_q   // 最终形状全进键
值 = shared_ptr<const Mesh> + applied_r / inflate_h / safe_cap

GlyphInstance { shared_ptr<const Mesh> mesh; rest_x; rest_y; glyph_index }
```

layout：先查 Mesh 池；命中则跳过 L0/L1/3D；miss 则走 #8 + 挤出 → 缩放 → put。

## 非目标

GPU buffer 共享（#12）；文本槽位 diff。
