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

    std::cout << "[outline_boolean_test] PASS area=" << area << "\n";
    return 0;
}
