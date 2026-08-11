# third_party

本目录的依赖**实体不进 Git**，由清单拉取：

```bash
python3 scripts/fetch_deps.py
```

清单与版本见仓库根目录 `deps.json`。

拉取后大致布局：

| 目录 | 用途 |
|------|------|
| `freetype/` | 字体 outline |
| `harfbuzz/` | shaping（hb-ft） |
| `glm/` | 数学 |
| `imgui/` | 调试 UI |
| `libtess2/` | 三角化后端 |
| `earcut/` | 三角化后端（header） |
| `clipper2/` | 2D 多边形布尔（重叠裁剪） |
| `stb/` | stb_image |
| `glad/` | OpenGL 3.3 loader（glad2 生成） |

系统依赖仍需本机安装：GLFW（macOS：`brew install glfw`；Windows：`vcpkg install glfw3:x64-windows`；Linux：发行版 `libglfw3-dev`）。
