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
#include "perf/perf_stats.h"
#include "mesh/build_result.h"
#include "mesh/async_mesh_builder.h"
#include "text/font_catalog.h"
#include "mesh/mesh_offset_tess.h"
#include "render/camera.h"
#include "render/pbr_material.h"
#include "render/renderer.h"
#include "render/offscreen_canvas.h"
#include "render/screen_pass.h"
#include "anims/text_anim.h"
#include "text/text_layout.h"
#include "bench/bench_runner.h"

#include <cstdlib>

namespace {

struct AppState {
    text3d::OrbitCamera camera;
    bool orbiting = false;  // 左键绕目标旋转
    bool panning = false;   // 右键平移目标
    double last_x = 0.0;
    double last_y = 0.0;

    text3d::EditParams edit;
    text3d::PerfStats perf;
    text3d::BuildResult last_build;

    std::string applied_text = "Hello";
    float applied_depth = 24.f;
    float applied_bevel = 0.f;
    float applied_fillet = 0.f;
    float applied_inflate = 0.f;
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

    text3d::AsyncMeshBuilder mesh_builder;
    bool mesh_build_inflight = false;
    bool has_submitted_req = false;
    text3d::AsyncMeshBuildRequest last_submitted_req;

    double last_frame_time = 0.0;
    float fps_smooth = 0.f;

