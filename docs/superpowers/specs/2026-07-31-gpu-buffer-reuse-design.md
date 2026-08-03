# #11 GPU Buffer — 零上传（第一轮）设计

日期：2026-07-31  
工程：`3d-text-demo/`  
状态：已落地（零上传；容量复用仍暂缓）  
依赖：#10  

新手向完整说明见：`文字渲染3.0.md` §5。

## 目标

- Mesh 完全相同 → 零上传（整句整表 Skip；逐字按**槽位** Skip）
- HUD / `[perf]`：`gpu_up_skip` / `gpu_up` / `gpu_bytes`
- 容量复用（SubData）、同字母共用 GPU Buffer（#12）暂缓

## 语义（易混）

- **#11**：第 i 个槽和该槽上次一样 → Skip；新槽 / 变槽 → Upload  
- **不是**：全剧「同几何就可 Skip」（那是 #12）  
- Hello 首次 → up=5（两个 l 是两个槽）

## 判定

- fingerprint：FNV-1a 64；空 mesh = 0  
- 逐字：`shared_ptr` 指针相同或 fingerprint 相同  
- 整句：fingerprint + `merged_uploaded_once_`

## 行为

- `upload_mesh`：Skip 或 `glBufferData`；开头 `clear_glyphs_()`  
- `upload_glyphs`：不默认全 clear；档 1 整表 Skip；档 2 按槽差分；字少则删多余槽  

## 文件

`renderer.*`、`perf_stats.*`、`debug_ui.cpp`、`main.cpp`（apply 写 stats）
