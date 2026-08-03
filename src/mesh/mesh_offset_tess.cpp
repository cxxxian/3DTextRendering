/*
 * miter 内缩 + Earcut/libtess2 三角化 + 半径二分钳制。
 */

#include "mesh/mesh_offset_tess.h"

#include "mesh/contour_clean.h"
#include "mesh/mesh_geom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "earcut.hpp"
#include "tesselator.h"

namespace text3d {
namespace {

constexpr float kMiterLimit = 4.f;
constexpr int kBinSearchIters = 8;

/* 顶点沿内侧法线偏移 amount（side_normal 对本数据指向内侧） */
bool offset_contour(const Contour& src, float amount, Contour& dst) {
    dst.points.clear();
    const size_t n = src.points.size();
    if (n < 3 || amount <= kMeshEps) {
        dst = src;
        return amount <= kMeshEps;
    }

    dst.points.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec2& prev = src.points[(i + n - 1) % n];
        const Vec2& cur = src.points[i];
        const Vec2& next = src.points[(i + 1) % n];

        float n0x = 0.f, n0y = 0.f;
        float n1x = 0.f, n1y = 0.f;
        side_normal(prev.x, prev.y, cur.x, cur.y, n0x, n0y);
        side_normal(cur.x, cur.y, next.x, next.y, n1x, n1y);

        float bx = n0x + n1x;
        float by = n0y + n1y;
        const float blen = std::sqrt(bx * bx + by * by);
        if (blen < 1e-8f) {
            bx = n0x;
            by = n0y;
        } else {
            bx /= blen;
            by /= blen;
        }

        float cos_half = bx * n0x + by * n0y;
        if (cos_half < 1e-4f) {
            cos_half = 1e-4f;
        }
        float miter = amount / cos_half;
        const float max_miter = amount * kMiterLimit;
        if (miter > max_miter) {
            miter = max_miter;
        }

        dst.points[i] = {cur.x + bx * miter, cur.y + by * miter};
    }
    return true;
}

int orient2d(const Vec2& a, const Vec2& b, const Vec2& c) {
    const float v = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (v > 1e-8f) {
        return 1;
    }
    if (v < -1e-8f) {
        return -1;
    }
    return 0;
}

bool segments_intersect(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const int o1 = orient2d(a, b, c);
    const int o2 = orient2d(a, b, d);
    const int o3 = orient2d(c, d, a);
    const int o4 = orient2d(c, d, b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool contour_self_intersects(const Contour& c) {
    const size_t n = c.points.size();
    if (n < 4) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = c.points[i];
        const Vec2& b = c.points[(i + 1) % n];
        for (size_t j = i + 1; j < n; ++j) {
            if (j == i || (j + 1) % n == i || j == (i + 1) % n) {
                continue;
            }
            if (i == 0 && j == n - 1) {
                continue;
            }
            const Vec2& c0 = c.points[j];
            const Vec2& c1 = c.points[(j + 1) % n];
            if (segments_intersect(a, b, c0, c1)) {
                return true;
            }
        }
    }
    return false;
}

bool offset_looks_valid(const GlyphOutline& outer, const GlyphOutline& inner) {
    if (inner.contours.size() != outer.contours.size()) {
        return false;
    }
    for (size_t ci = 0; ci < outer.contours.size(); ++ci) {
        const Contour& o = outer.contours[ci];
        const Contour& in = inner.contours[ci];
        if (o.points.size() != in.points.size() || in.points.size() < 3) {
            return false;
        }
        for (size_t i = 0; i < in.points.size(); ++i) {
            const Vec2& a = in.points[i];
            const Vec2& b = in.points[(i + 1) % in.points.size()];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            if (dx * dx + dy * dy < 1e-12f) {
                return false;
            }
        }
        if (contour_self_intersects(in)) {
            return false;
        }
    }
    return true;
}

bool build_offset_outline(const GlyphOutline& outline, float amount, GlyphOutline& out) {
    out = GlyphOutline{};
    out.advance_x = outline.advance_x;
    out.bearing_x = outline.bearing_x;
    out.bearing_y = outline.bearing_y;
    out.contours.reserve(outline.contours.size());
    for (const Contour& c : outline.contours) {
        if (c.points.size() < 3) {
            continue;
        }
        Contour offset;
        offset_contour(c, amount, offset);
        out.contours.push_back(std::move(offset));
    }
    return offset_looks_valid(outline, out);
}

bool offset_and_tess_ok(const GlyphOutline& outline, float amount, TessMode mode,
                        GlyphOutline& inner, std::vector<float>& xy,
                        std::vector<unsigned int>& tris, std::string* fallback_reason) {
    if (!build_offset_outline(outline, amount, inner)) {
        return false;
    }
    // 内缩环与外环按下标配对，不能丢环；只清点 + 统一绕向
    CleanOptions opt;
    opt.drop_tiny_contours = false;
    if (!clean_glyph_outline(inner, opt)) {
        return false;
    }
    for (const Contour& c : inner.contours) {
        if (c.points.size() < 3) {
            return false;
        }
    }
    return try_tessellate(inner, mode, xy, tris, fallback_reason);
}

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
    const float qx = ax + abx * t;
    const float qy = ay + aby * t;
    const float dx = px - qx;
    const float dy = py - qy;
    return std::sqrt(dx * dx + dy * dy);
}

bool point_in_contour(const Contour& c, float x, float y) {
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
        const bool intersect =
            ((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + 1e-30f) + xi);
        if (intersect) {
            inside = !inside;
        }
    }
    return inside;
}

