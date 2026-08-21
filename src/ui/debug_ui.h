#pragma once
/*
 * ImGui 调试面板：性能 HUD、文字/着色/动画参数、Play。
 */

#include "text/font_catalog.h"
#include "render/light.h"
#include "render/pbr_material.h"
#include "render/renderer.h"
#include "anims/text_anim.h"
#include "perf/perf_stats.h"
#include "mesh/build_result.h"

#include <vector>

struct GLFWwindow;

namespace text3d {

struct EditParams {
    char text[512] = "Hello";
    float depth = 24.f;
    float bevel = 0.f;   // 平倒角强度 0~1
    float fillet = 0.f;  // 真圆角强度 0~1
    float inflate = 0.f; // Cap Inflate 强度 0~1
    float applied_r_min = 0.f;
    float applied_r_max = 0.f;
    float safe_r_min = 0.f;
    float applied_inflate_h = 0.f;  // rebuild 后实际拱高（取各字最大）
    bool unlock_vsync = false;
    bool use_offscreen_canvas = true;  // #14：默认离屏画布
    int canvas_size_index = 0;         // 0=720p 高，1=1080p 高；交互宽度跟窗口 aspect
    bool text_edited = false;

    /* 几何滑条 Active（拖动或 Ctrl+输入中）时为 false：预览 rebuild 但不 put L1/Mesh */
    bool cache_write_geometry = true;

    int tess_backend = 0;  // 0=auto(earcut→libtess2)，1=earcut，2=libtess2
    bool tess_backend_changed = false;

    ShadingModel shading = ShadingModel::Pbr;
    DirectionalLight light;
    float albedo[3] = {0.92f, 0.88f, 0.78f};
    float metallic = 0.f;
    float roughness = 0.45f;
    float shininess = 32.f;
    float opacity = 0.28f;
    float env_strength = 1.6f;

    const std::vector<PbrMaterialEntry>* pbr_materials = nullptr;
    int pbr_material_index = 0;  // 0 = None（用上面常量）
    bool pbr_material_changed = false;
    bool request_rescan_materials = false;
    bool use_triplanar = true;        // 物体空间三平面（跟动画走）
    float triplanar_scale = 0.02f;
    float triplanar_sharpness = 4.f;

    const std::vector<AnimOption>* anims = nullptr;
    int anim_index = 1;
    bool anim_changed = false;
    bool request_play = false;

    const std::vector<FontEntry>* fonts = nullptr;
    int font_index = 0;
    bool font_changed = false;
};

bool debug_ui_init(GLFWwindow* window, const char* cjk_font_path);
void debug_ui_shutdown();
void debug_ui_begin_frame();
void debug_ui_draw(const PerfStats& perf, EditParams& edit, bool anim_playing,
                   bool use_per_glyph, const BuildResult* last_build = nullptr,
                   bool mesh_build_busy = false);
void debug_ui_end_frame();

}  // namespace text3d
