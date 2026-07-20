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
    src/mesh/mesh_geom.cpp
    src/mesh/mesh_offset_tess.cpp
    src/mesh/edge_chamfer.cpp
    src/mesh/edge_fillet.cpp
    src/text/ft_outline.cpp
    src/text/hb_shaper.cpp
    src/text/text_layout.cpp
    src/text/utf8.cpp
    src/text/font_catalog.cpp
    src/ui/debug_ui.cpp
    src/render/renderer.cpp
    src/render/texture.cpp
    src/render/pbr_material.cpp
    src/anims/text_anim.cpp
    src/anims/appear_spin.cpp
)

target_include_directories(Text3DDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

# 着色器 / 资产路径：默认指向源码树，运行时可命令行覆盖字体
target_compile_definitions(Text3DDemo PRIVATE
    TEXT3D_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    TEXT3D_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    TEXT3D_DEFAULT_FONT="/System/Library/Fonts/Supplemental/Arial.ttf"
    TEXT3D_ARABIC_FONT="/System/Library/Fonts/SFArabic.ttf"
)

target_link_libraries(Text3DDemo PRIVATE
    glad
    libtess2
    earcut
    glm
    glfw
    imgui
    freetype
    harfbuzz
)

if(APPLE)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(Text3DDemo PRIVATE ${OPENGL_LIBRARY})
endif()

# 拷贝着色器到可执行文件旁，便于从 build/ 直接运行
add_custom_command(TARGET Text3DDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:Text3DDemo>/shaders
)
