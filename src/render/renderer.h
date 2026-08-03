#pragma once
/*
 * OpenGL 绘制：整句 mesh / 逐字 glyph（#12 池共享 + 实例化），Lambert·Phong·PBR·Glass。
 * #11：槽未变 Skip；#12：同 fingerprint 共用 GPU Mesh，按几何合批 Instanced。
 */

#include "mesh/mesh_extrude.h"
#include "render/gpu_mesh_pool.h"
#include "render/light.h"
#include "text/text_layout.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace text3d {

enum class ShadingModel {
    Lambert = 0,
    Phong = 1,
    Pbr = 2,
    Glass = 3,
};

enum class GpuUploadKind {
    Skip = 0,
    Upload = 1,
};

struct GpuUploadStats {
    GpuUploadKind kind = GpuUploadKind::Upload;
    std::size_t bytes_uploaded = 0;
    int slots_skipped = 0;
    int slots_uploaded = 0;      // 与 meshes_uploaded 同值（兼容旧 HUD 字段名）
    int unique_meshes = 0;       // 当前活跃唯一 GPU Mesh
    int meshes_uploaded = 0;     // 本次新写入池的唯一几何数
};

struct DrawParams {
    ShadingModel shading = ShadingModel::Pbr;
    DirectionalLight light;
    glm::vec3 camera_pos{0.f, 0.f, 3.5f};
    glm::vec3 albedo{0.92f, 0.88f, 0.78f};
    float metallic = 0.f;
    float roughness = 0.45f;
    float shininess = 32.f;
    float opacity = 0.28f;
    float env_strength = 1.6f;

    unsigned int albedo_map = 0;
    unsigned int orm_map = 0;
    unsigned int normal_map = 0;
    int orm_layout = 0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(const std::string& shader_dir);

    void upload_mesh(const Mesh& mesh);
    void draw(const float* mvp16, const float* model16, const DrawParams& params) const;

    void upload_glyphs(const std::vector<GlyphInstance>& glyphs);
    /* 逐字实例化：models 与槽等长；view_proj = Projection * View */
    void draw_glyphs_instanced(const glm::mat4* models, int count, const float* view_proj16,
                               const DrawParams& params);
    void draw_glyph(int index, const float* mvp16, const float* model16,
                    const DrawParams& params) const;
    int glyph_count() const { return static_cast<int>(glyphs_.size()); }

    bool has_mesh() const { return index_count_ > 0 && glyphs_.empty(); }
    bool has_glyphs() const { return !glyphs_.empty(); }

    int vertex_count() const { return vertex_count_; }
    int triangle_count() const { return index_count_ / 3; }
    GpuUploadStats last_upload_stats() const { return last_upload_stats_; }
    int last_draw_batches() const { return last_draw_batches_; }
    int last_gl_draw_calls() const { return last_gl_draw_calls_; }

private:
    struct GpuGlyphSlot {
        std::uint64_t fingerprint = 0;
        const Mesh* mesh_ptr = nullptr;
        int index_count = 0;
    };

    struct ProgramLocs {
        unsigned int program = 0;
        int mvp = -1;
        int model = -1;
        int vp = -1;
        int use_instance = -1;
        int light_dir = -1;
        int light_color = -1;
        int camera_pos = -1;
        int albedo = -1;
        int ambient = -1;
        int metallic = -1;
        int roughness = -1;
        int shininess = -1;
        int opacity = -1;
        int env_strength = -1;
        int use_albedo_map = -1;
        int use_orm_map = -1;
        int use_normal_map = -1;
        int orm_layout = -1;
        int albedo_map = -1;
        int orm_map = -1;
        int normal_map = -1;
    };

    void clear_merged_();
    void clear_glyphs_();
    void upload_into_(unsigned int vao, unsigned int vbo, unsigned int ebo,
                      const Mesh& mesh, int& out_index_count);
    void bind_instance_attribs_() const;
    bool load_program_(const std::string& shader_dir, const char* frag_name,
                       ProgramLocs& out);
    void cache_locations_(ProgramLocs& locs);
    const ProgramLocs& program_for_(ShadingModel shading) const;
    void bind_draw_params_(const ProgramLocs& locs, const DrawParams& params) const;
    void draw_with_(const ProgramLocs& locs, unsigned int vao, int index_count,
                    const float* mvp16, const float* model16,
                    const DrawParams& params) const;
    void draw_instanced_with_(const ProgramLocs& locs, unsigned int vao, int index_count,
                              int instance_count, const float* vp16,
                              const DrawParams& params) const;

    ProgramLocs lambert_{};
    ProgramLocs phong_{};
    ProgramLocs pbr_{};
    ProgramLocs glass_{};

    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    unsigned int instance_vbo_ = 0;
    int index_count_ = 0;
    int vertex_count_ = 0;
    std::uint64_t merged_fingerprint_ = 0;
    bool merged_uploaded_once_ = false;
    GpuUploadStats last_upload_stats_{};
    mutable int last_draw_batches_ = 0;   // #13：逻辑 DC（Glass 仍计 1）
    mutable int last_gl_draw_calls_ = 0;  // #13：真实 glDraw* 次数（Glass ×2）

    std::vector<GpuGlyphSlot> glyphs_;
    GpuMeshPool pool_;
};

}  // namespace text3d