    bool bench_mode = false;
    bool bench_just_applied = false;
    float bench_apply_upload_ms = 0.f;
    bool bench_request_orbit = false;
    text3d::BenchOptions bench_opt;
    text3d::BenchRunner bench_runner;
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

text3d::TessMode tess_mode_from_ui(int backend) {
    if (backend == 1) {
        return text3d::TessMode::Earcut;
    }
    if (backend == 2) {
        return text3d::TessMode::Libtess2;
    }
    return text3d::TessMode::Auto;
}

text3d::AsyncMeshBuildRequest make_mesh_build_request() {
    text3d::AsyncMeshBuildRequest req;
    req.text = g_state.edit.text;
    req.font_path = g_state.fonts[static_cast<size_t>(g_state.edit.font_index)].path;
    req.depth = g_state.edit.depth;
    req.bevel = g_state.edit.bevel;
    req.fillet = g_state.edit.fillet;
    req.inflate = g_state.edit.inflate;
    req.tess_mode = tess_mode_from_ui(g_state.edit.tess_backend);
    req.flatness = 0.4f;
    req.scale = 1.0f / 128.0f;
    req.per_glyph = g_state.anim_player.needs_per_glyph();
    req.write_cache = g_state.edit.cache_write_geometry;
    return req;
}

bool request_same_content(const text3d::AsyncMeshBuildRequest& a,
                          const text3d::AsyncMeshBuildRequest& b) {
    return a.text == b.text && a.font_path == b.font_path &&
           std::fabs(a.depth - b.depth) < 1e-4f && std::fabs(a.bevel - b.bevel) < 1e-4f &&
           std::fabs(a.fillet - b.fillet) < 1e-4f && std::fabs(a.inflate - b.inflate) < 1e-4f &&
           a.tess_mode == b.tess_mode && std::fabs(a.flatness - b.flatness) < 1e-6f &&
           std::fabs(a.scale - b.scale) < 1e-8f && a.per_glyph == b.per_glyph &&
           a.write_cache == b.write_cache;
}

void submit_mesh_rebuild() {
    text3d::AsyncMeshBuildRequest req = make_mesh_build_request();
    // 已提交过且内容相同：不要每帧抬 generation，否则会清掉刚完成的结果导致永远 apply 不上
    if (g_state.has_submitted_req && request_same_content(req, g_state.last_submitted_req)) {
        g_state.mesh_build_inflight = g_state.mesh_builder.is_busy();
        return;
    }
    g_state.last_submitted_req = req;
    g_state.has_submitted_req = true;
    g_state.mesh_builder.submit(std::move(req));
    g_state.mesh_build_inflight = true;
}

void apply_mesh_result(text3d::Renderer& renderer, text3d::AsyncMeshBuildResult& result) {
    g_state.last_build = result.build;

    if (!result.ok) {
        std::cerr << "[main] async build 失败: "
                  << text3d::build_result_format(g_state.last_build) << "\n";
        if (g_state.edit.font_index != g_state.applied_font_index &&
            g_state.applied_font_index >= 0) {
            g_state.edit.font_index = g_state.applied_font_index;
        }
        g_state.mesh_build_inflight = g_state.mesh_builder.is_busy();
        return;
    }

    const auto t_upload0 = std::chrono::steady_clock::now();
    g_state.perf.rebuild = result.timings;
    g_state.anim_player.stop_idle();

    if (result.text.empty()) {
        {
            text3d::ScopedTimer upload_timer(&g_state.perf.rebuild.stage_upload_ms);
            renderer.upload_mesh({});
        }
        g_state.glyphs.clear();
        g_state.use_per_glyph = false;
        g_state.perf.verts = 0;
        g_state.perf.tris = 0;
        g_state.last_build.set_ok(0, 0);
    } else if (result.use_per_glyph) {
        {
            text3d::ScopedTimer upload_timer(&g_state.perf.rebuild.stage_upload_ms);
            renderer.upload_glyphs(result.glyphs);
        }
        g_state.glyphs = std::move(result.glyphs);
        g_state.use_per_glyph = true;
        g_state.perf.verts = renderer.vertex_count();
        g_state.perf.tris = renderer.triangle_count();
        g_state.last_build.set_ok(g_state.perf.verts, g_state.perf.tris);
    } else {
        {
            text3d::ScopedTimer upload_timer(&g_state.perf.rebuild.stage_upload_ms);
            renderer.upload_mesh(result.merged_mesh);
        }
        g_state.glyphs.clear();
        g_state.use_per_glyph = false;
        g_state.perf.verts = renderer.vertex_count();
        g_state.perf.tris = renderer.triangle_count();
        g_state.last_build.set_ok(g_state.perf.verts, g_state.perf.tris);
    }

    const auto t_upload1 = std::chrono::steady_clock::now();
    const float upload_ms =
        std::chrono::duration<float, std::milli>(t_upload1 - t_upload0).count();
    g_state.perf.rebuild.stage_upload_ms = upload_ms;
    g_state.perf.rebuild.total_ms = result.timings.total_ms + upload_ms;
    g_state.bench_just_applied = true;
    g_state.bench_apply_upload_ms = upload_ms;

    g_state.perf.cache_outline = result.cache_outline;
    g_state.perf.cache_planar = result.cache_planar;
    g_state.perf.cache_mesh = result.cache_mesh;

    {
        const text3d::GpuUploadStats us = renderer.last_upload_stats();
        g_state.perf.upload_slots_skipped = us.slots_skipped;
        g_state.perf.upload_slots_uploaded = us.meshes_uploaded;
        g_state.perf.upload_gpu_unique_buffers = us.unique_meshes;
        g_state.perf.upload_bytes = us.bytes_uploaded;
    }

    g_state.applied_text = result.text;
    g_state.applied_depth = result.depth;
    g_state.applied_bevel = result.bevel;
    g_state.applied_fillet = result.fillet;
    g_state.applied_inflate = result.inflate;
    g_state.applied_font_index = g_state.edit.font_index;
    g_state.applied_anim_index = g_state.edit.anim_index;
    g_state.applied_tess_backend = result.tess_backend;
    g_state.edit.applied_r_min = result.applied_r_min;
    g_state.edit.applied_r_max = result.applied_r_max;
    g_state.edit.safe_r_min = result.safe_r_min;
    g_state.edit.applied_inflate_h = result.inflate_h_max;

    std::cout << "[main] async apply path=" << (g_state.use_per_glyph ? "per-glyph" : "merged")
              << " tess="
              << text3d::tess_mode_name(tess_mode_from_ui(result.tess_backend))
              << " applied_R=[" << result.applied_r_min << "," << result.applied_r_max << "]"
              << " safe_R_min=" << result.safe_r_min << " inflate_H=" << result.inflate_h_max
              << "\n"
              << "[perf] " << text3d::perf_stats_format_full_log(g_state.perf) << "\n"
              << "[build] " << text3d::build_result_format(g_state.last_build) << "\n";

    g_state.mesh_build_inflight = g_state.mesh_builder.is_busy();
}

void poll_async_mesh_result(text3d::Renderer& renderer) {
    text3d::AsyncMeshBuildResult result;
    if (!g_state.mesh_builder.poll_result(result)) {
        g_state.mesh_build_inflight = g_state.mesh_builder.is_busy();
        return;
    }
    apply_mesh_result(renderer, result);
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
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--bench") {
            g_state.bench_mode = true;
        } else if (a == "--out" && i + 1 < argc) {
            g_state.bench_opt.out_path = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            g_state.bench_opt.sample_frames = std::max(1, std::atoi(argv[++i]));
        } else if (a == "--warmup" && i + 1 < argc) {
            g_state.bench_opt.warmup_frames = std::max(0, std::atoi(argv[++i]));
        } else if (a == "--build-type" && i + 1 < argc) {
            g_state.bench_opt.build_type = argv[++i];
        } else if (a.rfind("-", 0) != 0) {
            cli_font = argv[i];
        } else {
            std::cerr << "[main] unknown arg: " << a << "\n";
        }
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
    g_state.edit.anim_index = g_state.bench_mode ? 0 : 1;
    g_state.anim_player.set_by_index(g_state.edit.anim_index);

