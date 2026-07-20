/*
 * 入口：开窗、整句/逐字双路径绘制、动画采样、ImGui。
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#endif

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ui/debug_ui.h"
#include "text/font_catalog.h"
#include "text/ft_outline.h"
#include "mesh/mesh_extrude.h"
#include "mesh/mesh_offset_tess.h"
#include "render/camera.h"
#include "render/pbr_material.h"
#include "render/renderer.h"
#include "anims/text_anim.h"
#include "text/text_layout.h"

namespace {

struct AppState {
    text3d::OrbitCamera camera;
    bool orbiting = false;  // 左键绕目标旋转
    bool panning = false;   // 右键平移目标
    double last_x = 0.0;
    double last_y = 0.0;

    text3d::EditParams edit;
    text3d::PerfStats perf;

    std::string applied_text = "Hello";
    float applied_depth = 24.f;
    float applied_bevel = 0.f;
    float applied_fillet = 0.f;
    int applied_font_index = -1;
    int applied_anim_index = -1;
    int applied_tess_backend = -1;

    bool reload_mesh = true;
    bool reload_font = false;
    bool text_pending = false;
    double text_edit_time = 0.0;
    bool vsync_unlocked = false;

    std::vector<text3d::FontEntry> fonts;
    std::vector<text3d::AnimOption> anim_options;
    std::vector<text3d::GlyphInstance> glyphs;
    text3d::TextAnimPlayer anim_player;
    bool use_per_glyph = false;

    std::vector<text3d::PbrMaterialEntry> pbr_material_entries;
    text3d::PbrMaterialGpu active_pbr_material;
    int loaded_pbr_material_index = 0;  // 0 = 无贴图材质

    double last_frame_time = 0.0;
    float fps_smooth = 0.f;
};

AppState g_state;

constexpr double kTextDebounceSec = 0.2;

bool imgui_wants_mouse() {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

bool imgui_wants_keyboard() {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
}

void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h > 0 ? h : 1);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    if (imgui_wants_mouse()) {
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_state.orbiting = true;
            g_state.panning = false;
            glfwGetCursorPos(window, &g_state.last_x, &g_state.last_y);
        } else if (action == GLFW_RELEASE) {
            g_state.orbiting = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            g_state.panning = true;
            g_state.orbiting = false;
            glfwGetCursorPos(window, &g_state.last_x, &g_state.last_y);
        } else if (action == GLFW_RELEASE) {
            g_state.panning = false;
        }
    }
}

void cursor_pos_callback(GLFWwindow*, double x, double y) {
    if (imgui_wants_mouse() || (!g_state.orbiting && !g_state.panning)) {
        return;
    }
    const double dx = x - g_state.last_x;
    const double dy = y - g_state.last_y;
    g_state.last_x = x;
    g_state.last_y = y;

    if (g_state.orbiting) {
        g_state.camera.yaw_deg += static_cast<float>(dx) * 0.3f;
        g_state.camera.pitch_deg += static_cast<float>(dy) * 0.3f;
        g_state.camera.pitch_deg = std::clamp(g_state.camera.pitch_deg, -89.f, 89.f);
        return;
    }

    // 右键：在相机右/上平面平移 target
    auto& cam = g_state.camera;
    const glm::vec3 eye = cam.eye();
    glm::vec3 forward = cam.target - eye;
    if (glm::dot(forward, forward) < 1e-12f) {
        return;
    }
    forward = glm::normalize(forward);
    const glm::vec3 world_up(0.f, 1.f, 0.f);
    glm::vec3 right = glm::cross(forward, world_up);
    if (glm::dot(right, right) < 1e-12f) {
        right = glm::vec3(1.f, 0.f, 0.f);
    } else {
        right = glm::normalize(right);
    }
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float scale = cam.distance * 0.0015f;
    cam.target += right * static_cast<float>(dx) * scale;
    cam.target -= up * static_cast<float>(dy) * scale;
}

void scroll_callback(GLFWwindow*, double, double yoffset) {
    if (imgui_wants_mouse()) {
        return;
    }
    g_state.camera.distance *= (yoffset > 0.0) ? 0.9f : 1.1f;
    g_state.camera.distance = std::clamp(g_state.camera.distance, 0.5f, 20.f);
}

void key_callback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }
    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }
    if (imgui_wants_keyboard()) {
        return;
    }
    if (key == GLFW_KEY_SPACE) {
        // Hello → مرحبا（阿拉伯）→ A 循环；切阿拉伯时尽量选 SF Arabic
        const bool is_hello = std::strcmp(g_state.edit.text, "Hello") == 0;
        const bool is_arabic = std::strcmp(g_state.edit.text, u8"مرحبا") == 0;
        if (is_hello) {
            std::strncpy(g_state.edit.text, u8"مرحبا", sizeof(g_state.edit.text) - 1);
            for (int i = 0; i < static_cast<int>(g_state.fonts.size()); ++i) {
                if (g_state.fonts[static_cast<size_t>(i)].id == "sf-arabic") {
                    g_state.edit.font_index = i;
                    g_state.edit.font_changed = true;
                    break;
                }
            }
        } else if (is_arabic) {
            std::strncpy(g_state.edit.text, "A", sizeof(g_state.edit.text) - 1);
        } else {
            std::strncpy(g_state.edit.text, "Hello", sizeof(g_state.edit.text) - 1);
        }
        g_state.edit.text[sizeof(g_state.edit.text) - 1] = '\0';
        g_state.reload_mesh = true;
        g_state.text_pending = false;
        return;
    }
}

/* 按当前动画是否需要逐字，重建几何并上传 GPU */
bool rebuild_and_upload(text3d::FontFace& font, text3d::Renderer& renderer) {
    const auto t0 = std::chrono::steady_clock::now();

    const text3d::TessBackend backend =
        (g_state.edit.tess_backend == 1) ? text3d::TessBackend::Libtess2
                                         : text3d::TessBackend::Earcut;
    text3d::set_tess_backend(backend);

    text3d::LayoutOptions opt;
    opt.extrude.depth = g_state.edit.depth;
    opt.extrude.bevel = g_state.edit.bevel;
    opt.extrude.fillet = g_state.edit.fillet;
    opt.flatness = 0.4f;
    opt.scale = 1.0f / 128.0f;
    float edge_r_cap = 0.f;
    opt.out_edge_r_cap = &edge_r_cap;

    const std::string text = g_state.edit.text;
    // 换几何时打断正在播的片段，避免旧时间轴套到新字上
    g_state.anim_player.stop_idle();
    g_state.glyphs.clear();

    if (text.empty()) {
        renderer.upload_mesh({});
        g_state.use_per_glyph = false;
        g_state.perf.verts = 0;
        g_state.perf.tris = 0;
        edge_r_cap = 0.f;
    } else if (g_state.anim_player.needs_per_glyph()) {
        // 逐字 mesh：才能各自绕字心做 model 变换
        if (!text3d::layout_text_glyphs(font, text, opt, g_state.glyphs)) {
            std::cerr << "[main] layout_text_glyphs 失败\n";
            return false;
        }
        renderer.upload_glyphs(g_state.glyphs);
        g_state.use_per_glyph = true;
        g_state.perf.verts = renderer.vertex_count();
        g_state.perf.tris = renderer.triangle_count();
    } else {
        // 整句合并，一次 draw
        text3d::Mesh mesh;
        if (!text3d::layout_text(font, text, opt, mesh)) {
            std::cerr << "[main] layout_text 失败\n";
            return false;
        }
        renderer.upload_mesh(mesh);
        g_state.use_per_glyph = false;
        g_state.perf.verts = renderer.vertex_count();
        g_state.perf.tris = renderer.triangle_count();
    }

    const auto t1 = std::chrono::steady_clock::now();
    g_state.perf.rebuild_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    g_state.applied_text = text;
    g_state.applied_depth = g_state.edit.depth;
    g_state.applied_bevel = g_state.edit.bevel;
    g_state.applied_fillet = g_state.edit.fillet;
    g_state.applied_font_index = g_state.edit.font_index;
    g_state.applied_anim_index = g_state.edit.anim_index;
    g_state.applied_tess_backend = g_state.edit.tess_backend;
    g_state.edit.edge_r_cap = edge_r_cap;
    // 不重置 camera.target，保留右键平移

    std::cout << "[main] rebuild path=" << (g_state.use_per_glyph ? "per-glyph" : "merged")
              << " tess=" << text3d::tess_backend_name(backend)
              << " → " << g_state.perf.rebuild_ms << " ms"
              << " verts=" << g_state.perf.verts << " tris=" << g_state.perf.tris
              << " edge_r_cap=" << edge_r_cap << "\n";
    return true;
}

