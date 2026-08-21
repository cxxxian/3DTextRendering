/*
 * 诊断：阿拉伯连写 Union 后，倒角/圆角内缩为何镂空。
 * 不改生产逻辑，只打印证据。
 */

#include "mesh/contour_clean.h"
#include "mesh/glyph_planar.h"
#include "mesh/mesh_extrude.h"
#include "mesh/mesh_offset_tess.h"
#include "mesh/outline_boolean.h"
#include "text/ft_outline.h"
#include "text/hb_shaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

float dist_point_segment(float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float ab2 = abx * abx + aby * aby;
    float t = 0.f;
    if (ab2 > 1e-12f) {
        t = (apx * abx + apy * aby) / ab2;
        t = std::max(0.f, std::min(1.f, t));
    }
    const float dx = px - (ax + abx * t);
    const float dy = py - (ay + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

bool point_in_contour(const text3d::Contour& c, float x, float y) {
    const size_t n = c.points.size();
    if (n < 3) {
        return false;
    }
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const float yi = c.points[i].y;
        const float yj = c.points[j].y;
        const float xi = c.points[i].x;
        const float xj = c.points[j].x;
        const bool hit =
            ((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + 1e-30f) + xi);
        if (hit) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<int> nest_depth(const text3d::GlyphOutline& o) {
    const size_t n = o.contours.size();
    std::vector<int> depth(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (o.contours[i].points.empty()) {
            continue;
        }
        for (size_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            const auto& inner = o.contours[i];
            const auto& outer = o.contours[j];
            if (point_in_contour(outer, inner.points[0].x, inner.points[0].y)) {
                ++depth[i];
            }
        }
    }
    return depth;
}

/* 单环自贴近：点到非邻接边的最小距离（连写细笔画半宽） */
float min_self_pinch(const text3d::Contour& c) {
    const size_t n = c.points.size();
    if (n < 6) {
        return 1e30f;
    }
    float best = 1e30f;
    for (size_t i = 0; i < n; ++i) {
        const auto& p = c.points[i];
        for (size_t j = 0; j < n; ++j) {
            const size_t j1 = (j + 1) % n;
            const int di = static_cast<int>(std::min((j + n - i) % n, (i + n - j) % n));
            const int dj = static_cast<int>(std::min((j1 + n - i) % n, (i + n - j1) % n));
            if (di <= 2 || dj <= 2) {
                continue;
            }
            best = std::min(best, dist_point_segment(p.x, p.y, c.points[j].x, c.points[j].y,
                                                     c.points[j1].x, c.points[j1].y));
        }
    }
    return best;
}

float tess_area(const std::vector<float>& xy, const std::vector<unsigned int>& tris) {
    double a = 0.0;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const unsigned i0 = tris[t];
        const unsigned i1 = tris[t + 1];
        const unsigned i2 = tris[t + 2];
        const float ax = xy[i0 * 2];
        const float ay = xy[i0 * 2 + 1];
        const float bx = xy[i1 * 2];
        const float by = xy[i1 * 2 + 1];
        const float cx = xy[i2 * 2];
        const float cy = xy[i2 * 2 + 1];
        a += 0.5 * static_cast<double>((bx - ax) * (cy - ay) - (cx - ax) * (by - ay));
    }
    return static_cast<float>(std::fabs(a));
}

void dump_outline(const char* tag, const text3d::GlyphOutline& o);
void dump_inset(const char* tag, const text3d::GlyphOutline& o, float fillet);

bool prepare_unioned(const text3d::FontFace& font, const std::string& text,
                     text3d::GlyphOutline& out_union, int& glyph_n, int& run_n) {
    std::vector<text3d::ShapedGlyph> shaped;
    if (!text3d::shape_text(font, text, text3d::ShapeOptions{}, shaped)) {
        return false;
    }
    glyph_n = static_cast<int>(shaped.size());

    std::vector<text3d::GlyphOutline> laid;
    float pen_x = 0.f;
    for (const auto& sg : shaped) {
        text3d::GlyphOutline raw;
        if (!font.load_glyph_outline_by_index(sg.glyph_index, raw, 1.f)) {
            pen_x += sg.x_advance;
            continue;
        }
        text3d::GlyphOutline cleaned;
        if (!text3d::build_glyph_cleaned_outline(raw, cleaned)) {
            pen_x += sg.x_advance;
            continue;
        }
        text3d::translate_outline(cleaned, pen_x + sg.x_offset, sg.y_offset);
        {
            const auto d = nest_depth(cleaned);
            int outer0 = 0;
            int holes = 0;
            for (int v : d) {
                if ((v & 1) == 0) {
                    ++outer0;
                } else {
                    ++holes;
                }
            }
            std::cout << "  glyph " << sg.glyph_index << " contours=" << cleaned.contours.size()
                      << " depth0_outers=" << outer0 << " holes=" << holes << "\n";
        }
        laid.push_back(std::move(cleaned));
        pen_x += sg.x_advance;
    }
    if (laid.empty()) {
        return false;
    }

    const size_t n = laid.size();
    std::vector<int> parent(n);
    for (size_t i = 0; i < n; ++i) {
        parent[i] = static_cast<int>(i);
    }
    auto find_root = [&](int x) {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    };
    auto unite = [&](int a, int b) {
        a = find_root(a);
        b = find_root(b);
        if (a != b) {
            parent[static_cast<size_t>(b)] = a;
        }
    };

    std::vector<text3d::Aabb2> boxes;
    boxes.reserve(n);
    for (const auto& o : laid) {
        boxes.push_back(text3d::compute_outline_aabb(o));
    }
    for (size_t j = 0; j < n; ++j) {
        for (size_t i = 0; i < j; ++i) {
            if (static_cast<int>(j - i) > 2) {
                continue;
            }
            if (!text3d::aabb_overlaps(boxes[i], boxes[j], 0.5f)) {
                continue;
            }
            if (text3d::outline_intersection_area(laid[i], laid[j]) < 1e-2) {
                continue;
            }
            unite(static_cast<int>(i), static_cast<int>(j));
        }
    }

    std::vector<std::vector<size_t>> comps(n);
    for (size_t i = 0; i < n; ++i) {
        comps[static_cast<size_t>(find_root(static_cast<int>(i)))].push_back(i);
    }

    run_n = 0;
    std::vector<text3d::GlyphOutline> runs;
    for (size_t r = 0; r < n; ++r) {
        if (comps[r].empty()) {
            continue;
        }
        ++run_n;
        std::vector<const text3d::GlyphOutline*> parts;
        for (size_t idx : comps[r]) {
            parts.push_back(&laid[idx]);
        }
        text3d::GlyphOutline united;
        if (parts.size() == 1) {
            united = *parts[0];
        } else if (!text3d::union_outlines(parts, united)) {
            std::cerr << "union failed for a run\n";
            continue;
        }
        runs.push_back(std::move(united));
    }
    if (runs.empty()) {
        return false;
    }
    for (size_t i = 0; i < runs.size(); ++i) {
        const std::string tag = "run[" + std::to_string(i) + "]";
        std::cout << "\n#### " << tag << " members_area=" << text3d::outline_abs_area(runs[i])
                  << "\n";
        dump_outline(tag.c_str(), runs[i]);
        dump_inset(tag.c_str(), runs[i], 0.6f);
    }

    out_union = runs[0];
    double biggest_area = text3d::outline_abs_area(out_union);
    for (size_t i = 1; i < runs.size(); ++i) {
        const double a = text3d::outline_abs_area(runs[i]);
        if (a > biggest_area) {
            biggest_area = a;
            out_union = runs[i];
        }
    }
    return !out_union.contours.empty();
}

void dump_outline(const char* tag, const text3d::GlyphOutline& o) {
    const auto depth = nest_depth(o);
    std::cout << "\n=== " << tag << " ===\n";
    std::cout << "contours=" << o.contours.size() << " abs_area=" << text3d::outline_abs_area(o)
              << "\n";
    float min_pinch = 1e30f;
    int holes = 0;
    int micro_holes = 0;
    for (size_t i = 0; i < o.contours.size(); ++i) {
        const float a2 = text3d::contour_signed_area2(o.contours[i]);
        const float area = std::fabs(a2) * 0.5f;
        const bool hole = (depth[i] & 1) != 0;
        if (hole) {
            ++holes;
            if (area < 8.f) {
                ++micro_holes;
            }
        }
        const float pinch = min_self_pinch(o.contours[i]);
        if (!hole) {
            min_pinch = std::min(min_pinch, pinch);
        }
        std::cout << "  c" << i << " pts=" << o.contours[i].points.size() << " depth=" << depth[i]
                  << (hole ? " HOLE" : " OUTER") << " area=" << area << " pinch=" << pinch << "\n";
    }
    const float safe = text3d::max_safe_edge_radius(o, 24.f);
    const float half = text3d::estimate_min_half_width(o);
    std::cout << "holes=" << holes << " micro_holes(area<8)=" << micro_holes
              << " min_outer_pinch=" << min_pinch << "\n";
    std::cout << "estimate_min_half_width=" << half << " max_safe_R@depth24=" << safe << "\n";
    if (min_pinch < 1e29f) {
        std::cout << "pinch_half=" << (0.5f * min_pinch) << "  safe_R / pinch_half="
                  << (safe / std::max(0.5f * min_pinch, 1e-6f)) << "\n";
        if (safe > 0.5f * min_pinch) {
            std::cout << "!! safe_R 大于单环自贴近半宽：内缩会把细笔画掐断\n";
        }
    }
}

void dump_inset(const char* tag, const text3d::GlyphOutline& o, float fillet) {
    text3d::ExtrudeOptions opt;
    opt.depth = 24.f;
    opt.fillet = fillet;
    opt.bevel = 0.f;
    opt.inflate = 0.f;
    opt.tess_mode = text3d::TessMode::Auto;
    text3d::GlyphPlanar2D planar;
    text3d::BuildResult br;
    const bool ok = text3d::build_glyph_planar_from_cleaned(o, opt, planar, &br);
    std::cout << "\n--- inset " << tag << " fillet=" << fillet << " ok=" << ok
              << " applied_R=" << planar.applied_radius << " " << text3d::build_result_format(br)
              << " ---\n";
    if (!ok) {
        return;
    }

    std::vector<float> outer_xy;
    std::vector<unsigned int> outer_tris;
    text3d::try_tessellate(o, text3d::TessMode::Auto, outer_xy, outer_tris);
    const float a_outer = tess_area(outer_xy, outer_tris);
    const float a_inner = tess_area(planar.cap_xy, planar.cap_tris);
    std::cout << "outer_tess_area=" << a_outer << " inner_cap_area=" << a_inner
              << " ratio=" << (a_outer > 1e-6f ? a_inner / a_outer : 0.f) << "\n";
    std::cout << "outer_contours=" << planar.cleaned.contours.size()
              << " inner_contours=" << planar.inner.contours.size() << "\n";

    const size_t nc = std::min(planar.cleaned.contours.size(), planar.inner.contours.size());
    int pts_mismatch = 0;
    int nest_broken = 0;
    const auto d_out = nest_depth(planar.cleaned);
    const auto d_in = nest_depth(planar.inner);
    for (size_t i = 0; i < nc; ++i) {
        const size_t po = planar.cleaned.contours[i].points.size();
        const size_t pi = planar.inner.contours[i].points.size();
        if (po != pi) {
            ++pts_mismatch;
        }
        if (i < d_out.size() && i < d_in.size() && d_out[i] != d_in[i]) {
            ++nest_broken;
        }
        std::cout << "  pair" << i << " outer_pts=" << po << " inner_pts=" << pi
                  << " outer_depth=" << (i < d_out.size() ? d_out[i] : -1)
                  << " inner_depth=" << (i < d_in.size() ? d_in[i] : -1) << "\n";
    }
    std::cout << "index_pts_mismatch=" << pts_mismatch << " nest_depth_changed=" << nest_broken
              << "\n";
    if (pts_mismatch > 0) {
        std::cout << "!! 内外环点数不一致：棱带按下标配对会漏面（帽面/棱带之间镂空）\n";
    }
    if (a_outer > 1e-6f && a_inner / a_outer < 0.55f && planar.applied_radius > 0.5f) {
        std::cout << "!! 帽面面积掉太多，像镂空/细笔画被内缩吃掉\n";
    }
}

}  // namespace

int main() {
    {
        // 外环带共线中点：内缩后若再清点，inner 点数会变少，棱带漏面。
        text3d::GlyphOutline sq;
        text3d::Contour c;
        c.points = {{0.f, 0.f},  {0.f, 5.f},  {0.f, 10.f}, {5.f, 10.f},
                    {10.f, 10.f}, {10.f, 5.f}, {10.f, 0.f}, {5.f, 0.f}};
        sq.contours.push_back(std::move(c));
        text3d::ExtrudeOptions opt;
        opt.depth = 24.f;
        opt.fillet = 0.5f;
        text3d::GlyphPlanar2D planar;
        text3d::BuildResult br;
        const bool ok = text3d::build_glyph_planar_from_cleaned(sq, opt, planar, &br);
        const size_t n_out =
            planar.cleaned.contours.empty() ? 0 : planar.cleaned.contours[0].points.size();
        const size_t n_in =
            planar.inner.contours.empty() ? 0 : planar.inner.contours[0].points.size();
        std::cout << "=== collinear-square pair test ok=" << ok << " outer_pts=" << n_out
                  << " inner_pts=" << n_in << " applied_R=" << planar.applied_radius << " ===\n";
        if (!ok || n_out != n_in || n_out != 8) {
            std::cerr << "[FAIL] inset must keep 1:1 vertex pairing with outer\n";
            return 1;
        }
        std::cout << "[ok] inset keeps 1:1 pairing\n";
    }

    const char* latin_font = "/System/Library/Fonts/Supplemental/Arial.ttf";
    const char* arabic_font = "/System/Library/Fonts/SFArabic.ttf";

    text3d::FontFace latin;
    text3d::FontFace arabic;
    if (!latin.load(latin_font, 128)) {
        std::cerr << "latin font load failed\n";
        return 1;
    }
    if (!arabic.load(arabic_font, 128)) {
        std::cerr << "arabic font load failed: " << arabic_font << "\n";
        return 1;
    }

    text3d::GlyphOutline hello;
    int g = 0, r = 0;
    if (!prepare_unioned(latin, "Hello", hello, g, r)) {
        std::cerr << "Hello prepare failed\n";
        return 1;
    }
    std::cout << "Hello glyphs=" << g << " runs=" << r << "\n";
    dump_outline("Hello (no union expected)", hello);
    dump_inset("Hello", hello, 0.6f);

    text3d::GlyphOutline marhaba;
    if (!prepare_unioned(arabic, u8"مرحبا", marhaba, g, r)) {
        std::cerr << "marhaba prepare failed\n";
        return 1;
    }
    std::cout << "\nمرحبا glyphs=" << g << " runs=" << r << "\n";
    dump_outline("مرحبا biggest run", marhaba);

    // 丢掉 Union 碎环后再看安全半径：模拟「没有退化环把门闩打到 0」
    text3d::GlyphOutline stripped = marhaba;
    stripped.contours.erase(
        std::remove_if(stripped.contours.begin(), stripped.contours.end(),
                       [](const text3d::Contour& c) {
                           return std::fabs(text3d::contour_signed_area2(c)) * 0.5f < 8.f;
                       }),
        stripped.contours.end());
    dump_outline("مرحبا stripped micro contours", stripped);
    dump_inset("مرحبا stripped fillet=0.6", stripped, 0.6f);

    std::cout << "\n=== per-glyph Clipper unify (overlapping outers → outer+hole) ===\n";
    {
        text3d::FontFace& f = arabic;
        std::vector<text3d::ShapedGlyph> shaped;
        text3d::shape_text(f, u8"مرحبا", text3d::ShapeOptions{}, shaped);
        for (const auto& sg : shaped) {
            text3d::GlyphOutline raw;
            if (!f.load_glyph_outline_by_index(sg.glyph_index, raw, 1.f)) {
                continue;
            }
            text3d::GlyphOutline cleaned;
            if (!text3d::build_glyph_cleaned_outline(raw, cleaned)) {
                continue;
            }
            std::vector<text3d::GlyphOutline> parts_store;
            std::vector<const text3d::GlyphOutline*> parts;
            for (const auto& c : cleaned.contours) {
                text3d::GlyphOutline one;
                one.contours.push_back(c);
                parts_store.push_back(std::move(one));
            }
            for (const auto& p : parts_store) {
                parts.push_back(&p);
            }
            text3d::GlyphOutline uni;
            if (!text3d::union_outlines(parts, uni)) {
                std::cout << "  glyph " << sg.glyph_index << " unify FAILED\n";
                continue;
            }
            dump_outline(("unify glyph " + std::to_string(sg.glyph_index)).c_str(), uni);
            dump_inset(("unify glyph " + std::to_string(sg.glyph_index)).c_str(), uni, 0.6f);
        }
    }

    {
        text3d::GlyphOutline raw;
        latin.load_glyph_outline_by_index(72, raw, 1.f);  // e
        text3d::GlyphOutline cleaned;
        text3d::build_glyph_cleaned_outline(raw, cleaned);
        std::vector<text3d::GlyphOutline> parts_store;
        std::vector<const text3d::GlyphOutline*> parts;
        for (const auto& c : cleaned.contours) {
            text3d::GlyphOutline one;
            one.contours.push_back(c);
            parts_store.push_back(std::move(one));
        }
        for (const auto& p : parts_store) {
            parts.push_back(&p);
        }
        text3d::GlyphOutline uni;
        text3d::union_outlines(parts, uni);
        dump_outline("unify Latin e (should keep 1 hole)", uni);
        dump_inset("unify Latin e", uni, 0.6f);
    }

    // 强制内缩到 pinch 以上，看帽面是否被掐断
    text3d::GlyphOutline inner;
    std::vector<float> xy;
    std::vector<unsigned int> tris;
    std::string fb;
    const float forced = 2.f;
    const float got =
        text3d::resolve_inset_radius(stripped, forced, 24.f, text3d::TessMode::Auto, inner, xy,
                                     tris, &fb);
    std::vector<float> o_xy;
    std::vector<unsigned int> o_tris;
    text3d::try_tessellate(stripped, text3d::TessMode::Auto, o_xy, o_tris);
    std::cout << "\n=== forced inset R_req=" << forced << " R_got=" << got
              << " outer_area=" << tess_area(o_xy, o_tris)
              << " inner_area=" << tess_area(xy, tris) << " fb=" << fb << " ===\n";

    return 0;
}
