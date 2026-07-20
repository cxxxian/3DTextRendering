/*
 * Renderer 实现：着色器加载、整句/逐字上传、按 ShadingModel 绘制。
 */

#include "render/renderer.h"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>

#include <glad/gl.h>

namespace text3d {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

unsigned int compile_shader(unsigned int type, const std::string& src) {
    const unsigned int sh = glCreateShader(type);
    const char* csrc = src.c_str();
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::cerr << "[renderer] shader compile error:\n" << log << "\n";
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

unsigned int link_program(unsigned int vs, unsigned int fs) {
    const unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[renderer] program link error:\n" << log << "\n";
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void setup_vertex_attribs() {
    const int stride = static_cast<int>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, px)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
}

void set_uniform3(int loc, const glm::vec3& v) {
    if (loc >= 0) {
        glUniform3f(loc, v.x, v.y, v.z);
    }
}

void set_uniform1f(int loc, float v) {
    if (loc >= 0) {
        glUniform1f(loc, v);
    }
}

}  // namespace

Renderer::~Renderer() {
    clear_glyphs_();
    clear_merged_();
    if (lambert_.program) {
        glDeleteProgram(lambert_.program);
        lambert_.program = 0;
    }
    if (phong_.program) {
        glDeleteProgram(phong_.program);
        phong_.program = 0;
    }
    if (pbr_.program) {
        glDeleteProgram(pbr_.program);
        pbr_.program = 0;
    }
    if (glass_.program) {
        glDeleteProgram(glass_.program);
        glass_.program = 0;
    }
}

void Renderer::clear_merged_() {
    if (ebo_) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    index_count_ = 0;
}

void Renderer::clear_glyphs_() {
    for (auto& g : glyphs_) {
        if (g.ebo) glDeleteBuffers(1, &g.ebo);
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
        if (g.vao) glDeleteVertexArrays(1, &g.vao);
    }
    glyphs_.clear();
}

void Renderer::upload_into_(unsigned int vao, unsigned int vbo, unsigned int ebo,
                            const Mesh& mesh, int& out_index_count) {
    out_index_count = static_cast<int>(mesh.indices.size());

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                 mesh.vertices.empty() ? nullptr : mesh.vertices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
                 mesh.indices.empty() ? nullptr : mesh.indices.data(),
                 GL_STATIC_DRAW);
    setup_vertex_attribs();
    glBindVertexArray(0);
}

void Renderer::cache_locations_(ProgramLocs& locs) {
    locs.mvp = glGetUniformLocation(locs.program, "uMVP");
    locs.model = glGetUniformLocation(locs.program, "uModel");
    locs.light_dir = glGetUniformLocation(locs.program, "uLightDir");
    locs.light_color = glGetUniformLocation(locs.program, "uLightColor");
    locs.camera_pos = glGetUniformLocation(locs.program, "uCameraPos");
    locs.albedo = glGetUniformLocation(locs.program, "uAlbedo");
    locs.ambient = glGetUniformLocation(locs.program, "uAmbient");
    locs.metallic = glGetUniformLocation(locs.program, "uMetallic");
    locs.roughness = glGetUniformLocation(locs.program, "uRoughness");
    locs.shininess = glGetUniformLocation(locs.program, "uShininess");
    locs.opacity = glGetUniformLocation(locs.program, "uOpacity");
    locs.env_strength = glGetUniformLocation(locs.program, "uEnvStrength");
    locs.use_albedo_map = glGetUniformLocation(locs.program, "uUseAlbedoMap");
    locs.use_orm_map = glGetUniformLocation(locs.program, "uUseOrmMap");
    locs.use_normal_map = glGetUniformLocation(locs.program, "uUseNormalMap");
    locs.orm_layout = glGetUniformLocation(locs.program, "uOrmLayout");
    locs.albedo_map = glGetUniformLocation(locs.program, "uAlbedoMap");
    locs.orm_map = glGetUniformLocation(locs.program, "uOrmMap");
    locs.normal_map = glGetUniformLocation(locs.program, "uNormalMap");
}

bool Renderer::load_program_(const std::string& shader_dir, const char* frag_name,
                             ProgramLocs& out) {
    const std::string vs_src = read_file(shader_dir + "/pbr.vert");
    const std::string fs_src = read_file(shader_dir + "/" + frag_name);
    if (vs_src.empty() || fs_src.empty()) {
        std::cerr << "[renderer] 读不到着色器: " << shader_dir << "/pbr.vert + "
                  << frag_name << "\n";
        return false;
    }

    const unsigned int vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    const unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    out.program = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!out.program) {
        return false;
    }
    cache_locations_(out);
    return true;
}

bool Renderer::init(const std::string& shader_dir) {
    if (!load_program_(shader_dir, "lambert.frag", lambert_) ||
        !load_program_(shader_dir, "phong.frag", phong_) ||
        !load_program_(shader_dir, "pbr.frag", pbr_) ||
        !load_program_(shader_dir, "glass.frag", glass_)) {
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    setup_vertex_attribs();
    glBindVertexArray(0);

    std::cout << "[renderer] 着色器就绪 (Lambert / Phong / PBR / Glass)\n";
    return true;
}

void Renderer::upload_mesh(const Mesh& mesh) {
    clear_glyphs_();

    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
    }

    upload_into_(vao_, vbo_, ebo_, mesh, index_count_);
    vertex_count_ = static_cast<int>(mesh.vertices.size());

    std::cout << "[renderer] 上传整句 mesh: verts=" << mesh.vertices.size()
              << " indices=" << mesh.indices.size() << "\n";
}

