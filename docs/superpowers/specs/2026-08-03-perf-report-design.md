# #15 固定场景性能报告设计

日期：2026-08-03  
工程：`3d-text-demo/`  
状态：已落地  
依赖：#1（埋点）、#14（离屏画布）  
Backlog：`3D文字Demo优化Backlog.md` #15  

## 目标

用**同一操作序列**在 Debug / RelWithDebInfo / Release 下跑固定场景，产出可对比报告；对照 Release 门槛，未达标时写清瓶颈与下一步。

## 跑法

```text
Text3DDemo --bench [--out <path.md>] [--frames N] [--warmup W]
```

- 进程内自动驱动（不依赖外部 UI 自动化）
- 强制：离屏画布开；可选场景内切 720p/1080p
- 跑完写报告并退出；退出码 0 = 采完（门槛 FAIL 仍写进报告，不因此非 0）
- 三档构建：同一序列分别跑，报告文件名带 config

辅助：`scripts/run_perf_matrix.sh` 对已有或现编三档二进制依次 `--bench`。

## 场景表


| ID | 覆盖 | 操作摘要 |
|----|------|----------|
| `plain` | 普通文字 | `"Hello"`，Anim=None，merged，canvas 720p，稳态采样 |
| `complex` | 复杂文字 | `"赢龘田回国"`，同上 |
| `typing` | 连续输入 | 连续改字串（多步），测 submit→apply 可见延迟 |
| `geom` | 几何参数 | depth / inflate 阶梯变化，测 rebuild 与延迟 |
| `xform` | 材质与变换 | 转相机 + 改 albedo；断言无新的 mesh rebuild |
| `canvas_1080` | 1080p 画布 | 同 `plain`，canvas 1080p |
| `anim` | 动画对比 | Appear Spin 逐字 vs 前序 None；比 Draw Call / 帧耗时 |

每场景：`warmup` 帧（默认 30）→ `sample` 帧（默认 120）。`typing`/`geom` 另采延迟样本。

## 指标

| 指标 | 来源 |
|------|------|
| frame P50/P95/P99 | 每帧 `ui_frame_ms` |
| 画布 FPS | `1000 / P50(frame)` 或均值 FPS |
| text_draw / offscreen / present / render_submit | 已有 PerfStats |
| 缓存命中率 | outline / planar / mesh |
| Draw Call | `draw_batches` / `gl_draw_calls` |
| 每帧上传 | `upload_bytes`（及 skip/up） |
| 输入→可见延迟 | submit 时刻 → apply 完成墙钟 ms |
| 主线程阻塞代理 | apply 上传耗时 + 当帧 `render_submit_ms` 的分布（异步构建下主线程不应扛 stage_2d/3d） |

## Release 门槛（报告对照）

- 画布 FPS ≈ 60（`plain` @720p，VSync 开时以帧时为准）
- 720p `ui_frame` **P95 ≤ 16.6ms**（`plain`）
- 主线程阻塞代理 **P95 ≤ 4ms**（稳态 `plain` 的 `render_submit`；含 apply 的帧单独标注）

VSync 开启时 FPS 会被锁 ~60；报告同时给出 **frame P95** 与 **unlock 可选**（bench 默认关 VSync 以便量真耗时，报告注明）。

**Bench 默认：关 VSync**，避免刷新率掩盖差异；报告头写明。

## 报告格式

Markdown（主交付），路径默认：

`docs/perf/<date>-<build_type>.md`

结构：

1. 环境：OS、GPU 简述、build type、commit、VSync off、canvas  
2. 门槛表：PASS/FAIL  
3. 分场景表：P50/P95/P99、FPS、DC、upload、cache、latency  
4. 未达标：优化前（可填 N/A）、优化后（本次）、剩余瓶颈、下一步（模板）

可选同目录 `.json` 便于以后对比（本轮若时间紧可只做 MD）。

## 模块

| 模块 | 职责 |
|------|------|
| `src/bench/bench_runner.*` | 场景状态机、改 EditParams、采样、汇总百分位 |
| `src/bench/bench_report.*` | 写 Markdown |
| `main.cpp` | 解析 argv；`--bench` 时走 runner，跳过/精简 ImGui 交互 |
| `scripts/run_perf_matrix.sh` | 三档构建+跑分（可 `--skip-build`） |

百分位：对采样数组 sort 后取分位（线性插值或 nearest）。

## 与现有代码关系

- 复用 `PerfStats`、`AsyncMeshBuilder` poll/apply、#14 canvas  
- Bench 不改网格算法；只驱动参数与记数  
- `xform`：记录场景开始/结束的 `rebuild.total_ms` 触发次数或 generation，期望 0 次新成功 apply（或仅相机不动网格）

## 非目标

- CI 卡门槛失败  
- #11 容量复用  
- 底图输入帧  
- GUI 录屏  

## 验收对照

| Backlog | 本设计 |
|---------|--------|
| 场景覆盖七类 | 上表 7 场景 |
| P50/P95/P99 等 | 采样 + 报告列 |
| 三档同一序列 | matrix 脚本 + 同 `--bench` |
| Release 门槛 | 报告门槛表 |
| 未达标说明 | 报告第 4 节模板 |
