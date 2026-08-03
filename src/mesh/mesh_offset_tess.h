#pragma once
/*
 * 轮廓内缩 + 平面三角化（Bevel / Fillet 共用）。
 * 半径约束：2*R < depth，且 R 不超过最短边/相邻轮廓间距之半（防相邻圆角相碰）；失败时 resolve 二分兜底。
 * TessMode：Auto（Earcut→libtess2）或强制单一后端；选项随请求传入，无全局可变后端。
 */

#include "text/ft_outline.h"

#include <string>
#include <vector>

namespace text3d {

enum class TessBackend {
    Earcut = 0,
    Libtess2 = 1,
};

/* Auto：主用 Earcut，失败再 libtess2；其余为强制单后端（不回退） */
enum class TessMode {
    Auto = 0,
    Earcut = 1,
    Libtess2 = 2,
};

const char* tess_backend_name(TessBackend backend);
const char* tess_mode_name(TessMode mode);

/* 保证 2*R < depth，否则切棱空间不够 */
float clamp_edge_radius(float radius, float depth);

/* 估计安全半径上限：min(最短边/2, 相邻轮廓最小间距/2)；失败时用 bbox 兜底 */
float estimate_min_half_width(const GlyphOutline& outline);

/* depth 与笔画半宽共同约束下的安全半径上限 */
float max_safe_edge_radius(const GlyphOutline& outline, float depth);

/* 强度 0~1 → 请求半径；再经 max_safe / resolve 得到实际 R */
float edge_radius_from_strength(float strength_01, const GlyphOutline& outline, float depth);

/* 对 outline（可含孔）三角化 → 交错 xy + 三角索引；回退时写入 fallback_reason */
bool try_tessellate(const GlyphOutline& outline, TessMode mode, std::vector<float>& out_xy,
                    std::vector<unsigned int>& out_tris,
                    std::string* fallback_reason = nullptr);

/* 先按短边/邻距钳制，再内缩+tess；过大则二分。成功写出 inner/xy/tris，失败返回 0 */
float resolve_inset_radius(const GlyphOutline& outline, float requested, float depth, TessMode mode,
                           GlyphOutline& inner, std::vector<float>& xy,
                           std::vector<unsigned int>& tris,
                           std::string* fallback_reason = nullptr);

}  // namespace text3d
