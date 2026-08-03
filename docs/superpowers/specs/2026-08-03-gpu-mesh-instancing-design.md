# #12 逐字 GPU Mesh 共享 / 实例化 — 设计

日期：2026-08-03  
工程：`3d-text-demo/`  
状态：已落地  
依赖：#9（CPU Mesh 同字共享）、#11（槽位零上传；容量复用仍暂缓）

## 0. 易混点（槽 / Entry / 矩阵）

「槽不再拥有 VBO」**不等于**「没有 VBO」。VBO 还在，只是换了主人。

| 角色 | 有什么 |
|------|--------|
| **GpuMeshEntry（池里）** | 真正的 VAO / VBO / EBO —— 几何数据 |
| **GpuGlyphSlot（每个可见字）** | 只记「我用哪份几何」：`fingerprint` + `mesh_ptr` + `index_count` |

`abcc` 时大致是：

```
池：
  Entry(a) → VBO_a
  Entry(b) → VBO_b
  Entry(c) → VBO_c     ← 只有一份 c

槽：
  slot0 → fingerprint(a)
  slot1 → fingerprint(b)
  slot2 → fingerprint(c)  ─┐
  slot3 → fingerprint(c)  ─┘ 两个槽指向同一 Entry
```

实例化画 `c`：绑同一份 `VBO_c`，再喂 2 个 `mat4`，一次 `DrawElementsInstanced(..., 2)`。

**所有字母都走同一套池**（不要求 `shared_ptr` 引用计数 >1 才进池）。`a`/`b` 各自一份 Entry，画时 `instance_count=1`；与普通 `DrawElements` 成本几乎一样。若只对重复字分叉，会多出两套上传/销毁路径，且独字仍要有 GPU buffer。

**GpuGlyphSlot 不是「多一套矩阵输入」：**

| | 存什么 | 何时变 |
|--|--------|--------|
| Slot | 指向哪份 GPU 几何 | apply / upload |
| 每帧 mat4 | 这个字画在哪、转多少 | 每帧由 `rest_x/y` + 动画算 |

Slot =「第 i 个字用哪块几何」；Instance mat4 =「第 i 个字这一帧的位姿」；Entry =「那块几何在 GPU 上的 buffer」。

## 1. 目标

- 逐字路径下，重复 glyph **共享一份 GPU Mesh**（同一套 VBO/EBO）
- 逐字动画保留 **独立变换**，不复制几何缓冲
- **优先实例化**：同几何多字用 `glDrawElementsInstanced` 合批
- 与静态合批（merged 整句）路径可对比说明

## 2. 非目标

- #11 暂缓的 Buffer 容量复用（`glBufferSubData` 扩缩容）
- #13 正式 Draw Call 验收（本项只预埋 `draw_batches` 计数）
- 后厨线程 upload（OpenGL Context 仍在主线程）
- 跨 ShadingModel 合批
- 改 CPU Mesh / #9 MeshKey / 异步构建契约

## 3. 与 #9 / #11 的边界

| | #9 CPU | #11 零上传 | #12（本项） |
|--|--------|-----------|------------|
| 共享对象 | `shared_ptr<const Mesh>` | — | `GpuMeshEntry`（VAO/VBO/EBO） |
| 判定单位 | MeshKey | **槽位 i** 与上次比 | **fingerprint** 全局去重 |
| Hello 首次 | 4 份 CPU Mesh | up=**5**（每槽各传） | up=**4**（唯一几何数） |
| Hel→Hello | CPU 池命中 `l` | 槽 3、4 新 → up=2 | `l` 命中 GPU 池 → 通常只传 `o` |

一句话：

> **#9**：CPU 同字一份 Mesh。  
> **#11**：槽没变就不重传该槽。  
> **#12**：同几何共用一块 GPU Buffer，并用实例化按几何合批绘制。

#11 槽位差分框架保留；#12 在「需要上传」时把「按槽 upload」改成「按唯一 fingerprint upload」。

## 4. 决策摘要

