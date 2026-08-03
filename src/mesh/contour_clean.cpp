/*
 * 轮廓清理实现：相邻点线性扫描，不做全点对距离。
 */

#include "mesh/contour_clean.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace text3d {
namespace {

float dist2(const Vec2& a, const Vec2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float dist(const Vec2& a, const Vec2& b) {
    return std::sqrt(dist2(a, b));
}

/* 2D 叉积 z：(b-a)×(c-b) */
float cross(const Vec2& a, const Vec2& b, const Vec2& c) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float bcx = c.x - b.x;
    const float bcy = c.y - b.y;
    return abx * bcy - aby * bcx;
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

bool contour_contains(const Contour& a, const Contour& b) {
    if (b.points.empty()) {
        return false;
    }
    // 用环上多个采样降低落在边上的误判
    const size_t n = b.points.size();
    const size_t samples = std::min<size_t>(n, 3);
    int inside_votes = 0;
    for (size_t s = 0; s < samples; ++s) {
        const size_t idx = (s * n) / samples;
        if (point_in_contour(a, b.points[idx].x, b.points[idx].y)) {
            ++inside_votes;
        }
    }
    return inside_votes * 2 >= static_cast<int>(samples);
}

void reverse_contour(Contour& c) {
    std::reverse(c.points.begin(), c.points.end());
}

/* 合并相邻近重复点（含首尾），O(n) */
void merge_nearby_points(Contour& c, float eps) {
    if (c.points.size() < 2) {
        return;
    }
    const float eps2 = eps * eps;
    std::vector<Vec2> out;
    out.reserve(c.points.size());
    out.push_back(c.points[0]);
    for (size_t i = 1; i < c.points.size(); ++i) {
        if (dist2(out.back(), c.points[i]) > eps2) {
            out.push_back(c.points[i]);
        }
    }
    if (out.size() >= 2 && dist2(out.front(), out.back()) <= eps2) {
        out.pop_back();
    }
    c.points = std::move(out);
}

/*
 * 去掉近共线中间点。保守策略：
 * - 只删相邻三点中的中间点
 * - 相对叉积足够小，且中间点投影大致落在边上
 * - 多趟直到稳定，避免一次删太多引入折角自交
 */
void remove_collinear_points(Contour& c, float collinear_eps) {
    if (c.points.size() < 3 || collinear_eps <= 0.f) {
        return;
    }

    constexpr int kMaxPasses = 8;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        const size_t n = c.points.size();
        if (n < 3) {
            break;
        }
        std::vector<char> keep(n, 1);
        int removed = 0;
        for (size_t i = 0; i < n; ++i) {
            const size_t i0 = (i + n - 1) % n;
            const size_t i1 = i;
            const size_t i2 = (i + 1) % n;
            if (!keep[i0] || !keep[i2]) {
                continue;
            }
            const Vec2& a = c.points[i0];
            const Vec2& b = c.points[i1];
            const Vec2& d = c.points[i2];
            const float e0 = dist(a, b);
            const float e1 = dist(b, d);
            if (e0 < 1e-12f || e1 < 1e-12f) {
                keep[i1] = 0;
                ++removed;
                continue;
            }
            const float cr = std::fabs(cross(a, b, d));
            const float rel = cr / (e0 * e1);
            if (rel > collinear_eps) {
                continue;
            }
            // 投影落在 [a,d] 线段附近才删，避免削掉真正尖角
            const float adx = d.x - a.x;
            const float ady = d.y - a.y;
            const float ad2 = adx * adx + ady * ady;
            if (ad2 < 1e-20f) {
                keep[i1] = 0;
                ++removed;
                continue;
            }
            const float t = ((b.x - a.x) * adx + (b.y - a.y) * ady) / ad2;
            if (t > 0.02f && t < 0.98f) {
                keep[i1] = 0;
                ++removed;
            }
        }
        if (removed == 0) {
            break;
        }
        // 同趟不删相邻两点，降低局部形状突变
        std::vector<Vec2> out;
        out.reserve(n);
        bool prev_removed = false;
        for (size_t i = 0; i < n; ++i) {
            if (!keep[i]) {
                if (prev_removed) {
                    out.push_back(c.points[i]);  // 保留，下趟再议
                    prev_removed = false;
                } else {
                    prev_removed = true;
                }
                continue;
            }
            out.push_back(c.points[i]);
            prev_removed = false;
        }
        if (out.size() < 3) {
            c.points = std::move(out);
            break;
        }
        c.points = std::move(out);
    }
}

}  // namespace

float contour_signed_area2(const Contour& c) {
    const size_t n = c.points.size();
    if (n < 3) {
        return 0.f;
    }
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& p0 = c.points[i];
        const Vec2& p1 = c.points[(i + 1) % n];
        sum += static_cast<double>(p0.x) * static_cast<double>(p1.y) -
               static_cast<double>(p1.x) * static_cast<double>(p0.y);
    }
    return static_cast<float>(sum);
}

int clean_contour_points(Contour& c, const CleanOptions& opt) {
    merge_nearby_points(c, opt.merge_eps);
    remove_collinear_points(c, opt.collinear_eps);
    merge_nearby_points(c, opt.merge_eps);  // 共线删除后再并一次
    return static_cast<int>(c.points.size());
}

bool clean_glyph_outline(GlyphOutline& io, const CleanOptions& opt) {
    std::vector<Contour> cleaned;
    cleaned.reserve(io.contours.size());

    for (Contour& c : io.contours) {
        clean_contour_points(c, opt);
        if (!opt.drop_tiny_contours || c.points.size() >= 3) {
            cleaned.push_back(std::move(c));
        }
    }
    io.contours = std::move(cleaned);

    if (io.contours.empty()) {
        return false;
    }

    if (!opt.unify_winding) {
        return true;
    }

    const size_t n = io.contours.size();
    std::vector<int> depth(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            if (contour_contains(io.contours[j], io.contours[i])) {
                ++depth[i];
            }
        }
    }

    // 偶深度 = 外/岛 → CW（area2 < 0）；奇深度 = 孔 → CCW（area2 > 0）
    for (size_t i = 0; i < n; ++i) {
        Contour& c = io.contours[i];
        const float a2 = contour_signed_area2(c);
        const bool want_cw = (depth[i] & 1) == 0;
        if (want_cw) {
            if (a2 > 0.f) {
                reverse_contour(c);
            }
        } else {
            if (a2 < 0.f) {
                reverse_contour(c);
            }
        }
    }

    return !io.contours.empty();
}

}  // namespace text3d
