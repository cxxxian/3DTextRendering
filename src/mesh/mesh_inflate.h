#pragma once
/*
 * PS 风格 Cap Inflate：帽面按到轮廓边界距离鼓包（边上天 h=0，中心 h=H）。
 * 拱高 h = H·(1-(1-d/d_max)^4)；细分压弯中轴弦切误差。
 */

#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <vector>

namespace text3d {

/* 强度 0~1 → 拱高；k=0.35，且不超过 0.45*depth */
float inflate_height_from_strength(float strength_01, float depth);

/* 帽面鼓包；H<=0 或细分后仍无内部点时退化为平面，返回是否真正鼓起 */
bool append_inflated_caps(Mesh& out, const GlyphOutline& boundary, const std::vector<float>& xy,
                          const std::vector<unsigned int>& tris, float z_top, float z_bot, float H,
                          float minx, float miny, float sx, float sy);

}  // namespace text3d
