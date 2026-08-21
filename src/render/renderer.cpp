/*
 * Renderer 实现：着色器加载、整句/逐字上传（#12 池共享）、实例化绘制。
 */

#include "render/renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, tx)));
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

void set_uniform1i(int loc, int v) {
    if (loc >= 0) {
        glUniform1i(loc, v);
    }
}

void fnv1a_mix_bytes(std::uint64_t& h, const void* data, std::size_t nbytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < nbytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
}

std::uint64_t mesh_fingerprint(const Mesh& mesh) {
    if (mesh.vertices.empty() && mesh.indices.empty()) {
        return 0;
    }
    std::uint64_t h = 14695981039346656037ull;
    const std::uint64_t nv = static_cast<std::uint64_t>(mesh.vertices.size());
    const std::uint64_t ni = static_cast<std::uint64_t>(mesh.indices.size());
    fnv1a_mix_bytes(h, &nv, sizeof(nv));
    fnv1a_mix_bytes(h, &ni, sizeof(ni));
    if (!mesh.vertices.empty()) {
        fnv1a_mix_bytes(h, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
    }
    if (!mesh.indices.empty()) {
        fnv1a_mix_bytes(h, mesh.indices.data(), mesh.indices.size() * sizeof(unsigned int));
    }
    return h;
}

std::size_t mesh_upload_bytes(const Mesh& mesh) {
    return mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(unsigned int);
}

}  // namespace

Renderer::~Renderer() {
    clear_glyphs_();
    clear_merged_();
    if (instance_vbo_) {
        glDeleteBuffers(1, &instance_vbo_);
        instance_vbo_ = 0;
    }
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
    vertex_count_ = 0;
    merged_fingerprint_ = 0;
    merged_uploaded_once_ = false;
}

void Renderer::clear_glyphs_() {
    glyphs_.clear();
    pool_.clear();
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

void Renderer::bind_instance_attribs_() const {
    const int stride = static_cast<int>(sizeof(glm::mat4));
    for (int i = 0; i < 4; ++i) {
        // location 0..3 = mesh；4..7 = instance mat4
        const unsigned int loc = static_cast<unsigned int>(4 + i);
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(sizeof(float) * 4 * i));
        glVertexAttribDivisor(loc, 1);
    }
}

void Renderer::cache_locations_(ProgramLocs& locs) {
    locs.mvp = glGetUniformLocation(locs.program, "uMVP");
    locs.model = glGetUniformLocation(locs.program, "uModel");
    locs.vp = glGetUniformLocation(locs.program, "uVP");
    locs.use_instance = glGetUniformLocation(locs.program, "uUseInstance");
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
    glGenBuffers(1, &instance_vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    setup_vertex_attribs();
    glBindVertexArray(0);

    std::cout << "[renderer] 着色器就绪 (Lambert / Phong / PBR / Glass, #12 instancing)\n";
    return true;
}

void Renderer::upload_mesh(const Mesh& mesh) {
    clear_glyphs_();

    const std::uint64_t fp = mesh_fingerprint(mesh);
    const int vert_n = static_cast<int>(mesh.vertices.size());
    const int index_n = static_cast<int>(mesh.indices.size());

    if (merged_uploaded_once_ && vao_ && fp == merged_fingerprint_ &&
        vert_n == vertex_count_ && index_n == index_count_) {
        last_upload_stats_ = GpuUploadStats{GpuUploadKind::Skip, 0, 1, 0, 1, 0};
        std::cout << "[renderer] 整句 mesh Skip (fingerprint hit)\n";
        return;
    }

    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
    }

    upload_into_(vao_, vbo_, ebo_, mesh, index_count_);
    vertex_count_ = vert_n;
    merged_fingerprint_ = fp;
    merged_uploaded_once_ = true;
    last_upload_stats_ =
        GpuUploadStats{GpuUploadKind::Upload, mesh_upload_bytes(mesh), 0, 1, 1, 1};

    std::cout << "[renderer] 上传整句 mesh: verts=" << mesh.vertices.size()
              << " indices=" << mesh.indices.size()
              << " bytes=" << last_upload_stats_.bytes_uploaded << "\n";
}

