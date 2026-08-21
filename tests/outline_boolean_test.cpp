/*
 * outline_boolean 冒烟：两矩形差集后无交、面积约 50。
 */

#include "mesh/outline_boolean.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

text3d::GlyphOutline make_rect(float x0, float y0, float x1, float y1) {
    text3d::GlyphOutline o;
    text3d::Contour c;
    // 外环 CW（Y-up）
    c.points.push_back({x0, y0});
    c.points.push_back({x0, y1});
    c.points.push_back({x1, y1});
    c.points.push_back({x1, y0});
    o.contours.push_back(std::move(c));
    return o;
}

void expect(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
    std::cout << "[ok] " << msg << "\n";
}

}  // namespace

int main() {
    const text3d::GlyphOutline a = make_rect(0.f, 0.f, 10.f, 10.f);
    const text3d::GlyphOutline b = make_rect(5.f, 0.f, 15.f, 10.f);

    expect(text3d::aabb_overlaps(text3d::compute_outline_aabb(a), text3d::compute_outline_aabb(b),
                                 0.5f),
           "aabb overlaps");
    expect(text3d::outline_intersection_area(a, b) > 49.0, "intersection area ~50");

    text3d::GlyphOutline out;
    std::vector<const text3d::GlyphOutline*> clips = {&a};
    expect(text3d::difference_outlines(b, clips, out, 0.f), "difference succeeds");
    expect(text3d::outline_intersection_area(a, out) < 1e-2, "no residual intersection");
    const double area = text3d::outline_abs_area(out);
    expect(std::fabs(area - 50.0) < 1.0, "result area ~50");

    text3d::GlyphOutline uni;
    std::vector<const text3d::GlyphOutline*> parts = {&a, &b};
    expect(text3d::union_outlines(parts, uni), "union succeeds");
    const double uni_area = text3d::outline_abs_area(uni);
    expect(std::fabs(uni_area - 150.0) < 2.0, "union area ~150");

    // 同一份 outline 里两条重叠外环（阿语组件构字）：unify 后应合成一块
    text3d::GlyphOutline overlap;
    overlap.contours.push_back(make_rect(0.f, 0.f, 10.f, 10.f).contours[0]);
    overlap.contours.push_back(make_rect(5.f, 0.f, 15.f, 10.f).contours[0]);
    expect(overlap.contours.size() == 2, "pre-unify two outers");
    expect(text3d::unify_outline_fill(overlap), "unify overlapping outers");
    expect(overlap.contours.size() == 1, "unify merged to one outer");
    expect(std::fabs(text3d::outline_abs_area(overlap) - 150.0) < 2.0, "unified area ~150");

    // 外环+孔不应被 unify 填实
    text3d::GlyphOutline eyed = make_rect(0.f, 0.f, 10.f, 10.f);
    text3d::Contour hole;
    hole.points.push_back({3.f, 3.f});
    hole.points.push_back({7.f, 3.f});
    hole.points.push_back({7.f, 7.f});
    hole.points.push_back({3.f, 7.f});  // CCW hole, Y-up
    eyed.contours.push_back(std::move(hole));
    expect(text3d::unify_outline_fill(eyed), "unify outer+hole");
    expect(eyed.contours.size() == 2, "hole preserved");
    expect(std::fabs(text3d::outline_abs_area(eyed) - 84.0) < 2.0, "area 100-16");

    // Union 碎环应丢掉
    text3d::GlyphOutline sliver = make_rect(0.f, 0.f, 10.f, 10.f);
    text3d::Contour junk;
    junk.points.push_back({1.f, 1.f});
    junk.points.push_back({1.2f, 1.f});
    junk.points.push_back({1.f, 1.2f});
    sliver.contours.push_back(std::move(junk));
    expect(text3d::unify_outline_fill(sliver), "unify drops sliver");
    expect(sliver.contours.size() == 1, "sliver dropped");

    std::cout << "[outline_boolean_test] PASS diff_area=" << area << " union_area=" << uni_area
              << "\n";
    return 0;
}
