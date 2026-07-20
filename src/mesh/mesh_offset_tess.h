#pragma once
/*
 * 轮廓内缩 + 平面三角化（Bevel / Fillet 共用）。
 * 半径约束：2*R < depth，且 R < 笔画半宽；失败时 resolve_inset_radius 二分兜底。
 * TessBackend 可运行时切换：Earcut（需外环/孔嵌套）或 libtess2（NONZERO）。
 */

#include "text/ft_outline.h"

#include <vector>

namespace text3d {

enum class TessBackend {
    Earcut = 0,
    Libtess2 = 1,
};

void set_tess_backend(TessBackend backend);
TessBackend get_tess_backend();
const char* tess_backend_name(TessBackend backend);

/* 保证 2*R < depth，否则切棱空间不够 */
float clamp_edge_radius(float radius, float depth);

/* 估计最小笔画半宽（顶点到非邻接边最小距离之半）；失败时用 bbox 兜底 */
float estimate_min_half_width(const GlyphOutline& outline);

/* depth 与笔画半宽共同约束下的安全半径上限 */
float max_safe_edge_radius(const GlyphOutline& outline, float depth);

/* 对 outline（可含孔）三角化 → 交错 xy + 三角索引 */
bool try_tessellate(const GlyphOutline& outline, std::vector<float>& out_xy,
                    std::vector<unsigned int>& out_tris);

/* 先按笔画半宽钳制，再内缩+tess；过大则二分。成功写出 inner/xy/tris，失败返回 0 */
float resolve_inset_radius(const GlyphOutline& outline, float requested, GlyphOutline& inner,
                           std::vector<float>& xy, std::vector<unsigned int>& tris);

}  // namespace text3d