bool contour_contains(const Contour& outer, const Contour& inner) {
    if (inner.points.empty()) {
        return false;
    }
    return point_in_contour(outer, inner.points[0].x, inner.points[0].y);
}

float min_contour_edge_length(const Contour& c) {
    const size_t n = c.points.size();
    if (n < 2) {
        return 1e30f;
    }
    constexpr float kMinEdgeLen = 0.5f;  // 忽略细分碎边，避免误钳 R
    float min_len = 1e30f;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = c.points[i];
        const Vec2& b = c.points[(i + 1) % n];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len >= kMinEdgeLen) {
            min_len = std::min(min_len, len);
        }
    }
    return min_len;
}

float min_dist_contours(const Contour& a, const Contour& b) {
    float min_d = 1e30f;
    const auto test_points_to_edges = [&](const Contour& pts, const Contour& edges) {
        const size_t ne = edges.points.size();
        if (ne < 2) {
            return;
        }
        for (const Vec2& p : pts.points) {
            for (size_t j = 0; j < ne; ++j) {
                const Vec2& ea = edges.points[j];
                const Vec2& eb = edges.points[(j + 1) % ne];
                min_d = std::min(min_d, dist_point_segment(p.x, p.y, ea.x, ea.y, eb.x, eb.y));
            }
        }
    };
    test_points_to_edges(a, b);
    test_points_to_edges(b, a);
    return min_d;
}

/* 单环最短边之半：相邻角圆角在 R 超过此值时会在边上相撞 */
float estimate_min_edge_half(const GlyphOutline& outline) {
    float min_edge = 1e30f;
    for (const Contour& c : outline.contours) {
        min_edge = std::min(min_edge, min_contour_edge_length(c));
    }
    if (!(min_edge < 1e29f)) {
        return 0.f;
    }
    return 0.5f * min_edge;
}

/*
 * 不同轮廓边界之间的最小距离之半。
 * 含：外框↔孔、孔↔孔（如「田」四口之间）。R 超过此值时相邻棱带会相碰。
 */