    if (g_state.bench_mode) {
        g_state.edit.unlock_vsync = true;
        g_state.edit.use_offscreen_canvas = true;
        g_state.edit.canvas_size_index = 0;
        for (int i = 0; i < static_cast<int>(g_state.fonts.size()); ++i) {
            if (g_state.fonts[static_cast<size_t>(i)].id == "187086") {
                g_state.edit.font_index = i;
                break;
            }
        }
        if (g_state.bench_opt.out_path.empty()) {
            g_state.bench_opt.out_path =
                std::string("docs/perf/bench-") + g_state.bench_opt.build_type + ".md";
        }
        g_state.bench_runner.configure(g_state.bench_opt);
        std::cout << "[bench] mode on out=" << g_state.bench_opt.out_path
                  << " warmup=" << g_state.bench_opt.warmup_frames
                  << " frames=" << g_state.bench_opt.sample_frames << "\n";
    }

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
        glfwCreateWindow(960, 640,
                         g_state.bench_mode ? "3D Text — Bench" : "3D Text — FreeType outline extrude",
                         nullptr, nullptr);
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
    if (g_state.bench_mode) {
        g_state.edit.unlock_vsync = true;
        apply_vsync_if_needed(window);
    }

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

    text3d::Renderer renderer;
    std::string shader_dir = TEXT3D_SHADER_DIR;
    if (!renderer.init(shader_dir)) {
        shader_dir = "shaders";
        if (!renderer.init(shader_dir)) {
            return 1;
        }
    }

    text3d::OffscreenCanvas canvas;
    text3d::ScreenPass screen_pass;
    if (!screen_pass.init(shader_dir)) {
        std::cerr << "[main] ScreenPass init failed\n";
        return 1;
    }

    // 与当前 edit 对齐，避免首帧前 applied_*=-1 导致每帧反复 submit、结果永被丢弃
    g_state.applied_font_index = g_state.edit.font_index;
    g_state.applied_anim_index = g_state.edit.anim_index;
    g_state.applied_tess_backend = g_state.edit.tess_backend;

    g_state.mesh_builder.start();
    submit_mesh_rebuild();
    g_state.reload_mesh = false;

    if (g_state.bench_mode) {
        g_state.bench_runner.start();
    }

    g_state.last_frame_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - g_state.last_frame_time);
        g_state.last_frame_time = now;
        g_state.perf.ui_frame_ms = dt * 1000.f;
        if (dt > 0.f) {
            const float instant_fps = 1.f / dt;
            g_state.fps_smooth = (g_state.fps_smooth <= 0.f)
                                     ? instant_fps
                                     : (g_state.fps_smooth * 0.9f + instant_fps * 0.1f);
            g_state.perf.ui_fps = g_state.fps_smooth;
        }

        g_state.bench_just_applied = false;