void set_vsync_enabled(bool enabled) {
    glfwSwapInterval(enabled ? 1 : 0);
#if defined(__APPLE__)
    CGLContextObj ctx = CGLGetCurrentContext();
    if (ctx) {
        GLint sync = enabled ? 1 : 0;
        CGLSetParameter(ctx, kCGLCPSwapInterval, &sync);
    }
#endif
}

void apply_vsync_if_needed(GLFWwindow*) {
    if (g_state.edit.unlock_vsync == g_state.vsync_unlocked) {
        return;
    }
    g_state.vsync_unlocked = g_state.edit.unlock_vsync;
    set_vsync_enabled(!g_state.vsync_unlocked);
}

void pace_frame_if_vsync_on(double frame_start) {
    if (g_state.edit.unlock_vsync) {
        return;
    }
    constexpr double kTargetDt = 1.0 / 60.0;
    for (;;) {
        const double elapsed = glfwGetTime() - frame_start;
        if (elapsed >= kTargetDt) {
            break;
        }
        const double remain = kTargetDt - elapsed;
        if (remain > 0.0015) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remain * 0.7));
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* cli_font = nullptr;
    if (argc >= 2) {
        cli_font = argv[1];
    }

    g_state.fonts.push_back(
        text3d::FontEntry{"system", "System Arial", TEXT3D_DEFAULT_FONT});
    g_state.fonts.push_back(
        text3d::FontEntry{"sf-arabic", "SF Arabic", TEXT3D_ARABIC_FONT});
    {
        const auto assets = text3d::scan_font_assets({TEXT3D_ASSETS_DIR});
        g_state.fonts.insert(g_state.fonts.end(), assets.begin(), assets.end());
    }
    if (cli_font) {
        g_state.fonts.insert(g_state.fonts.begin() + 1,
                             text3d::FontEntry{"cli", "CLI Font", cli_font});
        g_state.edit.font_index = 1;
    }
    g_state.edit.fonts = &g_state.fonts;

    g_state.anim_options = text3d::list_anim_options();
    g_state.edit.anims = &g_state.anim_options;
    g_state.edit.anim_index = 1;  // Appear Spin
    g_state.anim_player.set_by_index(g_state.edit.anim_index);

    const std::string materials_root =
        std::string(TEXT3D_ASSETS_DIR) + "/textures/materials";
    g_state.pbr_material_entries = text3d::scan_pbr_materials(materials_root);
    g_state.edit.pbr_materials = &g_state.pbr_material_entries;
    for (int i = 0; i < static_cast<int>(g_state.pbr_material_entries.size()); ++i) {
        if (g_state.pbr_material_entries[static_cast<size_t>(i)].name == "demo_checker") {
            g_state.edit.pbr_material_index = i + 1;
            break;
        }
    }

    const char* imgui_cjk = nullptr;
    for (const auto& fe : g_state.fonts) {
        if (fe.id == "187086") {
            imgui_cjk = fe.path.c_str();
            break;
        }
    }

    const std::string& start_font =
        g_state.fonts[static_cast<size_t>(g_state.edit.font_index)].path;

    std::cout << "=== 3D Text Rendering Demo ===\n"
              << "启动字体: " << start_font << "\n"
              << "操作: 拖拽旋转 | Animation 下拉 + Play | Esc 退出\n";

    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window =
        glfwCreateWindow(960, 640, "3D Text — FreeType outline extrude", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        return 1;
    }

    set_vsync_enabled(true);
    g_state.vsync_unlocked = false;

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    if (!text3d::debug_ui_init(window, imgui_cjk)) {
        return 1;
    }

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.12f, 0.14f, 0.18f, 1.f);

    text3d::FontFace font;
    if (!font.load(start_font, 128)) {
        return 1;
    }
    font.dump_info();
    g_state.applied_font_index = g_state.edit.font_index;

    text3d::Renderer renderer;
    std::string shader_dir = TEXT3D_SHADER_DIR;
    if (!renderer.init(shader_dir)) {
        shader_dir = "shaders";
        if (!renderer.init(shader_dir)) {
            return 1;
        }
    }

    g_state.last_frame_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - g_state.last_frame_time);
        g_state.last_frame_time = now;
        g_state.perf.frame_ms = dt * 1000.f;
        if (dt > 0.f) {
            const float instant_fps = 1.f / dt;
            g_state.fps_smooth = (g_state.fps_smooth <= 0.f)
                                     ? instant_fps
                                     : (g_state.fps_smooth * 0.9f + instant_fps * 0.1f);
            g_state.perf.fps = g_state.fps_smooth;
        }

        text3d::debug_ui_begin_frame();
        g_state.edit.text_edited = false;
        g_state.edit.request_play = false;
        g_state.edit.font_changed = false;
        g_state.edit.anim_changed = false;
        g_state.edit.tess_backend_changed = false;
        g_state.edit.pbr_material_changed = false;
        g_state.edit.request_rescan_materials = false;
        text3d::debug_ui_draw(g_state.perf, g_state.edit, g_state.anim_player.playing(),
                              g_state.use_per_glyph);

        if (g_state.edit.request_rescan_materials) {
            g_state.pbr_material_entries = text3d::scan_pbr_materials(materials_root);
            g_state.edit.pbr_materials = &g_state.pbr_material_entries;
            if (g_state.edit.pbr_material_index >
                static_cast<int>(g_state.pbr_material_entries.size())) {
                g_state.edit.pbr_material_index = 0;
                g_state.edit.pbr_material_changed = true;
            }
        }

        if (g_state.edit.pbr_material_changed ||
            g_state.edit.pbr_material_index != g_state.loaded_pbr_material_index) {
            g_state.active_pbr_material.destroy();
            g_state.loaded_pbr_material_index = 0;
            if (g_state.edit.pbr_material_index > 0 &&
                g_state.edit.pbr_material_index <=
                    static_cast<int>(g_state.pbr_material_entries.size())) {
                const auto& entry = g_state.pbr_material_entries[static_cast<size_t>(
                    g_state.edit.pbr_material_index - 1)];
                if (text3d::load_pbr_material(entry.dir, entry.name,
                                              g_state.active_pbr_material)) {
                    g_state.loaded_pbr_material_index = g_state.edit.pbr_material_index;
                } else {
                    g_state.edit.pbr_material_index = 0;
                }
            }
        }

        if (g_state.edit.anim_changed ||
            g_state.edit.anim_index != g_state.applied_anim_index) {
            g_state.anim_player.set_by_index(g_state.edit.anim_index);
            g_state.reload_mesh = true;
        }

        if (g_state.edit.tess_backend_changed ||
            g_state.edit.tess_backend != g_state.applied_tess_backend) {
            g_state.reload_mesh = true;
        }

        if (g_state.edit.font_changed) {
            g_state.reload_font = true;
        }
        if (g_state.edit.text_edited) {
            g_state.text_pending = true;
            g_state.text_edit_time = now;
        }
        if (std::fabs(g_state.edit.depth - g_state.applied_depth) > 1e-4f ||
            std::fabs(g_state.edit.bevel - g_state.applied_bevel) > 1e-4f ||
            std::fabs(g_state.edit.fillet - g_state.applied_fillet) > 1e-4f) {
            g_state.reload_mesh = true;
        }
        if (g_state.text_pending && (now - g_state.text_edit_time) >= kTextDebounceSec) {
            g_state.text_pending = false;
            if (std::string(g_state.edit.text) != g_state.applied_text) {
                g_state.reload_mesh = true;
            }
        }

        if (g_state.reload_font) {
            const auto& fe = g_state.fonts[static_cast<size_t>(g_state.edit.font_index)];
            if (!font.load(fe.path, 128)) {
                g_state.edit.font_index = g_state.applied_font_index;
            } else {
                font.dump_info();
                g_state.reload_mesh = true;
            }
            g_state.reload_font = false;
        }

        if (g_state.reload_mesh) {
            rebuild_and_upload(font, renderer);
            g_state.reload_mesh = false;
        }

        if (g_state.edit.request_play && g_state.use_per_glyph) {
            g_state.anim_player.play();
            std::cout << "[main] Anim Play\n";
        }

        g_state.anim_player.update(dt);

        apply_vsync_if_needed(window);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glfwGetFramebufferSize(window, &fbw, &fbh);
        const float aspect = (fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.f;

        const glm::mat4 view = g_state.camera.view();
        const glm::mat4 proj = g_state.camera.projection(aspect);

        text3d::DrawParams draw_params;
        draw_params.shading = g_state.edit.shading;
        draw_params.light = g_state.edit.light;
        draw_params.camera_pos = g_state.camera.eye();
        draw_params.albedo = glm::vec3(g_state.edit.albedo[0], g_state.edit.albedo[1],
                                       g_state.edit.albedo[2]);
        draw_params.metallic = g_state.edit.metallic;
        draw_params.roughness = g_state.edit.roughness;
        draw_params.shininess = g_state.edit.shininess;
        draw_params.opacity = g_state.edit.opacity;
        draw_params.env_strength = g_state.edit.env_strength;
        if (g_state.loaded_pbr_material_index > 0 &&
            g_state.active_pbr_material.has_albedo &&
            draw_params.shading == text3d::ShadingModel::Pbr) {
            draw_params.albedo_map = g_state.active_pbr_material.albedo.id;
            if (g_state.active_pbr_material.has_orm) {
                draw_params.orm_map = g_state.active_pbr_material.orm.id;
                draw_params.orm_layout =
                    (g_state.active_pbr_material.packed_layout ==
                     text3d::PackedMrLayout::GltfMr)
                        ? 1
                        : 0;
            }
            if (g_state.active_pbr_material.has_normal) {
                draw_params.normal_map = g_state.active_pbr_material.normal.id;
            }
        }

        if (g_state.use_per_glyph) {
            // 动画只给标量；矩阵在此按相机方向组装
            text3d::AnimSample sample;
            g_state.anim_player.sample(sample);

            glm::vec3 toward_cam = g_state.camera.eye() - g_state.camera.target;
            if (glm::dot(toward_cam, toward_cam) > 1e-8f) {
                toward_cam = glm::normalize(toward_cam);
            } else {
                toward_cam = glm::vec3(0.f, 0.f, 1.f);
            }
            // approach: 1→0 时，近处偏移从最大收到 0
            const glm::vec3 near_offset =
                toward_cam * (sample.near_distance * sample.approach);

            for (int i = 0; i < static_cast<int>(g_state.glyphs.size()); ++i) {
                const auto& g = g_state.glyphs[static_cast<size_t>(i)];
                glm::mat4 model(1.f);
                // 几何本地原点已是字心，绕 Y = 绕字自身转
                model = glm::translate(
                    model, glm::vec3(g.rest_x, g.rest_y, 0.f) + near_offset);
                model = glm::rotate(model, sample.angle_y, glm::vec3(0.f, 1.f, 0.f));
                const glm::mat4 mvp = proj * view * model;
                renderer.draw_glyph(i, glm::value_ptr(mvp), glm::value_ptr(model),
                                    draw_params);
            }
        } else {
            const glm::mat4 model(1.f);
            const glm::mat4 mvp = proj * view * model;
            renderer.draw(glm::value_ptr(mvp), glm::value_ptr(model), draw_params);
        }

        text3d::debug_ui_end_frame();
        glfwSwapBuffers(window);
        glfwPollEvents();
        pace_frame_if_vsync_on(now);
    }

    text3d::debug_ui_shutdown();
    g_state.active_pbr_material.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
