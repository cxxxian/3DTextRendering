#pragma once
/*
 * ImGui 调试面板：性能 HUD、文字/着色/动画参数、Play。
 */

#include "text/font_catalog.h"
#include "render/light.h"
#include "render/pbr_material.h"
#include "render/renderer.h"
#include "anims/text_anim.h"

#include <vector>

struct GLFWwindow;

namespace text3d {

struct PerfStats {
    float fps = 0.f;
    float frame_ms = 0.f;
    int verts = 0;
    int tris = 0;
    float rebuild_ms = 0.f;
};

struct EditParams {
    char text[512] = "Hello";
    float depth = 24.f;
    float bevel = 0.f;   // 平倒角
    float fillet = 0.f;  // 真圆角
    float edge_r_cap = 0.f;  // rebuild 后由 layout 写入的安全半径上限（0=未知）
    bool unlock_vsync = false;
    bool text_edited = false;

    int tess_backend = 0;  // 0=earcut，1=libtess2
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
                   bool use_per_glyph);
void debug_ui_end_frame();

}  // namespace text3d