        text3d::debug_ui_begin_frame();
        g_state.edit.text_edited = false;
        g_state.edit.font_changed = false;
        g_state.edit.anim_changed = false;
        g_state.edit.tess_backend_changed = false;
        g_state.edit.pbr_material_changed = false;
        g_state.edit.request_rescan_materials = false;
        g_state.edit.cache_write_geometry = true;
        if (!g_state.bench_mode) {
            g_state.edit.request_play = false;
            text3d::debug_ui_draw(g_state.perf, g_state.edit, g_state.anim_player.playing(),
                                  g_state.use_per_glyph, &g_state.last_build,
                                  g_state.mesh_build_inflight || g_state.mesh_builder.is_busy());
        }

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
            std::fabs(g_state.edit.fillet - g_state.applied_fillet) > 1e-4f ||
            std::fabs(g_state.edit.inflate - g_state.applied_inflate) > 1e-4f) {
            g_state.reload_mesh = true;
        }
        if (g_state.text_pending && (now - g_state.text_edit_time) >= kTextDebounceSec) {
            g_state.text_pending = false;
            if (std::string(g_state.edit.text) != g_state.applied_text) {
                g_state.reload_mesh = true;
            }
        }

        if (g_state.reload_font) {
            g_state.reload_mesh = true;
            g_state.reload_font = false;
        }

        // 先取结果并更新 applied_*，再决定是否 submit，避免「未 apply 又 submit」清掉完成槽
        poll_async_mesh_result(renderer);

        if (g_state.reload_mesh) {
            submit_mesh_rebuild();
            g_state.reload_mesh = false;
        }

        if (g_state.edit.request_play && g_state.use_per_glyph) {
            g_state.anim_player.play();
            std::cout << "[main] Anim Play\n";
            g_state.edit.request_play = false;
        }

        g_state.anim_player.update(dt);

        apply_vsync_if_needed(window);

        glfwGetFramebufferSize(window, &fbw, &fbh);

        const bool use_canvas = g_state.edit.use_offscreen_canvas;
        // 固定竖直分辨率（720/1080）；交互模式宽度跟窗口宽高比走，
        // 左右拉宽会多渲真实场景背景，投影 aspect 同步所以字不扁。
        // bench 仍用固定 16:9，保证报告可比。
        const int canvas_h = (g_state.edit.canvas_size_index == 1) ? 1080 : 720;
        int canvas_w = (g_state.edit.canvas_size_index == 1) ? 1920 : 1280;
        if (!g_state.bench_mode) {
            const float win_aspect =
                (fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh) : (16.f / 9.f);
            canvas_w = std::max(8, static_cast<int>(std::lround(static_cast<float>(canvas_h) *
                                                                win_aspect)));
            canvas_w = (canvas_w + 7) / 8 * 8;  // 对齐，减少拖拽时 FBO 重建抖动
            canvas_w = std::min(canvas_w, 4096);
        }

        g_state.perf.offscreen_pass_ms = 0.f;
        g_state.perf.offscreen_compose_ms = 0.f;
        g_state.perf.present_ms = 0.f;

        if (use_canvas) {
            text3d::ScopedTimer off_timer(&g_state.perf.offscreen_pass_ms);
            if (!canvas.ensure(canvas_w, canvas_h)) {
                std::cerr << "[main] canvas ensure failed, fallback direct draw\n";
                g_state.edit.use_offscreen_canvas = false;
            } else {
                canvas.begin();
                glClearColor(0.12f, 0.14f, 0.18f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }
        }

        if (!g_state.edit.use_offscreen_canvas) {
            glViewport(0, 0, fbw > 0 ? fbw : 1, fbh > 0 ? fbh : 1);
            glClearColor(0.12f, 0.14f, 0.18f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        const bool drawing_to_canvas = g_state.edit.use_offscreen_canvas && canvas.valid();
        const float aspect = drawing_to_canvas
                                 ? (static_cast<float>(canvas.width()) /
                                    static_cast<float>(canvas.height() > 0 ? canvas.height() : 1))
                                 : ((fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh)
                                              : 1.f);

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

        const auto render_submit_t0 = std::chrono::steady_clock::now();
        g_state.perf.text_draw_ms = 0.f;
        {
            text3d::ScopedTimer text_draw_timer(&g_state.perf.text_draw_ms);
            if (g_state.use_per_glyph) {
                // 动画只给标量；矩阵在此按相机方向组装，再实例化合批
                text3d::AnimSample sample;
                g_state.anim_player.sample(sample);

                glm::vec3 toward_cam = g_state.camera.eye() - g_state.camera.target;
                if (glm::dot(toward_cam, toward_cam) > 1e-8f) {
                    toward_cam = glm::normalize(toward_cam);
                } else {
                    toward_cam = glm::vec3(0.f, 0.f, 1.f);
                }
                const glm::vec3 near_offset =
                    toward_cam * (sample.near_distance * sample.approach);

                const glm::mat4 view_proj = proj * view;
                std::vector<glm::mat4> models(g_state.glyphs.size());
                for (int i = 0; i < static_cast<int>(g_state.glyphs.size()); ++i) {
                    const auto& g = g_state.glyphs[static_cast<size_t>(i)];
                    glm::mat4 model(1.f);
                    model = glm::translate(
                        model, glm::vec3(g.rest_x, g.rest_y, 0.f) + near_offset);
                    model = glm::rotate(model, sample.angle_y, glm::vec3(0.f, 1.f, 0.f));
                    models[static_cast<size_t>(i)] = model;
                }
                renderer.draw_glyphs_instanced(models.data(),
                                               static_cast<int>(models.size()),
                                               glm::value_ptr(view_proj), draw_params);
                g_state.perf.draw_batches = renderer.last_draw_batches();
                g_state.perf.gl_draw_calls = renderer.last_gl_draw_calls();
            } else {
                const glm::mat4 model(1.f);
                const glm::mat4 mvp = proj * view * model;
                renderer.draw(glm::value_ptr(mvp), glm::value_ptr(model), draw_params);
                g_state.perf.draw_batches = renderer.last_draw_batches();
                g_state.perf.gl_draw_calls = renderer.last_gl_draw_calls();
            }
        }

        if (drawing_to_canvas) {
            canvas.end();
            {
                text3d::ScopedTimer present_timer(&g_state.perf.present_ms);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                // 画布宽高比已对齐窗口，全屏贴不会拉扁；多出的是场景里渲出来的背景。
                glViewport(0, 0, fbw > 0 ? fbw : 1, fbh > 0 ? fbh : 1);
                glClearColor(0.12f, 0.14f, 0.18f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                screen_pass.draw(canvas.color_tex(), 1.f);
            }
        }

        text3d::debug_ui_end_frame();
        const auto render_submit_t1 = std::chrono::steady_clock::now();
        g_state.perf.render_submit_ms =
            std::chrono::duration<float, std::milli>(render_submit_t1 - render_submit_t0).count();

        if (g_state.bench_mode && !g_state.bench_runner.finished()) {
            text3d::BenchFrameContext bctx;
            bctx.edit = &g_state.edit;
            bctx.perf = &g_state.perf;
            bctx.now = now;
            bctx.mesh_busy = g_state.mesh_build_inflight || g_state.mesh_builder.is_busy();
            bctx.applied_text = g_state.applied_text;
            bctx.applied_depth = g_state.applied_depth;
            bctx.applied_inflate = g_state.applied_inflate;
            bctx.applied_anim_index = g_state.applied_anim_index;
            bctx.just_applied = g_state.bench_just_applied;
            bctx.apply_upload_ms = g_state.bench_apply_upload_ms;
            bctx.reload_mesh = &g_state.reload_mesh;
            bctx.request_orbit = &g_state.bench_request_orbit;
            const bool still = g_state.bench_runner.tick(bctx);
            if (g_state.bench_request_orbit) {
                g_state.camera.yaw_deg += 1.5f;
                g_state.bench_request_orbit = false;
            }
            if (!still || g_state.bench_runner.finished()) {
                const auto gates = g_state.bench_runner.evaluate_gates();
                if (!text3d::write_bench_report_markdown(g_state.bench_opt.out_path,
                                                         g_state.bench_opt,
                                                         g_state.bench_runner.results(), gates)) {
                    std::cerr << "[bench] write report failed: " << g_state.bench_opt.out_path
                              << "\n";
                } else {
                    std::cout << "[bench] wrote " << g_state.bench_opt.out_path << "\n";
                }
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
        pace_frame_if_vsync_on(now);
    }

    g_state.mesh_builder.stop();
    screen_pass.shutdown();
    canvas.destroy();
    text3d::debug_ui_shutdown();
    g_state.active_pbr_material.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
