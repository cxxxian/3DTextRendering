#pragma once
/*
 * 挤出编排入口（对外唯一完整 API）：GlyphOutline → Mesh。
 * 内部 = clean → planar → 3D；缓存路径见 glyph_planar / glyph_geometry_cache。
 * 直边走 tess+直墙；fillet/bevel>0 走内缩 + IEdgeProfile + 外墙（fillet 优先）。
 */

#include "mesh/build_result.h"
#include "mesh/mesh_offset_tess.h"
#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <vector>

namespace text3d {

struct RebuildTimings;

struct ExtrudeOptions {
    float depth = 20.f;      // 总厚度（与轮廓同单位）
    float z_center = 0.f;    // 厚度中心
    float bevel = 0.f;       // 平倒角强度 0~1；实际 R = 强度 × 该字安全半径上限
    float fillet = 0.f;      // 真圆角强度 0~1；>0 时忽略 bevel
    float inflate = 0.f;     // Cap Inflate 强度 0~1（字面鼓包）
    TessMode tess_mode = TessMode::Auto;
    float* out_applied_radius = nullptr;     // 可选：本字实际棱半径
    float* out_applied_inflate_h = nullptr;  // 可选：本字实际拱高 H
    RebuildTimings* timings = nullptr;
};

/* 单字折线 → 立体 mesh；result 可选，失败时写入阶段 */
bool build_extruded_mesh(const GlyphOutline& outline, const ExtrudeOptions& opt, Mesh& out,
                         BuildResult* result = nullptr);

/* 排版：平移 / 缩放 / 合并为一次 draw */
void translate_mesh(Mesh& mesh, float dx, float dy, float dz);
void scale_mesh(Mesh& mesh, float s);
Mesh merge_meshes(const std::vector<Mesh>& parts);

}  // namespace text3d
