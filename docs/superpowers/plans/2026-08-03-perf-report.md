# #15 Perf Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** `Text3DDemo --bench` 自动跑固定场景，采 P50/P95/P99 等，写出 Markdown 报告；提供三档 matrix 脚本。

**Architecture:** `BenchRunner` 状态机改 EditParams/采 PerfStats；`BenchReport` 写 MD；main 解析 `--bench` 并接入帧循环。

**Tech Stack:** C++17, 现有 PerfStats / AsyncMeshBuilder / #14 canvas

**Spec:** `docs/superpowers/specs/2026-08-03-perf-report-design.md`

## Global Constraints

- Bench 默认关 VSync；强制离屏画布
- 门槛 FAIL 只写报告，退出码仍 0
- 不改网格算法

---

### Task 1: Bench 核心 + 报告

**Files:**
- Create: `src/bench/bench_runner.h/.cpp`, `src/bench/bench_report.h/.cpp`
- Modify: `CMakeLists.txt`

- [x] 百分位、场景表、采样汇总
- [x] Markdown 报告（门槛表 + 分场景）

---

### Task 2: main 接入 + matrix 脚本

**Files:**
- Modify: `src/main.cpp`
- Create: `scripts/run_perf_matrix.sh`
- Create: `docs/perf/.gitkeep`（或首次跑出报告）

- [x] `--bench [--out] [--frames] [--warmup]`
- [x] 帧循环驱动 runner；跑完写报告退出
- [x] matrix 脚本

---

### Task 3: 跑一次 Release + Backlog

- [x] Release `--bench` 生成报告
- [x] 勾 Backlog #15；更新 spec 状态
