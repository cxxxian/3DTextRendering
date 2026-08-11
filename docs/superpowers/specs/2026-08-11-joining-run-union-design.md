# 连写 Joining Run 合并设计

日期：2026-08-11  
工程：`3d-text-demo/`  
状态：设计待审  
依赖：#16 重叠检测门闩、Clipper2、`outline_boolean`、现有 layout / 挤出 / 动画  
取代：段内逐字 `Difference`（#16 Phase A2）与「seam 边标记」草案  
Backlog：建议 #17

## 1. 背景

#16 用相邻轮廓 **Difference** 消除共面 z-fighting，但 **Inflate / Fillet / 倒角** 仍按单字外轮廓工作：接缝被当成真外边界 → 鼓包塌沟、棱带像切开。

阿语连写的自然单位不是整句，也常常不是整词，而是 **joining run**（一段墨水相连的字母）。段内合成一块再做效果，比逐字标 seam 更简单、观感更好。

动画粒度改为：**一个 run = 一个可动实例**（段内一起动；段与段之间拆开）。

## 2. 目标 / 非目标

### 目标

- 同一行内，将「轮廓重叠连通」的字形并为 **一块布局轮廓**（Clipper **Union**），再 clean → planar → inflate/fillet/bevel → 挤出。
- 插入点仍在现有 **Phase A2**（原 Difference 处）：准备完布局轮廓之后、挤出之前。
- `|run|=1`（如拉丁无重叠）行为与缓存与现网一致。
- Appear Spin 等按 **run 实例** 变换（不再要求逐字母）。

### 非目标

- 逐字母动画  
- seam 边标记  
- 整句合成单 mesh  
- 本轮不强制删除 `difference_outlines` API（可保留作工具；layout 段内不再调用）

### 成功标准

- `مرحبا` + Inflate / Fillet / Bevel：段内连续，无沟、无假棱切开、无 z-fighting  
- Appear Spin：run 级动画可接受  
- `Hello` / 单字 `A`：无回归；实例数仍约等于字数  

## 3. Joining run 定义

**几何连通（文种无关，推荐）：**

1. 同一行 `prepared[]` 已摆到布局坐标，各有 AABB + `work_layout`  
2. 若 `aabb_overlaps(i,j, eps)` 且 `outline_intersection_area > min_area` → 建无向边  
3. 连通分量 = 一个 joining run  

与 #16 门闩参数可复用：`K` 邻域建边时可先只连 `|i-j|≤K` 的候选，再在分量内扩展；或对行内全对 AABB（短句可接受）。**推荐**：先对 `|i-j|≤K`（K=2）建边；若需更稳，对 AABB 膨胀后行内全对建边（阿语短词成本低）。

空格两侧通常不相交 → 自然分成多 run。词内非连写字母若轮廓不叠 → 多 run。

## 4. 管线（Phase A2 改制）

```text
Phase A   每字 L0 cleaned → origin → work_layout → aabb   （不变）
Phase A2  【改】重叠图 → 连通分量
          每个 run：
            |run|=1 → outline = 该字 work_layout
            |run|>1 → outline = Union(成员 work_layout) → clean
          输出 run 列表（每项一份 outline + 放置用 bbox/origin）
Phase B   对每个 run 挤出 → 一个 GlyphInstance（语义=run 实例）
```

**段内不再** `Difference`。

### Run 放置与 rest

- Union 在**布局坐标**完成  
- 对 run 的 mesh：先算 bbox 中心 `(cx,cy)`，几何移到本地（减 cx/cy），`rest_x/y = cx/cy`（再经整句 `center_glyphs_as_block`）  
- 与现「单字 rest = origin + layout_cx」一致，只是 origin 换成 run 的布局中心  

### 单字 run

完全走现有 L1 / Mesh 池路径（`glyph_index` 键）。

### 多字 run

- 不写 L1 / Mesh 池（邻域相关）  
- `GlyphInstance.glyph_index`：可用 run 内第一个成员的 index，或 0；日志打 `runs=` / `union_glyphs=`  

## 5. Clipper 用法

- 需要：**Union**（段内合并）  
- 不再需要段内 **Difference** 防闪  
- `outline_boolean` 新增例如：

```cpp
bool union_outlines(const std::vector<const GlyphOutline*>& parts, GlyphOutline& out);
```

实现：各 part → `Path64`，`Clipper2Lib::Union(..., FillRule::NonZero)`，再转回 + `clean_glyph_outline`。  
绕序转换与现 `difference_outlines` 相同。

可选：Union 后对结果做极小清理；失败则回退为「run 内不合并、改回逐字 Difference」或「只挤出面积最大的成员」——**推荐失败时 fallback_reason + 逐字不合并并打日志**（宁闪/接缝问题可见，不丢整行）。更稳妥的失败策略实现期可定为：失败则该 run 拆回逐字且对该子集临时走旧 Difference（可选，非必须）。

**本轮默认失败策略：** Union 失败 → 该 run 拆成逐字、不 Union，并写 `fallback_reason=run_union_failed`（与 #16 关闭时类似，可能闪；优先修 Union）。

## 6. 动画与 UI

- `layout_text_glyphs` 产出实例数 = **run 数**  
- Appear Spin / instancing：现有「按 glyphs 向量」逻辑不变，只是元素变少、每个更大  
- Debug：可选显示 `runs=N (union M glyphs)`  

## 7. 与 #16 文档关系

- #16 设计仍有效作：**重叠检测思想、Clipper 接入、AABB 门闩**  
- #16 Phase A2 **Difference 被本设计取代**（段内）  
- 实现合并后更新 `2026-08-11-glyph-overlap-clip-design.md` 状态备注，或在 3.0 笔记中改写 §9 Phase A2 描述  

## 8. 测试 / 验收

### 手工

1. `مرحبا` + SF/System Arabic：Inflate / Fillet / Bevel 段内连续、不闪  
2. Appear Spin：整段（或数 run）一起动  
3. `Hello`：实例数与字数一致量级，无 Union 日志洪泛  

### 单测

- `union_outlines`：两重叠矩形 → 面积约并集、单一外环（或合理孔）  
- 连通分量：三字链重叠 → 一个 run；两字分离 → 两个 run  

## 9. 改动面

| 区域 | 改动 |
|------|------|
| `outline_boolean.*` | `union_outlines` |
| `text_layout.cpp` Phase A2 | Difference → run 连通 + Union |
| `tests/` | Union / 连通冒烟 |
| Backlog | #17 |
| 3.0 笔记 | 后续补 §；本轮先 spec |

不改：inflate/fillet/bevel 算法本体（吃的是已 Union 的外轮廓，自然连续）。

## 10. 实现顺序建议

1. `union_outlines` + 单测  
2. Phase A2 改为连通分量 + Union，去掉 layout 内 Difference  
3. 确认 rest / 动画 / Hello 回归  
4. 文档与 Backlog  
