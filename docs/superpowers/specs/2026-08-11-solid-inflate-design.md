# 实体 Inflate（整字圆滑外胀）设计

日期：2026-08-11  
工程：`3d-text-demo/`  
状态：待实现  
依赖：Clipper2、`outline_boolean`、Fillet 剖面、#8 两级缓存、#17 joining run Union  
Backlog：#6（Inflate 语义重做；补齐原「2D 外扩」遗留）

## 1. 背景

现网 **Inflate** 是 Photoshop Cap 风格：只沿 Z 把正/背面按到轮廓距离鼓包，侧墙不动。用户期望改为 **整块立体字沿表面法线均匀外推**（实体 ⊕ 球 ≈ SDF 膨胀），棱角自然变圆。

真网格法线外推 / 体素 SDF 成本高、与现有分区/棱带难叠。本设计采用 **构造式实体偏移**（挤出棱柱的 Minkowski 和球的标准离散实现）。

## 2. 目标 / 非目标

### 目标

- 同一 `Inflate` 滑条（0~1）驱动整字实体外胀，不再做 Cap 抛物鼓包。
- XY 外环外胀、孔洞内收；Z 正背各推；帽↔侧圆角过渡。
- 强度映射到相对安全上限（半宽 / depth 钳制，保孔）。
- 对 **Union 后的 joining run** 轮廓做偏移（段内连续）。
- Bevel / Fillet **先尝试独立叠加**；观感差再改为 Inflate>0 时禁用（见 §6 回退）。
- 更新 UI 文案、README、Backlog #6 相关表述。

### 非目标

- 真体素 SDF / Marching Cubes / 通用三角网格 offset。
- 新增第二个滑条。
- 保留旧 Cap Inflate 作为可切换模式（本轮直接替换语义）。
- 本轮不做 Inflate 拖动时的 2D 缓存特化优化（正确优先）。

### 成功标准

- `Hello` 拉 Inflate：字变胖、正背变厚、棱变圆；侧墙与帽面圆滑衔接。
- `日` / `B` / `o`：孔不封死；`Applied Inflate R` > 0 且随强度单调不减（直至钳制）。
- `مرحبا`（joining run）：段内连续外胀，无假缝沟。
- `s=0`：与现网无 Inflate 行为一致。
- Bevel/Fillet 与 Inflate 同时开：不崩、不必然穿模；若圆滑度差，走 §6 回退。

## 3. 语义与公式

```text
s ∈ [0, 1]                                    // UI Inflate
R_safe = max_safe_edge_radius(outline, depth) // min(depth/2, 0.9×半宽) 等现有语义
R_req  = s × R_safe
R      = resolve_offset_radius(...)           // 外扩失败时下调，可能到 0
```

`R` 为实际应用的实体偏移半径（字体单位）。HUD 回传 **Applied Inflate R**（替换原 Applied H / 拱高）。

| 分量 | `R > 0` 时行为 |
|------|----------------|
| XY | Clipper `InflatePaths`，`JoinType::Round`，`EndType::Polygon`；外环 +δ、孔 −δ（Clipper 对定向环的标准偏移） |
| Z | 挤出厚度 `depth' = depth + 2R`（相对用户 depth，正背各外推 R） |
| 棱 | 帽↔侧使用半径贡献来自 Inflate 的圆角（见 §5）；竖直尖角由 Round join 变圆 |

`s=0` / `R=0`：不外扩、不加 Inflate 圆角、厚度仍为用户 `depth`。

## 4. 管线

插入点：在 **已 Union / clean 的挤出源轮廓** 上、进入「用户 Bevel/Fillet 内缩 + tess」之前。

```text
Phase A/A2   L0 cleaned → layout → joining Union（#17，不变）
extrude_prepared_glyph / build_glyph_planar_from_cleaned：
  outline0 = cleaned（run 或单字）
  R = resolve_offset_radius(outline0, s, depth)
  if R > 0:
      outline1 = OffsetRound(outline0, R) → clean
      depth'   = depth + 2R
  else:
      outline1 = outline0
      depth'   = depth
  // 其后与现网相同，但边界与厚度用 outline1 / depth'：
  用户 Bevel/Fillet：edge_radius_from_strength(..., outline1, depth')
                     + resolve_inset + tess
  build_glyph_3d_from_planar：侧墙/棱带基于 outline1；半厚 = depth'/2
```

**删除路径**：`append_inflated_caps` 抛物鼓包、帽面中点细分（仅为旧 Inflate 服务的部分）。直边 / Bevel / Fillet 的帽面恢复为平面帽（或仅由棱剖面决定的内缩帽），不再按距离场抬 z。

实现落点：**新建** `mesh_solid_offset.*`（`offset_outline_round` / `resolve_offset_radius`）；**删除**旧 Cap 鼓包路径（`append_inflated_caps` 及帽面细分）。`mesh_inflate.*` 若仅服务旧 Cap 则整文件移除，避免双语义并存。

## 5. 与 Bevel / Fillet 叠加（先试）

记：

- `R_inf` = 实体偏移实际半径  
- `R_user` = 用户 Bevel/Fillet 经 `edge_radius_from_strength` + `resolve_inset` 得到的半径（基于 `outline1`、`depth'`）

