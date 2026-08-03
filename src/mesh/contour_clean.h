#pragma once
/*
 * 轮廓清理：去退化点、认外环/孔、统一绕向（外 CW、孔 CCW）。
 * 在三角化 / 内缩前调用；复杂度按环 O(n)。
 */

#include "text/ft_outline.h"

namespace text3d {

struct CleanOptions {
    float merge_eps = 1e-3f;           // 重复点 / 零长度边阈值阈值
    float collinear_eps = 1e-3f;       // 近共线：|叉积| / (|e0||e1|) 阈值
    bool unify_winding = true;         // 外 CW、孔 CCW
    bool drop_tiny_contours = true;    // 清理后点数 < 3 丢弃
};

/* 有向面积的 2 倍（鞋带公式）；Y-up 下 >0 为 CCW，<0 为 CW */
float contour_signed_area2(const Contour& c);

/* 就地清理单个环的重复点 / 零边 / 近共线点；返回保留点数 */
int clean_contour_points(Contour& c, const CleanOptions& opt);

/*
 * 清理整字轮廓：逐环清点 → 嵌套深度认孔 → 统一绕向。
 * 成功时至少保留一条有效外环；失败返回 false（contours 可能被清空）。
 */
bool clean_glyph_outline(GlyphOutline& io, const CleanOptions& opt = {});

}  // namespace text3d
