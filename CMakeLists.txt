# Text3DDemo 构建入口。
# 依赖：先 python3 scripts/fetch_deps.py 拉取 third_party；本机需 GLFW。
cmake_minimum_required(VERSION 3.20)
project(Text3DDemo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(_TEXT3D_REQUIRED_DEPS
    third_party/glad/src/gl.c
    third_party/freetype/CMakeLists.txt
    third_party/harfbuzz/CMakeLists.txt
    third_party/imgui/imgui.cpp
    third_party/glm/glm/glm.hpp
    third_party/libtess2/Source/tess.c
    third_party/earcut/earcut.hpp
    third_party/clipper2/CPP/Clipper2Lib/include/clipper2/clipper.h
    third_party/stb/stb_image.h
)
foreach(_f IN LISTS _TEXT3D_REQUIRED_DEPS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
        message(FATAL_ERROR
            "缺少依赖: ${_f}\n"
            "请先执行: python3 scripts/fetch_deps.py\n"
            "（清单见 deps.json）")
    endif()
endforeach()

# glad：OpenGL 3.3 Core 函数加载
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# 三角化双后端：earcut（header-only）+ libtess2，运行时 UI 可切换
file(GLOB LIBTESS2_SOURCES third_party/libtess2/Source/*.c)
add_library(libtess2 STATIC ${LIBTESS2_SOURCES})
target_include_directories(libtess2 PUBLIC third_party/libtess2/Include)

add_library(earcut INTERFACE)
target_include_directories(earcut INTERFACE third_party/earcut)

# Clipper2：2D 多边形布尔（字形重叠裁剪）
set(CLIPPER2_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/clipper2/CPP/Clipper2Lib)
add_library(clipper2 STATIC
    ${CLIPPER2_ROOT}/src/clipper.engine.cpp
    ${CLIPPER2_ROOT}/src/clipper.offset.cpp
    ${CLIPPER2_ROOT}/src/clipper.rectclip.cpp
)
target_include_directories(clipper2 PUBLIC ${CLIPPER2_ROOT}/include)
set_target_properties(clipper2 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE third_party/glm)

find_package(glfw3 REQUIRED)

# Dear ImGui：调试 HUD + 参数面板
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw)

# FreeType：随仓编译；关掉与 HarfBuzz 的反向依赖，避免循环
set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/freetype)

# HarfBuzz：仅需 hb-ft（检测到已有 freetype target 会自动开启），关闭 CoreText / subset / utils
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/harfbuzz)

add_executable(Text3DDemo
    src/main.cpp
    src/mesh/mesh_extrude.cpp
    src/mesh/glyph_planar.cpp
    src/mesh/glyph_geometry_cache.cpp
    src/mesh/glyph_mesh_pool.cpp
    src/mesh/async_mesh_builder.cpp
    src/mesh/mesh_types.cpp
    src/mesh/build_result.cpp
    src/mesh/contour_clean.cpp
    src/mesh/mesh_geom.cpp
    src/mesh/mesh_inflate.cpp
    src/mesh/mesh_offset_tess.cpp
    src/mesh/edge_chamfer.cpp
    src/mesh/edge_fillet.cpp
    src/mesh/outline_boolean.cpp
    src/text/ft_outline.cpp
    src/text/hb_shaper.cpp
    src/text/text_layout.cpp
    src/text/utf8.cpp
    src/text/font_catalog.cpp
    src/ui/debug_ui.cpp
    src/perf/perf_stats.cpp
    src/render/renderer.cpp
    src/render/gpu_mesh_pool.cpp
    src/render/offscreen_canvas.cpp
    src/render/screen_pass.cpp
    src/render/texture.cpp
    src/render/pbr_material.cpp
    src/anims/text_anim.cpp
    src/anims/appear_spin.cpp
    src/bench/bench_runner.cpp
    src/bench/bench_report.cpp
)

target_include_directories(Text3DDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

# 路径宏用正斜杠，避免 Windows 反斜杠变成 C 字符串转义（如 \t）
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders" _TEXT3D_SHADER_DIR)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets" _TEXT3D_ASSETS_DIR)

if(APPLE)
    set(_TEXT3D_DEFAULT_FONT "/System/Library/Fonts/Supplemental/Arial.ttf")
    set(_TEXT3D_ARABIC_FONT "/System/Library/Fonts/SFArabic.ttf")
elseif(WIN32)
    set(_TEXT3D_DEFAULT_FONT "C:/Windows/Fonts/arial.ttf")
    # Arabic Typesetting；缺失时 main 会跳过，仍可用 assets/fonts
    set(_TEXT3D_ARABIC_FONT "C:/Windows/Fonts/arabtype.ttf")
else()
    set(_TEXT3D_DEFAULT_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    set(_TEXT3D_ARABIC_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
endif()

# 着色器 / 资产路径：默认指向源码树，运行时可命令行覆盖字体
target_compile_definitions(Text3DDemo PRIVATE
    TEXT3D_SHADER_DIR="${_TEXT3D_SHADER_DIR}"
    TEXT3D_ASSETS_DIR="${_TEXT3D_ASSETS_DIR}"
    TEXT3D_DEFAULT_FONT="${_TEXT3D_DEFAULT_FONT}"
    TEXT3D_ARABIC_FONT="${_TEXT3D_ARABIC_FONT}"
)

if(MSVC)
    target_compile_options(Text3DDemo PRIVATE /utf-8)
endif()

find_package(OpenGL REQUIRED)

target_link_libraries(Text3DDemo PRIVATE
    glad
    libtess2
    earcut
    clipper2
    glm
    glfw
    imgui
    freetype
    harfbuzz
    OpenGL::GL
)

# 拷贝着色器到可执行文件旁，便于从 build/ 直接运行
add_custom_command(TARGET Text3DDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:Text3DDemo>/shaders
)

# 轮廓布尔冒烟测试（无 GLFW）
add_executable(OutlineBooleanTest
    tests/outline_boolean_test.cpp
    src/mesh/outline_boolean.cpp
    src/mesh/contour_clean.cpp
)
target_include_directories(OutlineBooleanTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(OutlineBooleanTest PRIVATE clipper2)
set_target_properties(OutlineBooleanTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# 阿拉伯倒角镂空诊断（无 GLFW）
add_executable(ArabicBevelDiag
    tests/arabic_bevel_diag.cpp
    src/mesh/mesh_extrude.cpp
    src/mesh/glyph_planar.cpp
    src/mesh/mesh_types.cpp
    src/mesh/build_result.cpp
    src/mesh/contour_clean.cpp
    src/mesh/mesh_geom.cpp
    src/mesh/mesh_inflate.cpp
    src/mesh/mesh_offset_tess.cpp
    src/mesh/edge_chamfer.cpp
    src/mesh/edge_fillet.cpp
    src/mesh/outline_boolean.cpp
    src/text/ft_outline.cpp
    src/text/hb_shaper.cpp
    src/text/utf8.cpp
    src/perf/perf_stats.cpp
)
target_include_directories(ArabicBevelDiag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(ArabicBevelDiag PRIVATE
    libtess2
    earcut
    clipper2
    freetype
    harfbuzz
)
set_target_properties(ArabicBevelDiag PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# 侧面法线与几何法线一致性
add_executable(SideNormalTest
    tests/side_normal_test.cpp
    src/mesh/mesh_extrude.cpp
    src/mesh/glyph_planar.cpp
    src/mesh/mesh_types.cpp
    src/mesh/build_result.cpp
    src/mesh/contour_clean.cpp
    src/mesh/mesh_geom.cpp
    src/mesh/mesh_inflate.cpp
    src/mesh/mesh_offset_tess.cpp
    src/mesh/edge_chamfer.cpp
    src/mesh/edge_fillet.cpp
    src/mesh/outline_boolean.cpp
    src/text/ft_outline.cpp
    src/perf/perf_stats.cpp
)
target_include_directories(SideNormalTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(SideNormalTest PRIVATE
    libtess2
    earcut
    clipper2
    freetype
)
set_target_properties(SideNormalTest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(SideNormalTest PRIVATE
    TEXT3D_DEFAULT_FONT="${_TEXT3D_DEFAULT_FONT}"
)