void Renderer::upload_glyphs(const std::vector<GlyphInstance>& glyphs) {
    index_count_ = 0;
    merged_fingerprint_ = 0;
    merged_uploaded_once_ = false;

    const std::size_t new_n = glyphs.size();
    const std::size_t old_n = glyphs_.size();

    auto slot_unchanged = [](const GlyphInstance& src, const GpuGlyphSlot& gpu) -> bool {
        if (!src.mesh) {
            return gpu.mesh_ptr == nullptr && gpu.fingerprint == 0;
        }
        if (gpu.fingerprint == 0) {
            return false;
        }
        if (src.mesh.get() == gpu.mesh_ptr) {
            return true;
        }
        return mesh_fingerprint(*src.mesh) == gpu.fingerprint;
    };

    if (new_n == old_n && new_n > 0) {
        bool all_same = true;
        for (std::size_t i = 0; i < new_n; ++i) {
            if (!slot_unchanged(glyphs[i], glyphs_[i])) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            int verts = 0;
            int sum_idx = 0;
            for (std::size_t i = 0; i < new_n; ++i) {
                if (glyphs[i].mesh) {
                    verts += static_cast<int>(glyphs[i].mesh->vertices.size());
                }
                sum_idx += glyphs_[i].index_count;
            }
            vertex_count_ = verts;
            index_count_ = sum_idx;
            last_upload_stats_ = GpuUploadStats{
                GpuUploadKind::Skip,
                0,
                static_cast<int>(new_n),
                0,
                pool_.size(),
                0,
            };
            std::cout << "[renderer] 逐字 glyphs Skip all slots=" << new_n
                      << " unique=" << pool_.size()
                      << " total_verts=" << vertex_count_
                      << " total_indices=" << sum_idx << "\n";
            return;
        }
    }

    if (new_n > old_n) {
        glyphs_.resize(new_n);
    }

    int skipped = 0;
    int meshes_uploaded = 0;
    std::size_t bytes = 0;
    int verts = 0;
    int inds = 0;
    std::unordered_set<std::uint64_t> used_fps;

    for (std::size_t i = 0; i < new_n; ++i) {
        GpuGlyphSlot& gpu = glyphs_[i];
        const GlyphInstance& src = glyphs[i];

        if (!src.mesh) {
            gpu = GpuGlyphSlot{};
            continue;
        }

        if (slot_unchanged(src, gpu)) {
            ++skipped;
            verts += static_cast<int>(src.mesh->vertices.size());
            inds += gpu.index_count;
            gpu.mesh_ptr = src.mesh.get();
            used_fps.insert(gpu.fingerprint);
            continue;
        }

        const std::uint64_t fp = mesh_fingerprint(*src.mesh);
        bool uploaded = false;
        GpuMeshEntry* entry = pool_.acquire(fp, *src.mesh, &uploaded);
        if (uploaded) {
            ++meshes_uploaded;
            bytes += mesh_upload_bytes(*src.mesh);
        }
        gpu.fingerprint = fp;
        gpu.mesh_ptr = src.mesh.get();
        gpu.index_count = entry ? entry->index_count : 0;
        used_fps.insert(fp);
        verts += static_cast<int>(src.mesh->vertices.size());
        inds += gpu.index_count;
    }

    glyphs_.resize(new_n);
    pool_.retain_only(used_fps);

    vertex_count_ = verts;
    index_count_ = inds;
    last_upload_stats_ = GpuUploadStats{
        meshes_uploaded == 0 ? GpuUploadKind::Skip : GpuUploadKind::Upload,
        bytes,
        skipped,
        meshes_uploaded,
        pool_.size(),
        meshes_uploaded,
    };

    std::cout << "[renderer] 上传逐字 glyphs=" << new_n
              << " skip=" << skipped << " up=" << meshes_uploaded
              << " unique=" << pool_.size()
              << " bytes=" << bytes
              << " total_verts=" << vertex_count_
              << " total_indices=" << inds << "\n";
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
    const float roughness = params.roughness < 0.04f ? 0.04f : params.roughness;
    set_uniform1f(locs.roughness, roughness);
    set_uniform1f(locs.shininess, params.shininess);
    set_uniform1f(locs.opacity, params.opacity);
    set_uniform1f(locs.env_strength, params.env_strength);

    const int use_albedo = params.albedo_map != 0 ? 1 : 0;
    const int use_orm = params.orm_map != 0 ? 1 : 0;
    const int use_normal = params.normal_map != 0 ? 1 : 0;
    set_uniform1i(locs.use_albedo_map, use_albedo);
    set_uniform1i(locs.use_orm_map, use_orm);
    set_uniform1i(locs.use_normal_map, use_normal);
    set_uniform1i(locs.orm_layout, params.orm_layout);
    set_uniform1i(locs.albedo_map, 0);
    set_uniform1i(locs.orm_map, 1);
    set_uniform1i(locs.normal_map, 2);

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
    set_uniform1i(locs.use_instance, 0);
    glUniformMatrix4fv(locs.mvp, 1, GL_FALSE, mvp16);
    glUniformMatrix4fv(locs.model, 1, GL_FALSE, model16);
    bind_draw_params_(locs, params);

    glBindVertexArray(vao);

    if (params.shading == ShadingModel::Glass) {
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

void Renderer::draw_instanced_with_(const ProgramLocs& locs, unsigned int vao, int index_count,
                                   int instance_count, const float* vp16,
                                   const DrawParams& params) const {
    if (!locs.program || index_count <= 0 || !vao || instance_count <= 0) {
        return;
    }

    glUseProgram(locs.program);
    set_uniform1i(locs.use_instance, 1);
    glUniformMatrix4fv(locs.vp, 1, GL_FALSE, vp16);
    bind_draw_params_(locs, params);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    bind_instance_attribs_();

    if (params.shading == ShadingModel::Glass) {
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
        glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr,
                                instance_count);

        glCullFace(GL_BACK);
        glDepthMask(GL_TRUE);
        glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr,
                                instance_count);

        glDisable(GL_BLEND);
        glDepthMask(depth_mask);
        glCullFace(cull_face);
        if (!cull_was) {
            glDisable(GL_CULL_FACE);
        }
    } else {
        glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr,
                                instance_count);
    }

    glBindVertexArray(0);
}