float estimate_min_gap_half(const GlyphOutline& outline) {
    const size_t n = outline.contours.size();
    if (n < 2) {
        return 1e30f;
    }

    std::vector<int> depth(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            if (contour_contains(outline.contours[j], outline.contours[i])) {
                ++depth[i];
            }
        }
    }

    float min_gap = 1e30f;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const bool i_in_j = contour_contains(outline.contours[j], outline.contours[i]);
            const bool j_in_i = contour_contains(outline.contours[i], outline.contours[j]);
            if (i_in_j || j_in_i) {
                const Contour& inner_c =
                    i_in_j ? outline.contours[i] : outline.contours[j];
                const Contour& outer_c =
                    i_in_j ? outline.contours[j] : outline.contours[i];
                min_gap = std::min(min_gap, min_dist_contours(inner_c, outer_c));
            } else if (depth[i] == depth[j]) {
                min_gap =
                    std::min(min_gap, min_dist_contours(outline.contours[i], outline.contours[j]));
            }
        }
    }
    if (!(min_gap < 1e29f)) {
        return 1e30f;
    }
    return 0.5f * min_gap;
}

}  // namespace

float clamp_edge_radius(float radius, float depth) {
    if (!(radius > 0.f) || !(depth > 0.f)) {
        return 0.f;
    }
    const float max_b = depth * 0.5f - 1e-3f;
    if (max_b <= 0.f) {
        return 0.f;
    }
    return std::min(radius, max_b);
}

float estimate_min_half_width(const GlyphOutline& outline) {
    float limit = 1e30f;

    const float by_edge = estimate_min_edge_half(outline);
    if (by_edge > kMeshEps) {
        limit = std::min(limit, by_edge);
    }

    const float by_gap = estimate_min_gap_half(outline);
    if (by_gap < 1e29f) {
        limit = std::min(limit, by_gap);
    }

    if (limit < 1e29f) {
        return limit;
    }

    float minx = 0.f, miny = 0.f, maxx = 0.f, maxy = 0.f;
    outline_bounds(outline, minx, miny, maxx, maxy);
    return 0.25f * std::min(maxx - minx, maxy - miny);
}

float max_safe_edge_radius(const GlyphOutline& outline, float depth) {
    const float by_depth = clamp_edge_radius(1e30f, depth);
    float by_geom = 1e30f;

    const float edge_half = estimate_min_edge_half(outline);
    if (edge_half > kMeshEps) {
        // 同环相邻角圆角在短边上相撞：R ≈ 边长/2
        by_geom = std::min(by_geom, edge_half * 0.85f);
    }

    const float gap_half = estimate_min_gap_half(outline);
    if (gap_half < 1e29f) {
        // 相邻轮廓（孔↔孔、框↔孔）棱带相碰：R ≈ 间距/2，留更大余量
        by_geom = std::min(by_geom, gap_half * 0.75f);
    }

    if (by_geom > 1e29f) {
        by_geom = estimate_min_half_width(outline) * 0.9f;
    }

    if (!(by_geom > 0.f)) {
        return 0.f;
    }
    return std::min(by_depth, by_geom);
}

float edge_radius_from_strength(float strength_01, const GlyphOutline& outline, float depth) {
    if (!(strength_01 > 0.f)) {
        return 0.f;
    }
    const float t = std::min(1.f, strength_01);
    return t * max_safe_edge_radius(outline, depth);
}

const char* tess_backend_name(TessBackend backend) {
    switch (backend) {
        case TessBackend::Earcut:
            return "earcut";
        case TessBackend::Libtess2:
            return "libtess2";
    }
    return "unknown";
}

const char* tess_mode_name(TessMode mode) {
    switch (mode) {
        case TessMode::Auto:
            return "auto";
        case TessMode::Earcut:
            return "earcut";
        case TessMode::Libtess2:
            return "libtess2";
    }
    return "unknown";
}

namespace {

using EarPoint = std::array<double, 2>;

/* TrueType Y-up：外环 CW、孔 CCW；earcut 期望外环 CCW、孔 CW → 整环反转 */
std::vector<EarPoint> contour_to_earcut_ring(const Contour& c) {
    std::vector<EarPoint> ring;
    ring.reserve(c.points.size());
    for (size_t i = c.points.size(); i > 0; --i) {
        const Vec2& p = c.points[i - 1];
        ring.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
    }
    return ring;
}

bool earcut_one_group(const Contour& outer, const std::vector<const Contour*>& holes,
                      std::vector<float>& out_xy, std::vector<unsigned int>& out_tris) {
    std::vector<std::vector<EarPoint>> polygon;
    polygon.reserve(1 + holes.size());
    polygon.push_back(contour_to_earcut_ring(outer));
    for (const Contour* h : holes) {
        polygon.push_back(contour_to_earcut_ring(*h));
    }

    const std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);
    if (indices.size() < 3) {
        return false;
    }