void Renderer::upload_glyphs(const std::vector<GlyphInstance>& glyphs) {
    clear_glyphs_();
    index_count_ = 0;
    vertex_count_ = 0;

    glyphs_.resize(glyphs.size());
    for (size_t i = 0; i < glyphs.size(); ++i) {
        auto& gpu = glyphs_[i];
        glGenVertexArrays(1, &gpu.vao);
        glGenBuffers(1, &gpu.vbo);
        glGenBuffers(1, &gpu.ebo);
        upload_into_(gpu.vao, gpu.vbo, gpu.ebo, glyphs[i].mesh, gpu.index_count);
        vertex_count_ += static_cast<int>(glyphs[i].mesh.vertices.size());
        index_count_ += gpu.index_count;
    }

    std::cout << "[renderer] 上传逐字 glyphs=" << glyphs.size()
              << " total_verts=" << vertex_count_
              << " total_indices=" << index_count_ << "\n";
}

const Renderer::ProgramLocs& Renderer::program_for_(ShadingModel shading) const {
    switch (shading) {
        case ShadingModel::Lambert:
            return lambert_;
        case ShadingModel::Phong:
            return phong_;
        case ShadingModel::Glass:
            return glass_;
        case ShadingModel::Pbr:
        default:
            return pbr_;
    }
}

void Renderer::bind_draw_params_(const ProgramLocs& locs, const DrawParams& params) const {
    set_uniform3(locs.light_dir, params.light.direction);
    set_uniform3(locs.light_color, params.light.color);
    set_uniform3(locs.camera_pos, params.camera_pos);
    set_uniform3(locs.albedo, params.albedo);
    set_uniform1f(locs.ambient, params.light.ambient);
    set_uniform1f(locs.metallic, params.metallic);
    // roughness 过低时 GGX 数值不稳
    const float roughness = params.roughness < 0.04f ? 0.04f : params.roughness;
    set_uniform1f(locs.roughness, roughness);
    set_uniform1f(locs.shininess, params.shininess);
    set_uniform1f(locs.opacity, params.opacity);
    set_uniform1f(locs.env_strength, params.env_strength);

    const int use_albedo = params.albedo_map != 0 ? 1 : 0;
    const int use_orm = params.orm_map != 0 ? 1 : 0;
    const int use_normal = params.normal_map != 0 ? 1 : 0;
    if (locs.use_albedo_map >= 0) {
        glUniform1i(locs.use_albedo_map, use_albedo);
    }
    if (locs.use_orm_map >= 0) {
        glUniform1i(locs.use_orm_map, use_orm);
    }
    if (locs.use_normal_map >= 0) {
        glUniform1i(locs.use_normal_map, use_normal);
    }
    if (locs.orm_layout >= 0) {
        glUniform1i(locs.orm_layout, params.orm_layout);
    }
    if (locs.albedo_map >= 0) {
        glUniform1i(locs.albedo_map, 0);
    }
    if (locs.orm_map >= 0) {
        glUniform1i(locs.orm_map, 1);
    }
    if (locs.normal_map >= 0) {
        glUniform1i(locs.normal_map, 2);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, use_albedo ? params.albedo_map : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, use_orm ? params.orm_map : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, use_normal ? params.normal_map : 0);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::draw_with_(const ProgramLocs& locs, unsigned int vao, int index_count,
                          const float* mvp16, const float* model16,
                          const DrawParams& params) const {
    if (!locs.program || index_count <= 0 || !vao) {
        return;
    }

    glUseProgram(locs.program);
    glUniformMatrix4fv(locs.mvp, 1, GL_FALSE, mvp16);
    glUniformMatrix4fv(locs.model, 1, GL_FALSE, model16);
    bind_draw_params_(locs, params);

    glBindVertexArray(vao);

    if (params.shading == ShadingModel::Glass) {
        // 透明：先背面再正面；写深度避免后续误盖过
        GLboolean depth_mask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        GLboolean cull_was = glIsEnabled(GL_CULL_FACE);
        GLint cull_face = GL_BACK;
        glGetIntegerv(GL_CULL_FACE_MODE, &cull_face);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);

        glCullFace(GL_FRONT);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);

        glCullFace(GL_BACK);
        glDepthMask(GL_TRUE);
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);

        glDisable(GL_BLEND);
        glDepthMask(depth_mask);
        glCullFace(cull_face);
        if (!cull_was) {
            glDisable(GL_CULL_FACE);
        }
    } else {
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
}

void Renderer::draw(const float* mvp16, const float* model16, const DrawParams& params) const {
    if (index_count_ <= 0 || !vao_ || !glyphs_.empty()) {
        return;
    }
    draw_with_(program_for_(params.shading), vao_, index_count_, mvp16, model16, params);
}

void Renderer::draw_glyph(int index, const float* mvp16, const float* model16,
                          const DrawParams& params) const {
    if (index < 0 || index >= static_cast<int>(glyphs_.size())) {
        return;
    }
    const GpuGlyph& g = glyphs_[static_cast<size_t>(index)];
    if (g.index_count <= 0) {
        return;
    }
    draw_with_(program_for_(params.shading), g.vao, g.index_count, mvp16, model16, params);
}

}  // namespace text3d
