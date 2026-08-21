/*
 * ImGui 面板实现：右上角性能 HUD + 左下参数窗。
 */

#include "ui/debug_ui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
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
                   bool use_per_glyph, const BuildResult* last_build, bool mesh_build_busy) {
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
        // 日常只看帧率、网格规模、上次重建总耗时；分阶段细节按需展开
        ImGui::Text("FPS        %7.1f", perf.ui_fps);
        ImGui::Text("Frame      %7.2f ms", perf.ui_frame_ms);
        ImGui::Text("Verts/Tris %5d / %5d", perf.verts, perf.tris);
        ImGui::Text("Rebuild    %7.2f ms", perf.rebuild.total_ms);
        if (mesh_build_busy) {
            ImGui::Text("Build      WORK (async)");
        } else if (last_build) {
            if (last_build->ok) {
                ImGui::Text("Build      OK");
                if (!last_build->fallback_reason.empty()) {
                    ImGui::TextWrapped("Note  %s", last_build->fallback_reason.c_str());
                }
            } else {
                ImGui::Text("Build      FAIL @ %s", build_stage_name(last_build->stage));
                if (last_build->glyph_index != 0) {
                    ImGui::Text("Glyph      %u", last_build->glyph_index);
                }
                if (!last_build->fallback_reason.empty()) {
                    ImGui::TextWrapped("Reason %s", last_build->fallback_reason.c_str());
                }
            }
        }
        if (ImGui::TreeNode("Timing details")) {
            // 比例字体下空格补齐会抖：标签固定列宽，数值在固定宽度内右对齐
            const float label_w =
                ImGui::CalcTextSize("Offscreen").x + ImGui::GetStyle().ItemSpacing.x;
            const float value_w = ImGui::CalcTextSize("0000.00 ms").x;
            const auto timing_row = [label_w, value_w](const char* label, float ms) {
                const float x0 = ImGui::GetCursorPosX();
                ImGui::TextUnformatted(label);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2f ms", ms);
                ImGui::SameLine(x0 + label_w + value_w - ImGui::CalcTextSize(buf).x);
                ImGui::TextUnformatted(buf);
            };
            timing_row("Render Sub", perf.render_submit_ms);
            timing_row("Text Draw", perf.text_draw_ms);
            timing_row("Offscreen", perf.offscreen_pass_ms);
            timing_row("Compose", perf.offscreen_compose_ms);
            timing_row("Present", perf.present_ms);
            timing_row("2D", perf.rebuild.stage_2d_ms);
            timing_row("3D", perf.rebuild.stage_3d_ms);
            timing_row("Merge", perf.rebuild.stage_merge_ms);
            timing_row("Upload", perf.rebuild.stage_upload_ms);
            ImGui::Text("GPU Up    skip=%d up=%d unique=%d bytes=%llu",
                        perf.upload_slots_skipped, perf.upload_slots_uploaded,
                        perf.upload_gpu_unique_buffers,
                        static_cast<unsigned long long>(perf.upload_bytes));
            ImGui::Text("Draw Call logic=%d  gl=%d%s", perf.draw_batches, perf.gl_draw_calls,
                        use_per_glyph ? "  (per-glyph)" : "  (merged)");
            ImGui::Separator();
            ImGui::Text("Outline hit %5.1f%%  %llu/%llu", perf.cache_outline.hit_rate * 100.f,
                        static_cast<unsigned long long>(perf.cache_outline.hits),
                        static_cast<unsigned long long>(perf.cache_outline.lookups));
            ImGui::Text("Planar  hit %5.1f%%  %llu/%llu", perf.cache_planar.hit_rate * 100.f,
                        static_cast<unsigned long long>(perf.cache_planar.hits),
                        static_cast<unsigned long long>(perf.cache_planar.lookups));
            ImGui::Text("Mesh    hit %5.1f%%  %llu/%llu", perf.cache_mesh.hit_rate * 100.f,
                        static_cast<unsigned long long>(perf.cache_mesh.hits),
                        static_cast<unsigned long long>(perf.cache_mesh.lookups));
            ImGui::Text("Cache      %.2f / %.0f MB  (o=%d p=%d m=%d)",
                        perf.cache_outline.mb_used + perf.cache_planar.mb_used +
                            perf.cache_mesh.mb_used,
                        perf.cache_outline.mb_limit + perf.cache_planar.mb_limit +
                            perf.cache_mesh.mb_limit,
                        perf.cache_outline.entries, perf.cache_planar.entries,
                        perf.cache_mesh.entries);
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::Text("Tess       %s",
                    edit.tess_backend == 0   ? "auto"
                    : edit.tess_backend == 1 ? "earcut"
                                            : "libtess2");
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
            const char* tess_labels[] = {"auto", "earcut", "libtess2"};
            if (ImGui::Combo("Tessellator", &edit.tess_backend, tess_labels, 3)) {
                edit.tess_backend_changed = true;
            }
            ImGui::TextDisabled("auto: earcut then libtess2 on fail");
        }

        ImGui::SliderFloat("Depth", &edit.depth, 1.f, 80.f, "%.1f");
        if (ImGui::IsItemActive()) {
            edit.cache_write_geometry = false;
        }
        {
            if (edit.bevel < 0.f) {
                edit.bevel = 0.f;
            }
            if (edit.bevel > 1.f) {
                edit.bevel = 1.f;
            }
            if (edit.fillet < 0.f) {
                edit.fillet = 0.f;
            }
            if (edit.fillet > 1.f) {
                edit.fillet = 1.f;
            }
            if (ImGui::SliderFloat("Bevel", &edit.bevel, 0.f, 1.f, "%.2f")) {
                if (edit.bevel > 0.f) {
                    edit.fillet = 0.f;
                }
            }
            if (ImGui::IsItemActive()) {
                edit.cache_write_geometry = false;
            }
            ImGui::TextDisabled("Strength 0~1; R = strength x per-glyph safe radius");
            if (ImGui::SliderFloat("Fillet", &edit.fillet, 0.f, 1.f, "%.2f")) {
                if (edit.fillet > 0.f) {
                    edit.bevel = 0.f;
                }
            }
            if (ImGui::IsItemActive()) {
                edit.cache_write_geometry = false;
            }
            ImGui::TextDisabled("Round edge; mutually exclusive with Bevel");
            if (edit.applied_r_max > 0.f || edit.applied_r_min > 0.f) {
                if (std::fabs(edit.applied_r_max - edit.applied_r_min) < 1e-3f) {
                    ImGui::TextDisabled("Applied R = %.2f (font units)", edit.applied_r_min);
                } else {
                    ImGui::TextDisabled("Applied R = %.2f .. %.2f (per glyph)", edit.applied_r_min,
                                        edit.applied_r_max);
                }
            }
            if (edit.safe_r_min > 0.f) {
                ImGui::TextDisabled("Safe R@1.0 min = %.2f", edit.safe_r_min);
            }
            if (edit.inflate < 0.f) {
                edit.inflate = 0.f;
            }
            if (edit.inflate > 1.f) {
                edit.inflate = 1.f;
            }
            ImGui::SliderFloat("Inflate", &edit.inflate, 0.f, 1.f, "%.2f");
            if (ImGui::IsItemActive()) {
                edit.cache_write_geometry = false;
            }
            ImGui::TextDisabled("Cap bulge; Ctrl+Click to type, Enter commits cache");
            if (edit.applied_inflate_h > 0.f) {
                ImGui::TextDisabled("Applied H = %.2f (font units)", edit.applied_inflate_h);
            }
            if (!edit.cache_write_geometry) {
                ImGui::TextDisabled("Cache write paused (preview only)");
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
                ImGui::Checkbox("Triplanar (object)", &edit.use_triplanar);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "On: sample by object-space XYZ (sticks with anim, seamless faces)\n"
                        "Off: mesh UVs (front planar / side perimeter)");
                }
                if (edit.use_triplanar) {
                    ImGui::SliderFloat("TP Scale", &edit.triplanar_scale, 0.002f, 5.f, "%.3f");
                    ImGui::SliderFloat("TP Sharpness", &edit.triplanar_sharpness, 1.f, 16.f,
                                       "%.1f");
                }
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
        ImGui::Separator();
        ImGui::Checkbox("Offscreen canvas (#14)", &edit.use_offscreen_canvas);
        if (edit.use_offscreen_canvas) {
            const char* canvas_labels[] = {"720p height", "1080p height"};
            ImGui::Combo("Canvas height", &edit.canvas_size_index, canvas_labels, 2);
            ImGui::TextDisabled("Width follows window aspect (bench keeps 16:9)");
        } else {
            ImGui::TextDisabled("Direct window draw (legacy path)");
        }
    }
    ImGui::End();
}

void debug_ui_end_frame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace text3d
