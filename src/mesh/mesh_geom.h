#pragma once
/*
 * 挤出共用几何小工具：push / 法线 / UV / 条带 / cap / 墙。
 * 轮廓内缩、三角化、圆角/斜面剖面见 offset_tess、edge_*。
 */

#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <vector>

namespace text3d {

constexpr float kMeshEps = 1e-6f;

void push_vertex(Mesh& m, float x, float y, float z, float nx, float ny, float nz, float u,
                 float v);

/* 边方向 × +Z；对本仓库 FreeType 折线指向实体内侧，朝外光照时侧面需取反 */
void side_normal(float x0, float y0, float x1, float y1, float& nx, float& ny);

void outline_bounds(const GlyphOutline& outline, float& minx, float& miny, float& maxx,
                    float& maxy);

/* 字 bbox → [0,1]² UV */
void planar_uv(float x, float y, float minx, float miny, float sx, float sy, float& u, float& v);

float contour_perimeter(const Contour& c);

/* 侧面一条带：四顶点两三角，共用同一法线 */
void append_band_quad(Mesh& out, float ax, float ay, float az, float bx, float by, float bz,
                      float cx, float cy, float cz, float dx, float dy, float dz, float u0,
                      float u1, float v0, float v1, float nx, float ny, float nz);

void face_normal_from_quad(float ax, float ay, float az, float bx, float by, float bz, float dx,
                           float dy, float dz, float& nx, float& ny, float& nz);

/* 平面顶/底（+Z / -Z；底面绕序反且 V 翻转） */
void append_caps(Mesh& out, const std::vector<float>& xy, const std::vector<unsigned int>& tris,
                 float z_top, float z_bot, float minx, float miny, float sx, float sy);

/* bevel=fillet=0：沿原轮廓竖起直墙 */
void append_straight_sides(Mesh& out, const GlyphOutline& outline, float z_top, float z_bot);

/* Bevel/Fillet 共用：外轮廓中段直立墙（棱带由 edge_* 负责） */
void append_outer_walls(Mesh& out, const GlyphOutline& outer, float z_wall_top, float z_wall_bot);

}  // namespace text3d
