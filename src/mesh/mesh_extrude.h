#pragma once
/*
 * 挤出编排入口（对外唯一 API）：GlyphOutline → Mesh。
 * 直边走 tess+直墙；fillet/bevel>0 走内缩 + IEdgeProfile + 外墙（fillet 优先）。
 */

#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <vector>

namespace text3d {

struct ExtrudeOptions {
    float depth = 20.f;      // 总厚度（与轮廓同单位）
    float z_center = 0.f;    // 厚度中心
    float bevel = 0.f;       // 平倒角半径；须 2*R < depth
    float fillet = 0.f;      // 真圆角半径；>0 时忽略 bevel
};

/* 单字折线 → 立体 mesh */
bool build_extruded_mesh(const GlyphOutline& outline, const ExtrudeOptions& opt, Mesh& out);

/* 排版：平移 / 缩放 / 合并为一次 draw */
void translate_mesh(Mesh& mesh, float dx, float dy, float dz);
void scale_mesh(Mesh& mesh, float s);
Mesh merge_meshes(const std::vector<Mesh>& parts);

}  // namespace text3d
