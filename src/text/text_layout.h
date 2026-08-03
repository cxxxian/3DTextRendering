#pragma once
/*
 * 文本排版：HarfBuzz shape → FreeType outline → 挤出；提供整句与逐字两种出口。
 */

#include "mesh/build_result.h"
#include "mesh/mesh_extrude.h"
#include "text/ft_outline.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace text3d {

struct RebuildTimings;
class GlyphGeometryCache;
class GlyphMeshPool;

struct LayoutOptions {
    ExtrudeOptions extrude;
    float flatness = 0.5f;
    float scale = 1.f;
    float* out_applied_radius_min = nullptr;
    float* out_applied_radius_max = nullptr;
    float* out_safe_radius_min = nullptr;
    float* out_applied_inflate_h_max = nullptr;
    RebuildTimings* timings = nullptr;
    GlyphGeometryCache* cache = nullptr;  // #8 两级缓存
    GlyphMeshPool* mesh_pool = nullptr;   // #9 CPU Mesh 池
    bool write_planar_cache = true;       // 几何滑条拖动/输入中可关
    bool write_mesh_pool = true;
};

/* 单个可见字：几何在本地（原点=字心）；rest 为静止时的世界落点 */
struct GlyphInstance {
    std::shared_ptr<const Mesh> mesh;
    float rest_x = 0.f;
    float rest_y = 0.f;
    std::uint32_t glyph_index = 0;
};

bool layout_text_glyphs(const FontFace& font, const std::string& text,
                        const LayoutOptions& opt, std::vector<GlyphInstance>& out_glyphs,
                        BuildResult* result = nullptr);

bool layout_text(const FontFace& font, const std::string& text, const LayoutOptions& opt,
                 Mesh& out_mesh, BuildResult* result = nullptr);

}  // namespace text3d