| 项 | 选择 |
|----|------|
| 方案 | **GPU Mesh 池 + Instanced Draw**（一次做完，不分 Phase A/B） |
| 池键 | 沿用 #11 的 `mesh_fingerprint()`（FNV-1a 64）；快路径 `shared_ptr` 指针相同 ⇒ 同键 |
| 槽位 | 不再拥有 VBO/EBO；存 `fingerprint` + `mesh_ptr` + `index_count`（Skip 零查池） |
| 池生命周期（第一版） | **当前 apply 结束后 ref=0 即删**；不做跨 apply LRU |
| 绘制 API | 新增 `draw_glyphs_instanced`；旧 `draw_glyph(i)` 可保留作调试，主路径改走实例化 |
| Shader | 共用 `pbr.vert`：加 `uVP` + instanced `aModel`；整句路径仍用 `uMVP`/`uModel` |
| Instance buffer | Renderer 内一个 `GL_DYNAMIC_DRAW` VBO，每批 orphan/`BufferData` 写入 `mat4[]` |
| Glass | 同一批 instanced draw 做两次（先 front cull 再 back），语义与现 `draw_with_` 一致 |
| 统计 | `gpu_unique`、`gpu_up`（唯一几何）、`gpu_skip`（槽）、`gpu_bytes`、`draw_batches` |

## 5. 架构

```
upload_glyphs (主线程 / apply 时)
  GlyphInstance[]
       │
       ├─ #11 档1：整表槽不变 → Skip，pool 不动
       └─ #11 档2：按槽差分
              │
              ├─ 收集本帧引用的 fingerprint 集合
              ├─ miss → GpuMeshPool.upload 一次
              ├─ hit  → 仅 ref++
              └─ 槽只更新 fingerprint / mesh_ptr
              │
              └─ 扫描：ref=0 的 entry 销毁 GL 对象

draw_glyphs_instanced (每帧)
  slots + 每字 mat4 model + DrawParams
       │
       ├─ 按 fingerprint 分组 → Batch{ entry, mat4[] }
       ├─ 每批：bind entry.vao → 写 instance VBO → DrawElementsInstanced
       └─ Glass：每批 ×2 pass
```

静态 merged 路径（`upload_mesh` / `draw`）**不变**。

## 6. 数据模型

### 6.1 GpuMeshEntry / GpuMeshPool

新建 `render/gpu_mesh_pool.h/.cpp`（或内嵌在 `renderer` 私有实现；优先独立文件，便于测与读）。

```cpp
struct GpuMeshEntry {
    std::uint64_t fingerprint = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    int index_count = 0;
    int ref_count = 0;  // 当前 apply 后有多少槽引用
};

// 职责：按 fingerprint 查找 / 创建 / 增减引用 / 销毁无引用项
class GpuMeshPool {
    // find_or_upload(fingerprint, mesh) → entry*
    // begin_apply() / end_apply(active_fingerprints) 或等价：
    //   上传循环里 acquire；结束后 release 未出现在本帧集合的 entry
};
```

第一版 **不做 byte_limit / LRU**。理由：逐字路径活跃集合 = 当前句唯一字形，数量通常很小；跨 apply 缓存价值有限且与 #11「槽删再建」语义纠缠。若后续测得「删字再打回」upload 仍偏多，再加 LRU（另开小项）。

### 6.2 GpuGlyphSlot（替换现 GpuGlyph）

```cpp
struct GpuGlyphSlot {
    std::uint64_t fingerprint = 0;
    const Mesh* mesh_ptr = nullptr;
    int index_count = 0;  // 来自 pool entry；整表 Skip 时不查池
    // 无 vao/vbo/ebo
};
```

`Renderer` 持有：

- `std::vector<GpuGlyphSlot> glyphs_`
- `GpuMeshPool pool_`
- `unsigned int instance_vbo_`（实例矩阵）
- 既有整句 `vao_/vbo_/ebo_` 不变

### 6.3 统计结构扩展

在既有 `GpuUploadStats` 上扩展：

| 字段 | 含义 |
|------|------|
| `kind` | Skip / Upload（本次是否有实际上传） |
| `bytes_uploaded` | 本次实际上传字节 |
| `slots_skipped` | 槽级 Skip 数（与 #11 相同） |
| `slots_uploaded` | **保留字段名兼容**；取值改为与 `meshes_uploaded` 相同（避免 HUD 两套 up） |
| `unique_meshes` | 当前活跃唯一 GPU Mesh 数（`gpu_unique`） |
| `meshes_uploaded` | 本次新写入 pool 的唯一几何数（Hello 首次 = 4） |
| `draw_batches` | 上一帧逐字 **逻辑批** 数（按几何合批；Glass 双 pass 仍计 1） |

HUD / `[perf]` 展示：

- `GPU Up skip=<slots_skipped> up=<meshes_uploaded> unique=<unique_meshes> bytes=...`
- 绘制侧：`DC=<draw_batches>`（给 #13 预埋）

