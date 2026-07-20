/*
 * ImGui 面板实现：右上角性能 HUD + 左下参数窗。
 */

#include "ui/debug_ui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cfloat>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace text3d {

bool debug_ui_init(GLFWwindow* window, const char* cjk_font_path) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    bool loaded_cjk = false;
    if (cjk_font_path && cjk_font_path[0] != '\0') {
        ImFontConfig cfg;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        if (io.Fonts->AddFontFromFileTTF(cjk_font_path, 18.0f, &cfg, ranges) != nullptr) {
            loaded_cjk = true;
            std::cout << "[debug_ui] ImGui 中文字体: " << cjk_font_path << "\n";
        } else {
            std::cerr << "[debug_ui] 加载 ImGui 中文字体失败\n";
        }
    }
    if (!loaded_cjk) {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.f;
    style.FrameRounding = 2.f;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        return false;
    }
    return true;
}

void debug_ui_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void debug_ui_begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void debug_ui_draw(const PerfStats& perf, EditParams& edit, bool anim_playing,
                   bool use_per_glyph) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;

    const float pad = 10.f;
    ImGui::SetNextWindowPos(ImVec2(display.x - pad, pad), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags hud_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##perf_hud", nullptr, hud_flags)) {
        ImGui::Text("FPS        %7.1f", perf.fps);
        ImGui::Text("Frame      %7.2f ms", perf.frame_ms);
        ImGui::Text("Verts/Tris %5d / %5d", perf.verts, perf.tris);
        ImGui::Separator();
        ImGui::Text("Rebuild    %7.2f ms", perf.rebuild_ms);
        ImGui::Text("Tess       %s", edit.tess_backend == 0 ? "earcut" : "libtess2");
        ImGui::Text("Draw path  %s", use_per_glyph ? "per-glyph" : "merged");
        if (edit.anims && edit.anim_index >= 0 &&
            edit.anim_index < static_cast<int>(edit.anims->size())) {
            ImGui::Text("Anim       %s",
                        (*edit.anims)[static_cast<size_t>(edit.anim_index)].label.c_str());
        }
        if (use_per_glyph) {
            ImGui::Text("State      %s", anim_playing ? "Playing" : "Idle");
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pad, display.y - pad), ImGuiCond_FirstUseEver, ImVec2(0.f, 1.f));
    ImGui::SetNextWindowSize(ImVec2(360.f, 0.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Text Params")) {
        if (edit.fonts && !edit.fonts->empty()) {
            const char* preview = (*edit.fonts)[static_cast<size_t>(edit.font_index)].label.c_str();
            if (ImGui::BeginCombo("Font", preview)) {
                for (int i = 0; i < static_cast<int>(edit.fonts->size()); ++i) {
                    const bool selected = (i == edit.font_index);
                    if (ImGui::Selectable((*edit.fonts)[static_cast<size_t>(i)].label.c_str(),
                                          selected)) {
                        if (i != edit.font_index) {
                            edit.font_index = i;
                            edit.font_changed = true;
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::TextUnformatted("Text");
        if (ImGui::InputTextMultiline(
                "##text", edit.text, sizeof(edit.text),
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5.0f))) {
            edit.text_edited = true;
        }
        if (ImGui::Button("Hello")) {
            std::strncpy(edit.text, "Hello", sizeof(edit.text) - 1);
            edit.text[sizeof(edit.text) - 1] = '\0';
            edit.text_edited = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"مرحبا")) {
            std::strncpy(edit.text, u8"مرحبا", sizeof(edit.text) - 1);
            edit.text[sizeof(edit.text) - 1] = '\0';
            edit.text_edited = true;
            if (edit.fonts) {
                for (int i = 0; i < static_cast<int>(edit.fonts->size()); ++i) {
                    if ((*edit.fonts)[static_cast<size_t>(i)].id == "sf-arabic") {
                        if (edit.font_index != i) {
                            edit.font_index = i;
                            edit.font_changed = true;
                        }
                        break;
                    }
                }
            }
        }
        ImGui::TextDisabled("Space cycles Hello / Arabic / A; use SF Arabic font");

        {
            const char* tess_labels[] = {"earcut", "libtess2"};
            if (ImGui::Combo("Tessellator", &edit.tess_backend, tess_labels, 2)) {
                edit.tess_backend_changed = true;
            }
            ImGui::TextDisabled("Switch rebuilds mesh for A/B compare");
        }

        ImGui::SliderFloat("Depth", &edit.depth, 1.f, 80.f, "%.1f");
        {
            float r_max = std::max(0.f, edit.depth * 0.5f - 0.05f);
            if (edit.edge_r_cap > 0.f) {
                r_max = std::min(r_max, edit.edge_r_cap);
            }
            if (edit.bevel > r_max) {
                edit.bevel = r_max;
            }
            if (edit.fillet > r_max) {
                edit.fillet = r_max;
            }
            if (ImGui::SliderFloat("Bevel", &edit.bevel, 0.f, r_max, "%.2f")) {
                if (edit.bevel > 0.f) {
                    edit.fillet = 0.f;
                }
            }
            ImGui::TextDisabled("Flat chamfer (~45°)");
            if (ImGui::SliderFloat("Fillet", &edit.fillet, 0.f, r_max, "%.2f")) {
                if (edit.fillet > 0.f) {
                    edit.bevel = 0.f;
                }
            }
            ImGui::TextDisabled("Round edge; max limited by stroke width");
            if (edit.edge_r_cap > 0.f) {
                ImGui::TextDisabled("Safe R <= %.2f (min stroke / depth)", r_max);
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Shading");
        const char* shading_labels[] = {"Lambert", "Phong", "PBR", "Glass"};
        int shading_i = static_cast<int>(edit.shading);
        if (ImGui::Combo("Model", &shading_i, shading_labels, 4)) {
            edit.shading = static_cast<ShadingModel>(shading_i);
            if (edit.shading == ShadingModel::Glass) {
                // 切到玻璃时给一组更易看出透明感的默认值
                edit.roughness = 0.05f;
                edit.opacity = 0.32f;
                edit.env_strength = 1.8f;
                edit.albedo[0] = 0.55f;
                edit.albedo[1] = 0.92f;
                edit.albedo[2] = 0.88f;
            }
        }

        ImGui::ColorEdit3("Albedo", edit.albedo);
        ImGui::ColorEdit3("Light Color", &edit.light.color.x);
        ImGui::SliderFloat3("Light Dir", &edit.light.direction.x, -1.f, 1.f, "%.2f");
        ImGui::SliderFloat("Ambient", &edit.light.ambient, 0.f, 0.5f, "%.3f");

        if (edit.shading == ShadingModel::Phong) {
            ImGui::SliderFloat("Shininess", &edit.shininess, 1.f, 128.f, "%.0f");
        }
        if (edit.shading == ShadingModel::Glass) {
            ImGui::SliderFloat("Roughness", &edit.roughness, 0.04f, 0.35f, "%.2f");
            ImGui::SliderFloat("Opacity", &edit.opacity, 0.05f, 0.85f, "%.2f");
            ImGui::SliderFloat("Env Strength", &edit.env_strength, 0.2f, 4.f, "%.2f");
            ImGui::TextDisabled("Proc. studio env via Fresnel (no SSR yet)");
        }
        if (edit.shading == ShadingModel::Pbr) {
            if (edit.pbr_materials) {
                const char* mat_preview =
                    (edit.pbr_material_index <= 0 ||
                     edit.pbr_material_index >
                         static_cast<int>(edit.pbr_materials->size()))
                        ? "None (uniforms)"
                        : (*edit.pbr_materials)[static_cast<size_t>(edit.pbr_material_index - 1)]
                              .name.c_str();
                if (ImGui::BeginCombo("PBR Material", mat_preview)) {
                    if (ImGui::Selectable("None (uniforms)", edit.pbr_material_index == 0)) {
                        if (edit.pbr_material_index != 0) {
                            edit.pbr_material_index = 0;
                            edit.pbr_material_changed = true;
                        }
                    }
                    for (int i = 0; i < static_cast<int>(edit.pbr_materials->size()); ++i) {
                        const int idx = i + 1;
                        const bool selected = (edit.pbr_material_index == idx);
                        if (ImGui::Selectable((*edit.pbr_materials)[static_cast<size_t>(i)].name.c_str(),
                                              selected)) {
                            if (edit.pbr_material_index != idx) {
                                edit.pbr_material_index = idx;
                                edit.pbr_material_changed = true;
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Rescan Materials")) {
                    edit.request_rescan_materials = true;
                }
                ImGui::TextDisabled("Drop folder under assets/textures/materials/");
            }
            if (edit.pbr_material_index <= 0) {
                ImGui::SliderFloat("Metallic", &edit.metallic, 0.f, 1.f, "%.2f");
                ImGui::SliderFloat("Roughness", &edit.roughness, 0.04f, 1.f, "%.2f");
            } else {
                ImGui::TextDisabled("Maps: Diffuse + AO/Rough/Metal + Normal(GL)");
            }
        }

        if (edit.anims && !edit.anims->empty()) {
            const char* anim_preview =
                (*edit.anims)[static_cast<size_t>(edit.anim_index)].label.c_str();
            if (ImGui::BeginCombo("Animation", anim_preview)) {
                for (int i = 0; i < static_cast<int>(edit.anims->size()); ++i) {
                    const bool selected = (i == edit.anim_index);
                    if (ImGui::Selectable((*edit.anims)[static_cast<size_t>(i)].label.c_str(),
                                          selected)) {
                        if (i != edit.anim_index) {
                            edit.anim_index = i;
                            edit.anim_changed = true;
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        const bool can_play =
            edit.anims && edit.anim_index > 0 &&
            edit.anim_index < static_cast<int>(edit.anims->size()) &&
            (*edit.anims)[static_cast<size_t>(edit.anim_index)].needs_per_glyph;
        if (can_play) {
            if (ImGui::Button("Play")) {
                edit.request_play = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(anim_playing ? "playing..." : "Play selected anim");
        }

        ImGui::Checkbox("Unlock FPS (disable VSync)", &edit.unlock_vsync);
        if (!edit.unlock_vsync) {
            ImGui::TextDisabled("Locked ~60 FPS");
        }
    }
    ImGui::End();
}

void debug_ui_end_frame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace text3d