**推荐合成（v1）：**

```text
R_rim = min(R_safe(outline1, depth'), R_inf + R_user)

剖面类型：
  fillet>0            → FilletProfile
  else bevel>0        → ChamferProfile
  else R_inf>0        → FilletProfile   // 仅 Inflate 时必须有圆角棱，否则只是「变胖直棱柱」
  else                → 直边
```

- 仅 Inflate：`R_rim = R_inf`，Fillet，厚度 `depth'`，轮廓已 Offset → 对齐「棱柱 ⊕ 球」。  
- Inflate + 用户棱：半径加性叠加后再钳制；类型跟用户（无用户棱时用 Fillet）。

内缩 tess 的 inner 边界按 **`R_rim`**（不是只按 `R_user`），保证帽面边缘对齐棱带内侧。

## 6. 回退策略

若手测 Inflate+Bevel/Fillet 圆滑衔接差、尖刺或双圆角难看：

1. 当 `R_inf > eps` 时 **忽略用户 bevel/fillet**（`R_user=0`，强制 Fillet + `R_rim=R_inf`）。  
2. UI 可短暂 Disable 或灰色提示「Inflate 开启时棱角由膨胀决定」（可选，非必须首轮）。

本轮实现先做 §5；回退作为验收不通过时的同一 PR 或立刻 follow-up，不另开语义争论。

## 7. 孔洞与失败处理

- **保孔**：`R_safe` 继续吃半宽估计；外扩 `resolve_offset_radius` 若出现孔消失、面积坍缩、tess 失败 → 二分下调 `R`（镜像 `resolve_inset_radius`）。  
- Offset 后 `clean_glyph_outline`；空轮廓 → 失败回退 `R=0` 或返回 BuildStage 错误（与现网 tess 失败同级）。  
- Clipper 比例尺复用 `outline_boolean` / 现有 `kClipScale`。

## 8. 缓存与性能

| 层 | 变化 |
|----|------|
| L0 OutlineClean | 不变（仍是未偏移 cleaned） |
| L1 Planar | **`inflate` 进入 `PlanarKey`**（如 `inflate_q`）；改 Inflate 会 L1 miss 并重跑 Offset+tess |
| L2 Mesh pool | 已有 `inflate_q`，继续进键；语义变为实体 R，量化仍可用 `quantize_strength` |

接受：拖 Inflate 比旧 Cap 鼓包更重（动 2D）。正确优先；不做近似跳过 Offset。

`|run|>1` 的 Union 结果通常不进 L0/L1（与 #17 一致）；偏移在该路径上每次重建执行即可。

## 9. API / UI

| 位置 | 变更 |
|------|------|
| `ExtrudeOptions::inflate` | 注释改为实体偏移强度 0~1 |
| `out_applied_inflate_h` | 改为回传实际 `R`；推荐重命名 `out_applied_inflate_r`（及 layout/async/HUD 链路一并改名） |
| ImGui | 滑条名可仍为 `Inflate`；Disabled 文案 `Applied Inflate R = … (font units)` |
| README | 去掉「字面鼓包 / PS Cap」；改为整字实体外胀 |
| Backlog #6 | 勾选/改写「2D 外扩」与「正背面 Cap Inflate」条目，指向本 spec |

Bench（#15）若断言旧拱高语义，改为检查 `Applied Inflate R` 或仅几何阶段耗时。

## 10. 模块边界

| 单元 | 职责 |
|------|------|
| `offset_outline_round(outline, delta) → GlyphOutline` | Clipper 圆角偏移 + 绕序约定 |
| `resolve_offset_radius(...)` | 强度→R，失败二分 |
| `build_glyph_planar_from_cleaned` | 调用偏移；用 `depth'`/`outline1` 做用户棱内缩+tess；写入 `applied_inflate_r` |
| `build_glyph_3d_from_planar` | 用 `depth'` 与 `R_rim` 建帽/棱/墙；**不再** Cap 鼓包 |
| Fillet/Chamfer profiles | 去掉对 `append_inflated_caps` 的依赖；平面帽即可 |

## 11. 测试 / 验收

手工：

1. `Hello`，depth 适中，Inflate 0→1：变胖、变厚、变圆；Applied R 单调至钳制。  
2. `日`/`o`：孔可见，不封死。  
3. 仅 Fillet、仅 Bevel、Inflate+Fillet、Inflate+Bevel：不崩；衔接可接受或触发 §6。  
4. `مرحبا` + Inflate：段内连续。  
5. Inflate=0：与改前直边/棱行为无回归。

单测（若有 outline 测试架）：

- 矩形轮廓 OffsetRound 外扩后面积增大、圆角存在。  
- 带孔环：外胀后孔面积减小但不为零（在 R < 半宽时）。

## 12. 风险

- 半宽估计偏小 → Inflate 视觉偏弱（与现 Bevel Safe R 同一限制）。  
- Inflate+用户棱加性 `R_rim` 可能过圆 → §6。  
- 拖 Inflate 触发 L1 miss → 卡顿；可接受。  
- 旧文档/PPT 仍写 Cap Inflate → 本轮至少改 README + Backlog；长文 `文字渲染2.0.md` 可标过时（可选）。