**语义变更**：#11 的 `up` = 槽上传次数；#12 起 `up` = **唯一几何上传次数**。Hello 首次从 5 → 4。

## 7. 上传流程（`upload_glyphs`）

前置与 #11 相同：切到逐字时关掉整句绘制标志（`index_count_/merged_*` 清零语义保留）；整句 VAO 不删。

### 7.1 槽不变判定（沿用 #11）

```
slot_unchanged(src, gpu):
  无 mesh → gpu 空槽
  有 mesh → mesh_ptr 相同 或 fingerprint(*mesh)==gpu.fingerprint
```

### 7.2 档 1：整表 Skip

`new_n == old_n` 且全部 `slot_unchanged`：

- 不碰 pool、不 upload
- `last_upload_stats_ = { Skip, 0, new_n, 0, unique=当前池活跃数, meshes_uploaded=0 }`
- 更新 `vertex_count_` / `index_count_` 汇总（index 从 pool entry 读，或槽侧缓存 `index_count` 可选）

槽上已缓存 `index_count`（见 §6.2），Skip 路径零查池。

### 7.3 档 2：按槽差分 + 池去重

1. 扩容 `glyphs_` 若 `new_n > old_n`
2. 本 apply 使用集合 `used_fps: set<uint64_t>`
3. 对每个槽 `i`：
   - 无 mesh → 清空槽字段；continue
   - 若 `slot_unchanged`：`skipped++`；把 `fingerprint` 记入 `used_fps`；continue
   - 否则：算 `fp = mesh_fingerprint(*src.mesh)`
     - `pool.acquire(fp, *src.mesh)`：miss 则 `upload_into_` 并 `meshes_uploaded++` / 累加 bytes；hit 则零上传
     - 更新槽：`fingerprint/mesh_ptr/index_count`
     - `used_fps.insert(fp)`
4. 缩容：销毁多余槽记录（仅清槽，pool 在下一步统一收）
5. `pool.release_except(used_fps)`：不在集合内的 entry `ref` 清零并 `glDelete*`
6. 写 `last_upload_stats_`；打日志

**注意**：两个新槽同为 `l` 且 pool miss 时，只 upload **一次**，两槽都指向同一 entry。

### 7.4 与 `upload_mesh` 切换

`upload_mesh` 开头仍 `clear_glyphs_()`：清空槽 + **整池销毁**。  
`upload_glyphs` 开头仍使整句 merged 不可画（现逻辑）。

## 8. 绘制流程（实例化）

### 8.1 谁算矩阵

保持现状：`main.cpp` 按字组装 `mat4 model`（`rest_x/y` + `AnimSample` 的 approach/angle）。  
**推荐**：main 填好 `std::vector<glm::mat4> models`（与 glyphs 等长），调用：

```cpp
renderer.draw_glyphs_instanced(models.data(), n, view_proj, draw_params);
```

或 Renderer 提供接受 `GlyphInstance[] + AnimSample + camera` 的重载——为少耦合动画，**优先 main 传 matrices**。

### 8.2 合批

```
按 glyphs_[i].fingerprint 分组（顺序：稳定按首次出现顺序，保证同帧可复现）
每组：
  entry = pool.get(fp)
  instance_mats = 该组各槽的 model
  bind entry.vao
  上传 instance_vbo（mat4 数组）
  配置 attrib location 3..6，divisor=1
  DrawElementsInstanced(..., instance_count)
  Glass：重复第二遍 cull 策略
```

`draw_batches` = 按几何合批的**逻辑批**数（Glass 双 pass 仍计 1）。第一版不单独报 `gl_draw_calls`；需要时日志注明 Glass ×2。

### 8.3 Shader（`shaders/pbr.vert`）

目标：四套 frag 仍共用一个 vert。

```glsl
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in mat4 aModel;  // instanced；非实例路径可不绑

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uVP;
uniform int uUseInstance;  // 0=整句/旧路径，1=实例

void main() {
    mat4 model = (uUseInstance != 0) ? aModel : uModel;
    vec4 world = model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(model) * aNormal;
    vUV = aUV;
    if (uUseInstance != 0) {
        gl_Position = uVP * world;
    } else {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
}
```

整句 `draw`：`uUseInstance=0`，行为与现网一致。  
逐字 instanced：`uUseInstance=1`，设 `uVP`，绑 instance attrib。

