# Joining Run Union Implementation Plan

> **For agentic workers:** Execute task-by-task. Steps use checkbox syntax.

**Goal:** Phase A2 改为重叠连通分量 + Clipper Union，每 run 一个挤出实例；更新 3.0 §9。

**Architecture:** `union_outlines` + layout 连通分量；`|run|=1` 走原缓存，`|run|>1` 跳过池。

**Tech Stack:** 现有 Clipper2 / text_layout

## Global Constraints

- 段内不再 Difference；动画按 run
- Union 失败 → 拆回逐字 + fallback_reason

---

### Task 1: union_outlines + 测试

- Modify: `outline_boolean.h/.cpp`, `tests/outline_boolean_test.cpp`
- [ ] 实现 `union_outlines`
- [ ] 两矩形并集面积约 150、测试 PASS
- [ ] Commit

### Task 2: Phase A2 → runs

- Modify: `text_layout.cpp`
- [ ] 连通分量 + Union；PreparedRun/`from_run_union`
- [ ] 编译 Text3DDemo
- [ ] Commit

### Task 3: 文档

- Modify: `文字渲染3.0.md`, Backlog, spec 状态
- [ ] §9 改为 joining run
- [ ] Commit
