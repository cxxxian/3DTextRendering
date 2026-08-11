#pragma once
/*
 * 2D 轮廓布尔：AABB 门闩 + Clipper2 并/差/相交面积。
 * 工程约定：外环 CW、孔 CCW（Y-up）；进出 Clipper 时统一翻转绕序。
 */

#include "text/ft_outline.h"

#include <vector>

namespace text3d {

struct Aabb2 {
    float min_x = 0.f;
    float min_y = 0.f;
    float max_x = 0.f;
    float max_y = 0.f;
};

Aabb2 compute_outline_aabb(const GlyphOutline& outline);

bool aabb_overlaps(const Aabb2& a, const Aabb2& b, float eps);

void translate_outline(GlyphOutline& outline, float dx, float dy);

/* 相交区域绝对面积（字体单位²）；无交返回 0 */
double outline_intersection_area(const GlyphOutline& a, const GlyphOutline& b);

/* 轮廓绝对面积（字体单位²） */
double outline_abs_area(const GlyphOutline& outline);

/*
 * out := Union(parts)。成功写出 out（已 clean）；失败返回 false。
 */
bool union_outlines(const std::vector<const GlyphOutline*>& parts, GlyphOutline& out);

/*
 * subject := Difference(subject, Union(clips))。
 * clip_inflate_delta>0 时先对裁刀 Inflate（字体单位）。
 * 成功写出 out（已 clean）；失败返回 false（out 未定义）。
 */
bool difference_outlines(const GlyphOutline& subject,
                         const std::vector<const GlyphOutline*>& clips, GlyphOutline& out,
                         float clip_inflate_delta = 0.f);

}  // namespace text3d