`ProgramLocs` 增加 `uVP`、`uUseInstance`；`cache_locations_` 一并拉取。

### 8.4 Instance VBO 布局

- 每个 instance：1× `mat4`（列主序，与 glm 一致）
- `glVertexAttribPointer` 拆成 4× `vec4`，`glVertexAttribDivisor(i, 1)`
- 每批 `glBufferData(GL_DYNAMIC_DRAW)`（或 orphan：`nullptr` 再 `BufferSubData`）；第一版直接 `BufferData` 即可

### 8.5 旧 API

- `draw_glyph(int)`：可实现为「单 instance 批」或标记 deprecated；主路径不再调用
- `glyph_count()`：仍返回槽数

## 9. 路径对比（验收第 3 条）

| 路径 | 触发 | CPU | GPU Buffer | Draw | 变换 |
|------|------|-----|------------|------|------|
| Merged | 无动画 | 1 份合并 Mesh | 1 套 VBO | **1** | 顶点已含排版 或 统一 model |
| Per-glyph #12 | 动画 / needs_per_glyph | N 槽、U 份 CPU（#9） | **U 套 VBO** | **U 批**（≤N） | 每字 instance mat4 |
| Per-glyph #11 前 | （历史） | 同上 | **N 套 VBO** | **N** | 每槽 uniform |

示例：`Hello` + Appear Spin → U=4 → **4** instanced draws（`ll` 为 1 draw × 2 instances）。  
同文本切动画 None → merged → **1** draw。

## 10. 验收与手测

对应 Backlog：

| 验收项 | 判定 |
|--------|------|
| 重复 glyph 共享 GPU Mesh | `Hello` 首次 `meshes_uploaded=4`，`unique_meshes=4`；两 `l` 不双传 |
| 动画独立变换、不复制几何 | Appear Spin 时 `ll` 位姿正确；池内仅 1 份 `l` 的 VBO |
| 与静态合批可对比 | HUD/文档有上表；`draw_batches` 与 merged 的 1 DC 可对照 |

手测清单：

1. `Hello` 首次 apply：`up=4`（不是 5），`unique=4`
2. 删到 `Hel`：`unique=3`；再打 `lo`：`l` 若仍在句中则 `up≤1`（仅 `o`）；若已删光再打回，第一版可能 `up=2`（无跨 apply LRU，可接受）
3. Appear Spin：画面正确；改材质/相机不触发 upload
4. 切回 None：merged 单 draw，行为与现一致
5. Glass 材质：逐字透明双 pass 无异常

## 11. 文件改动

| 文件 | 变更 |
|------|------|
| `src/render/gpu_mesh_pool.h/.cpp` | 新建 |
| `src/render/renderer.h/.cpp` | 槽改 Slot；upload 接池；instanced draw；stats |
| `shaders/pbr.vert` | instance + `uVP` + `uUseInstance` |
| `src/main.cpp` | 逐字改为组装 matrices + `draw_glyphs_instanced` |
| `src/perf/perf_stats.*`、`src/ui/debug_ui.cpp` | HUD 字段 |
| `CMakeLists.txt` | 加入新源文件 |
| `文字渲染3.0.md` | 补 #12 章节（实现后或本轮文档一并） |

## 12. 风险与缓解

| 风险 | 缓解 |
|------|------|
| fingerprint 碰撞 | 与 #11 相同 FNV；极端可叠加 `mesh_ptr` 二次确认（同指针必同几何） |
| Instance attrib 与整句 VAO 冲突 | instance 布局只绑在 glyph entry 的 VAO 上，或 draw 前临时 setup；整句 VAO 不绑 location 3+ |
| Glass × instance | 复用现有双 pass 状态机，仅替换 `DrawElements` → `Instanced` |
| macOS GL 3.3 Core | `DrawElementsInstanced` / divisor 均可用 |
| 统计语义变化 | 文档与 HUD 文案明确「up=唯一几何」 |

## 13. 实现顺序（供后续 plan 拆任务）

1. `GpuMeshPool` + 改造 `upload_glyphs`（先仍用 `draw_glyph` 多次，验证共享上传）
2. `pbr.vert` + `draw_glyphs_instanced` + main 接线
3. HUD / perf 字段与手测
4. 更新 `文字渲染3.0.md` §6 → #12 正文

步骤 1 可本地冒烟（Hello up=4）后再做 2，避免上传与绘制同时翻车；对外仍算同一 #12 交付，不分两个 Backlog 项。