    const unsigned base = static_cast<unsigned>(out_xy.size() / 2);
    for (const auto& ring : polygon) {
        for (const EarPoint& p : ring) {
            out_xy.push_back(static_cast<float>(p[0]));
            out_xy.push_back(static_cast<float>(p[1]));
        }
    }
    for (uint32_t idx : indices) {
        out_tris.push_back(base + idx);
    }
    return true;
}

bool try_tessellate_earcut(const GlyphOutline& outline, std::vector<float>& out_xy,
                           std::vector<unsigned int>& out_tris) {
    out_xy.clear();
    out_tris.clear();

    std::vector<const Contour*> rings;
    rings.reserve(outline.contours.size());
    for (const Contour& c : outline.contours) {
        if (c.points.size() >= 3) {
            rings.push_back(&c);
        }
    }
    if (rings.empty()) {
        return false;
    }

    const size_t n = rings.size();
    std::vector<int> depth(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            if (contour_contains(*rings[j], *rings[i])) {
                ++depth[i];
            }
        }
    }

    // 偶深度 = 实心（外环/岛），奇深度 = 孔
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        if ((depth[i] & 1) != 0) {
            continue;
        }
        std::vector<const Contour*> holes;
        for (size_t j = 0; j < n; ++j) {
            if (depth[j] == depth[i] + 1 && contour_contains(*rings[i], *rings[j])) {
                holes.push_back(rings[j]);
            }
        }
        if (earcut_one_group(*rings[i], holes, out_xy, out_tris)) {
            any = true;
        }
    }
    return any && !out_tris.empty();
}

bool try_tessellate_libtess2(const GlyphOutline& outline, std::vector<float>& out_xy,
                             std::vector<unsigned int>& out_tris) {
    out_xy.clear();
    out_tris.clear();
    TESStesselator* tess = tessNewTess(nullptr);
    if (!tess) {
        return false;
    }

    for (const Contour& c : outline.contours) {
        if (c.points.size() < 3) {
            continue;
        }
        std::vector<TESSreal> coords;
        coords.reserve(c.points.size() * 2);
        for (const Vec2& p : c.points) {
            coords.push_back(p.x);
            coords.push_back(p.y);
        }
        tessAddContour(tess, 2, coords.data(), sizeof(TESSreal) * 2,
                       static_cast<int>(c.points.size()));
    }

    if (!tessTesselate(tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 2, nullptr)) {
        tessDeleteTess(tess);
        return false;
    }

    const int vert_count = tessGetVertexCount(tess);
    const TESSreal* verts = tessGetVertices(tess);
    const int elem_count = tessGetElementCount(tess);
    const TESSindex* elems = tessGetElements(tess);
    if (vert_count <= 0 || elem_count <= 0) {
        tessDeleteTess(tess);
        return false;
    }

    out_xy.resize(static_cast<size_t>(vert_count) * 2);
    for (int i = 0; i < vert_count; ++i) {
        out_xy[static_cast<size_t>(i) * 2] = verts[i * 2];
        out_xy[static_cast<size_t>(i) * 2 + 1] = verts[i * 2 + 1];
    }
    for (int e = 0; e < elem_count; ++e) {
        const TESSindex i0 = elems[e * 3 + 0];
        const TESSindex i1 = elems[e * 3 + 1];
        const TESSindex i2 = elems[e * 3 + 2];
        if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF) {
            continue;
        }
        out_tris.push_back(static_cast<unsigned>(i0));
        out_tris.push_back(static_cast<unsigned>(i1));
        out_tris.push_back(static_cast<unsigned>(i2));
    }
    tessDeleteTess(tess);
    return !out_tris.empty();
}

}  // namespace