void Renderer::draw(const float* mvp16, const float* model16, const DrawParams& params) const {
    if (index_count_ <= 0 || !vao_ || !glyphs_.empty()) {
        last_draw_batches_ = 0;
        last_gl_draw_calls_ = 0;
        return;
    }
    last_draw_batches_ = 1;
    last_gl_draw_calls_ = (params.shading == ShadingModel::Glass) ? 2 : 1;
    draw_with_(program_for_(params.shading), vao_, index_count_, mvp16, model16, params);
}

void Renderer::draw_glyphs_instanced(const glm::mat4* models, int count, const float* view_proj16,
                                     const DrawParams& params) {
    last_draw_batches_ = 0;
    last_gl_draw_calls_ = 0;
    if (!models || count <= 0 || glyphs_.empty()) {
        return;
    }
    const int n = std::min(count, static_cast<int>(glyphs_.size()));
    if (!instance_vbo_) {
        glGenBuffers(1, &instance_vbo_);
    }

    // 按 fingerprint 首次出现顺序合批
    std::vector<std::uint64_t> order;
    std::unordered_map<std::uint64_t, std::vector<int>> groups;
    order.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& slot = glyphs_[static_cast<std::size_t>(i)];
        if (slot.fingerprint == 0 || slot.index_count <= 0) {
            continue;
        }
        auto& idxs = groups[slot.fingerprint];
        if (idxs.empty()) {
            order.push_back(slot.fingerprint);
        }
        idxs.push_back(i);
    }

    const ProgramLocs& locs = program_for_(params.shading);
    const int gl_per_batch = (params.shading == ShadingModel::Glass) ? 2 : 1;
    std::vector<glm::mat4> batch_mats;
    batch_mats.reserve(static_cast<std::size_t>(n));

    for (std::uint64_t fp : order) {
        const GpuMeshEntry* entry = pool_.find(fp);
        if (!entry || !entry->vao || entry->index_count <= 0) {
            continue;
        }
        const auto& idxs = groups[fp];
        batch_mats.clear();
        for (int i : idxs) {
            batch_mats.push_back(models[i]);
        }

        glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(batch_mats.size() * sizeof(glm::mat4)),
                     batch_mats.data(), GL_DYNAMIC_DRAW);

        draw_instanced_with_(locs, entry->vao, entry->index_count,
                             static_cast<int>(batch_mats.size()), view_proj16, params);
        ++last_draw_batches_;
        last_gl_draw_calls_ += gl_per_batch;
    }
}

void Renderer::draw_glyph(int index, const float* mvp16, const float* model16,
                          const DrawParams& params) const {
    if (index < 0 || index >= static_cast<int>(glyphs_.size())) {
        return;
    }
    const GpuGlyphSlot& g = glyphs_[static_cast<size_t>(index)];
    if (g.index_count <= 0 || g.fingerprint == 0) {
        return;
    }
    const GpuMeshEntry* entry = pool_.find(g.fingerprint);
    if (!entry || !entry->vao) {
        return;
    }
    draw_with_(program_for_(params.shading), entry->vao, entry->index_count, mvp16, model16,
               params);
}

}  // namespace text3d
