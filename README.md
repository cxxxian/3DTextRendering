# 3D Text Demo

基于 FreeType 矢量轮廓的 3D 文字渲染 Demo：排版 → 轮廓折线化 → 三角化挤出 → OpenGL 着色显示，并带 ImGui 调试面板与简单入场动画。

## 功能概览

- FreeType 读取字形 outline，曲线细分为折线
- HarfBuzz shaping（含阿拉伯文等复杂文种）
- 挤出管线：直边 / Bevel / Fillet；棱强度 0~1；**Inflate** 字面鼓包（PS Cap 风格，帽面中点细分后再鼓）
- Mesh 分区：Front / Back / Side / Bevel / Rounded（可查 index 范围）
- 三角化：`TessMode` 随请求传入（auto / earcut / libtess2）；auto 失败时 Earcut→libtess2 回退
- 着色：Lambert / Phong / PBR / Glass
- 整句合并绘制与逐字绘制（入场动画）
- ImGui：参数面板、性能 HUD、字体/材质扫描

## 依赖

第三方库**不进仓库**，版本见根目录 `deps.json`，克隆后拉取到本地 `third_party/`：

| 组件 | 获取方式 |
|------|----------|
| FreeType / HarfBuzz / glm / imgui / libtess2 / earcut / stb / glad | `python3 scripts/fetch_deps.py` |
| GLFW | 本机安装（macOS：`brew install glfw`） |
| OpenGL | 系统（macOS 使用 OpenGL.framework） |

系统要求：CMake ≥ 3.20，C++17，Python3（拉取依赖），C99。大图/字体通过 Git LFS 管理。

## 编译与运行

```bash
# 1) 本仓 LFS（若尚未初始化）
git lfs install --local
git lfs pull

# 2) 拉取第三方依赖（首次或换机器必做）
python3 scripts/fetch_deps.py

# 3) 配置与编译
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/Text3DDemo
```

指定字体：

```bash
./build/Text3DDemo /path/to/font.ttf
```

固定场景性能报告（#15）：

```bash
# 单次（默认关 VSync，写 Markdown）
./build/Text3DDemo --bench --build-type Release --out docs/perf/$(date +%F)-Release.md

# 可选：--warmup 30 --frames 120
# 三档依次构建并跑：
./scripts/run_perf_matrix.sh
# 已编好二进制时：
./scripts/run_perf_matrix.sh --skip-build
```

强制重新下载依赖：

```bash
python3 scripts/fetch_deps.py --force
```

## 操作

| 键鼠 / UI | 作用 |
|-----------|------|
| 左键拖拽 | 轨道旋转（鼠标在 ImGui 上时不抢） |
| 滚轮 | 缩放 |
| 空格 | 切换预设文本（输入框未聚焦时） |
| ImGui Text Params | 字体、字符串、厚度、着色、PBR 材质、动画、Play |
| Unlock FPS | 关闭 VSync / 软件限帧 |
| Esc | 退出 |

## 资源约定

- 字体包：`assets/fonts/<id>/`（含 `material.json` 与 `package/` 下的 ttf/otf）
- PBR 材质：`assets/textures/materials/<name>/`  
  约定：`diffuse|albedo` 必需；`ao_rough_metal|orm|arm` 或分拆 metallic/roughness；`normal_gl|normal` 可选

## 目录结构

```text
deps.json                    第三方版本清单
scripts/fetch_deps.py        按清单拉取到 third_party/
src/                         业务代码
shaders/                     GLSL
assets/                      字体与材质（LFS）
third_party/                 拉取产物（gitignore，不进仓）
```