bool try_tessellate_one(TessBackend backend, const GlyphOutline& outline,
                        std::vector<float>& out_xy, std::vector<unsigned int>& out_tris) {
    if (backend == TessBackend::Libtess2) {
        return try_tessellate_libtess2(outline, out_xy, out_tris);
    }
    return try_tessellate_earcut(outline, out_xy, out_tris);
}

bool try_tessellate(const GlyphOutline& outline, TessMode mode, std::vector<float>& out_xy,
                    std::vector<unsigned int>& out_tris, std::string* fallback_reason) {
    if (mode == TessMode::Libtess2) {
        return try_tessellate_one(TessBackend::Libtess2, outline, out_xy, out_tris);
    }
    if (mode == TessMode::Earcut) {
        return try_tessellate_one(TessBackend::Earcut, outline, out_xy, out_tris);
    }

    // Auto：Earcut → libtess2
    if (try_tessellate_one(TessBackend::Earcut, outline, out_xy, out_tris)) {
        return true;
    }
    std::vector<float> fb_xy;
    std::vector<unsigned int> fb_tris;
    if (try_tessellate_one(TessBackend::Libtess2, outline, fb_xy, fb_tris)) {
        out_xy = std::move(fb_xy);
        out_tris = std::move(fb_tris);
        if (fallback_reason && fallback_reason->empty()) {
            *fallback_reason = "tess earcut failed; used libtess2";
        }
        return true;
    }
    if (fallback_reason && fallback_reason->empty()) {
        *fallback_reason = "tess earcut+libtess2 failed";
    }
    out_xy.clear();
    out_tris.clear();
    return false;
}

float resolve_inset_radius(const GlyphOutline& outline, float requested, float depth, TessMode mode,
                           GlyphOutline& inner, std::vector<float>& xy,
                           std::vector<unsigned int>& tris, std::string* fallback_reason) {
    if (requested <= kMeshEps) {
        return 0.f;
    }

    const float stroke_cap = max_safe_edge_radius(outline, depth);
    float r = requested;
    if (stroke_cap > kMeshEps && r > stroke_cap) {
        std::cerr << "[mesh_offset] radius " << requested << " 超过安全上限（短边/邻距），钳制到 "
                  << stroke_cap << "\n";
        r = stroke_cap;
    }

    if (r <= kMeshEps) {
        std::cerr << "[mesh_offset] 笔画过窄，无法内缩\n";
        return 0.f;
    }
    if (offset_and_tess_ok(outline, r, mode, inner, xy, tris, fallback_reason)) {
        return r;
    }

    float lo = 0.f;
    float hi = r;
    float best = 0.f;
    GlyphOutline best_inner;
    std::vector<float> best_xy;
    std::vector<unsigned int> best_tris;
    std::string best_fallback;

    for (int i = 0; i < kBinSearchIters; ++i) {
        const float mid = 0.5f * (lo + hi);
        GlyphOutline trial_inner;
        std::vector<float> trial_xy;
        std::vector<unsigned int> trial_tris;
        std::string trial_fallback;
        if (offset_and_tess_ok(outline, mid, mode, trial_inner, trial_xy, trial_tris,
                               &trial_fallback)) {
            best = mid;
            best_inner = std::move(trial_inner);
            best_xy = std::move(trial_xy);
            best_tris = std::move(trial_tris);
            best_fallback = std::move(trial_fallback);
            lo = mid;
        } else {
            hi = mid;
        }
    }

    if (best > kMeshEps) {
        std::cerr << "[mesh_offset] radius " << r << " 自交/失败，缩小到 " << best << "\n";
        inner = std::move(best_inner);
        xy = std::move(best_xy);
        tris = std::move(best_tris);
        if (fallback_reason && fallback_reason->empty() && !best_fallback.empty()) {
            *fallback_reason = std::move(best_fallback);
        }
        return best;
    }

    std::cerr << "[mesh_offset] radius " << r << " 无法内缩，退回 0\n";
    return 0.f;
}

}  // namespace text3d
