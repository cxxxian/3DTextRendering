# #10 异步构建 + latest-only — 设计

日期：2026-07-31  
工程：`3d-text-demo/`  
状态：已落地

## 目标

- CPU Mesh 构建（含 #8/#9 缓存比对）在工作线程完成
- 连续输入只保留最终结果（latest-only）
- 新结果完成前继续显示旧 GPU Mesh；完成后主线程一次性上传替换
- 连续变更期间主线程不被单次重建长时间阻塞

## 角色分工

| 角色 | 职责 |
|------|------|
| 主线程 | 输入 / debounce / 提交请求；poll 结果；**GPU 上传与绘制**（OpenGL Context 所在线程）；ImGui |
| 工作线程（1 个） | **独占** `FontFace` + `GlyphGeometryCache` + `GlyphMeshPool`；调用现有 `layout_text` / `layout_text_glyphs` |

主线程不读写 cache/pool。OpenGL 调用只在主线程。

## 决策摘要

| 项 | 选择 |
|----|------|
| 缓存归属 | Worker 独占（不加跨线程锁） |
| 触发范围 | 全部几何变更：改字、depth/bevel/fillet/inflate、字体、tess、动画路径 |
| 首帧 | 也异步；与运行期同一路径（可能短暂无字） |
| 文字 debounce | 保留约 200ms（少发订单；latest-only 仍生效） |
| 过期任务 | 算完再丢（不做挤出中途打断） |
| 失败 | 不替换 GPU，保留上一成功 Mesh |
| HUD | 构建中显示 busy（如 `Build WORK`） |
| 字体 | Worker 内自管 `FontFace`，路径变化再 load |
| Cache 统计 | 结果里带回快照，供 HUD |

## 数据流

```
主线程                              工作线程
  几何变更 / debounce 到期
  submit(req)  gen++  ─────────►  取 pending（覆盖旧 pending）
  继续 draw 当前 GPU              layout（cache/pool 仅此处）
  poll_result()  ◄─────────────  若 gen==latest → 写入完成槽
  成功 → upload → 换盘              否则丢弃结果
  失败/过期 → 不换盘
```

## 请求与结果

**请求快照**（提交时拷贝，worker 不读 UI 活状态）：

- `generation`
- `text`、`font_path`
- `depth` / `bevel` / `fillet` / `inflate`
- `tess_mode`、`flatness`、`scale`
- `per_glyph`、`write_cache`

**结果**（仅最新 generation 可被主线程取走）：

- `ok`、`BuildResult`、`RebuildTimings`
- `use_per_glyph` + `glyphs` 或 `merged_mesh`
- 半径 / inflate 元数据
- cache/mesh 命中率快照（供 HUD）
- 回显参数（applied 文本与几何字段）

## Latest-only 规则

1. 每次 `submit`：`generation++`，覆盖尚未开始的 pending；更新 `latest`
2. Worker 完成时：仅当 `result.generation == latest` 才写入完成槽
3. 主线程 `poll`：仅取 `generation == latest` 的完成结果
4. 正在计算的旧任务跑完即可，结果丢弃（不算真取消）

## 主线程状态机（简）

- `reload_mesh` / 参数变化 → `submit`（非阻塞）
- 每帧 `poll` → 有最新成功结果则 `upload_*` 并更新 `applied_*` / perf
- 上传前不 `clear` 当前 GPU 内容（避免黑屏）
- `glyphs` CPU 侧：仅在成功 apply 时替换；构建中继续用旧 GPU 路径绘制

## 文件

- 新增：`src/mesh/async_mesh_builder.h` / `.cpp`
- 改：`src/main.cpp`（submit/poll/apply；热路径不再同步挤出）
- 轻改：`debug_ui`（busy）、`CMakeLists.txt`
- 不改：`layout_text*` / 挤出算法本身（仅改调用线程）

## 非目标

- 挤出算法中途抢占取消
- GPU Buffer 复用 / 零上传（#11）
- 多 worker 并行
- 文本槽位 diff（#9 池命中足够）
- GL 共享 Context / 渲染线程迁移

## 验收对照

| Backlog 验收 | 实现要点 |
|--------------|----------|
| Mesh 构建离开主线程 | CPU layout 仅在 worker |
| 连续输入可丢弃旧任务 | generation + 完成校验 |
| 完成前显示旧 Mesh，一次性替换 | 成功前不 upload；成功后整批 upload |
| 无黑屏 / 半成品 / 旧字闪回 | 失败不换盘；只应用 latest |
| 主线程不被重建长时间阻塞 | 主线程仅 submit/poll/upload |

## 风险与注意

- 启动首帧异步：短暂可能无字，属预期
- Worker 持有的 `shared_ptr<const Mesh>` 随结果移交主线程后，池内条目仍可由 worker 淘汰；主线程持有的 `shared_ptr` 保证几何存活至下次替换
- FreeType：构建用 worker 内那份即可
- **必做**：进入主循环前将 `applied_font_index` / `applied_anim_index` / `applied_tess_backend` 与当前 `edit` 对齐。若保持 `-1`，每帧不等式会反复 `submit`，latest-only 会丢掉所有结果，画面永久空白
- **必做**：几何参数以「相对上次已提交请求」去重；主循环先 `poll` 再 `submit`。若每帧因 `edit!=applied` 就 `submit`，会清掉刚完成的结果，拖滑条时永远看不到更新
