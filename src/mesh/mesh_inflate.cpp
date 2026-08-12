/*
 * Cap Inflate：帽面细分 → d/d_max → 二次拱高；边界=帽面轮廓折线（外环+孔）。
 */

#include "mesh/mesh_inflate.h"

#include "mesh/mesh_geom.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace text3d {
namespace {

constexpr float kInflateFrac = 0.35f;
constexpr float kInflateCapFrac = 0.45f;
/* 先做固定轮次保证有内部点；再按「边中点真实拱高 vs 两端线性插值」误差加细 */
constexpr int kCapRefineBootstrap = 2;
constexpr int kCapRefineAdaptMax = 4;       // 额外自适应轮数上限（圆/环中轴弯时需要更多）
constexpr float kCapRefineErrFrac = 0.03f;  // 相对 H 的高度误差阈值

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

float dist_to_boundary(const GlyphOutline& outline, float x, float y) {
    float best = 1e30f;
    for (const Contour& c : outline.contours) {
        const size_t n = c.points.size();
        if (n < 2) {
            continue;
        }
        for (size_t i = 0; i < n; ++i) {
            const Vec2& a = c.points[i];
            const Vec2& b = c.points[(i + 1) % n];
            best = std::min(best, dist_point_segment(x, y, a.x, a.y, b.x, b.y));
        }
    }
    return best;
}

void accumulate_tri_normal(float ax, float ay, float az, float bx, float by, float bz, float cx,
                           float cy, float cz, float& nx, float& ny, float& nz) {
    const float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    const float e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    nx += e1y * e2z - e1z * e2y;
    ny += e1z * e2x - e1x * e2z;
    nz += e1x * e2y - e1y * e2x;
}

uint64_t edge_key(unsigned a, unsigned b) {
    const unsigned lo = std::min(a, b);
    const unsigned hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

unsigned midpoint_index(std::vector<float>& xy, std::map<uint64_t, unsigned>& mid_of_edge,
                        unsigned a, unsigned b) {
    const uint64_t key = edge_key(a, b);
    const auto it = mid_of_edge.find(key);
    if (it != mid_of_edge.end()) {
        return it->second;
    }
    const float ax = xy[a * 2], ay = xy[a * 2 + 1];
    const float bx = xy[b * 2], by = xy[b * 2 + 1];
    const unsigned mid = static_cast<unsigned>(xy.size() / 2);
    xy.push_back(0.5f * (ax + bx));
    xy.push_back(0.5f * (ay + by));
    mid_of_edge.emplace(key, mid);
    return mid;
}

/* 一轮：每三角拆 4（三边中点 + 中心三角）；共享边共用中点，不改轮廓拓扑 */
void refine_cap_mesh_once(std::vector<float>& xy, std::vector<unsigned int>& tris) {
    if (tris.size() < 3) {
        return;
    }
    std::map<uint64_t, unsigned> mid_of_edge;
    std::vector<unsigned int> out;
    out.reserve(tris.size() * 4);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const unsigned i0 = tris[t];
        const unsigned i1 = tris[t + 1];
        const unsigned i2 = tris[t + 2];
        const unsigned m01 = midpoint_index(xy, mid_of_edge, i0, i1);
        const unsigned m12 = midpoint_index(xy, mid_of_edge, i1, i2);
        const unsigned m20 = midpoint_index(xy, mid_of_edge, i2, i0);
        // 三角绕序保持与原三角一致
        out.push_back(i0);
        out.push_back(m01);
        out.push_back(m20);
        out.push_back(i1);
        out.push_back(m12);
        out.push_back(m01);
        out.push_back(i2);
        out.push_back(m20);
        out.push_back(m12);
        out.push_back(m01);
        out.push_back(m12);
        out.push_back(m20);
    }
    tris.swap(out);
}

void refine_cap_mesh(std::vector<float>& xy, std::vector<unsigned int>& tris, int rounds) {
    for (int r = 0; r < rounds; ++r) {
        refine_cap_mesh_once(xy, tris);
    }
}

/* h = H·(1-(1-t)^n)，n=4 冠部更钝，削弱中轴处二阶尖峰 */
constexpr int kArchFalloffPower = 4;

float arch_height_from_d(float d, float d_max, float H) {
    if (!(d_max > kMeshEps) || !(H > kMeshEps)) {
        return 0.f;
    }
    float t = d / d_max;
    t = std::max(0.f, std::min(1.f, t));
    const float u = 1.f - t;
    float u_pow = u * u;  // u^2
    u_pow *= u_pow;       // u^4
    // 若以后改 n，用循环累乘；当前固定 4
    static_assert(kArchFalloffPower == 4, "arch falloff hard-coded for n=4");
    return H * (1.f - u_pow);
}

/* 网格边用两端 h 线性插值，会削平真实拱高；误差大说明边穿过鼓包（环状中轴尤甚） */
float max_edge_height_error(const GlyphOutline& boundary, const std::vector<float>& xy,
                            const std::vector<unsigned int>& tris, float d_max, float H) {
    float max_err = 0.f;
    std::map<uint64_t, bool> seen;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const unsigned ids[3] = {tris[t], tris[t + 1], tris[t + 2]};
        for (int e = 0; e < 3; ++e) {
            const unsigned a = ids[e];
            const unsigned b = ids[(e + 1) % 3];
            const uint64_t key = edge_key(a, b);
            if (!seen.emplace(key, true).second) {
                continue;
            }
            const float ax = xy[a * 2], ay = xy[a * 2 + 1];
            const float bx = xy[b * 2], by = xy[b * 2 + 1];
            const float h0 = arch_height_from_d(dist_to_boundary(boundary, ax, ay), d_max, H);
            const float h1 = arch_height_from_d(dist_to_boundary(boundary, bx, by), d_max, H);
            const float mx = 0.5f * (ax + bx);
            const float my = 0.5f * (ay + by);
            const float h_mid =
                arch_height_from_d(dist_to_boundary(boundary, mx, my), d_max, H);
            const float h_lerp = 0.5f * (h0 + h1);
            max_err = std::max(max_err, std::fabs(h_mid - h_lerp));
        }
    }
    return max_err;
}

/* 三角面片平均值 vs 重心真实拱高：全落在轮廓上却盖住笔画内部时，误差很大 */
float max_tri_undershoot(const GlyphOutline& boundary, const std::vector<float>& xy,
                         const std::vector<unsigned int>& tris, float d_max, float H) {
    float max_err = 0.f;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const unsigned i0 = tris[t], i1 = tris[t + 1], i2 = tris[t + 2];
        const float x0 = xy[i0 * 2], y0 = xy[i0 * 2 + 1];
        const float x1 = xy[i1 * 2], y1 = xy[i1 * 2 + 1];
        const float x2 = xy[i2 * 2], y2 = xy[i2 * 2 + 1];
        const float h0 = arch_height_from_d(dist_to_boundary(boundary, x0, y0), d_max, H);
        const float h1 = arch_height_from_d(dist_to_boundary(boundary, x1, y1), d_max, H);
        const float h2 = arch_height_from_d(dist_to_boundary(boundary, x2, y2), d_max, H);
        const float cx = (x0 + x1 + x2) * (1.f / 3.f);
        const float cy = (y0 + y1 + y2) * (1.f / 3.f);
        const float h_c = arch_height_from_d(dist_to_boundary(boundary, cx, cy), d_max, H);
        const float h_avg = (h0 + h1 + h2) * (1.f / 3.f);
        max_err = std::max(max_err, h_c - h_avg);  // 只关心「面比真实场矮」的谷
    }
    return max_err;
}

float compute_d_max(const GlyphOutline& boundary, const std::vector<float>& xy) {
    float d_max = 0.f;
    const int n = static_cast<int>(xy.size() / 2);
    for (int i = 0; i < n; ++i) {
        d_max = std::max(d_max, dist_to_boundary(boundary, xy[i * 2], xy[i * 2 + 1]));
    }
    return d_max;
}

/*
 * 固定细分拿内部点，再按高度场逼近误差加密。
 * 直笔画中轴直、误差小，往往停在 bootstrap；O/环中轴弯，边弦切鼓包，会多加几轮。
 */
void refine_cap_mesh_for_inflate(std::vector<float>& xy, std::vector<unsigned int>& tris,
                                 const GlyphOutline& boundary, float H) {
    refine_cap_mesh(xy, tris, kCapRefineBootstrap);
    for (int r = 0; r < kCapRefineAdaptMax; ++r) {
        const float d_max = compute_d_max(boundary, xy);
        if (!(d_max > kMeshEps)) {
            return;
        }
        const float tol = std::max(H * kCapRefineErrFrac, d_max * 1e-4f);
        const float edge_err = max_edge_height_error(boundary, xy, tris, d_max, H);
        const float tri_err = max_tri_undershoot(boundary, xy, tris, d_max, H);
        if (edge_err <= tol && tri_err <= tol) {
            return;
        }
        refine_cap_mesh_once(xy, tris);
    }
}

}  // namespace

float inflate_height_from_strength(float strength_01, float depth) {
    if (!(strength_01 > 0.f) || !(depth > 0.f)) {
        return 0.f;
    }
    const float t = std::min(1.f, strength_01);
    const float h = t * kInflateFrac * depth;
    return std::min(h, kInflateCapFrac * depth);
}

bool append_inflated_caps(Mesh& out, const GlyphOutline& boundary, const std::vector<float>& xy,
                          const std::vector<unsigned int>& tris, float z_top, float z_bot, float H,
                          float minx, float miny, float sx, float sy) {
    if (!(H > kMeshEps)) {
        append_caps(out, xy, tris, z_top, z_bot, minx, miny, sx, sy);
        return false;
    }

    // 细分在 inflate 入口统一做：直边 / Bevel / Fillet 都走这里
    std::vector<float> refined_xy = xy;
    std::vector<unsigned int> refined_tris = tris;
    refine_cap_mesh_for_inflate(refined_xy, refined_tris, boundary, H);

    const int vert_count = static_cast<int>(refined_xy.size() / 2);
    if (vert_count <= 0) {
        return false;
    }

    std::vector<float> heights(static_cast<size_t>(vert_count), 0.f);
    float d_max = 0.f;
    for (int i = 0; i < vert_count; ++i) {
        const float x = refined_xy[static_cast<size_t>(i) * 2];
        const float y = refined_xy[static_cast<size_t>(i) * 2 + 1];
        const float d = dist_to_boundary(boundary, x, y);
        heights[static_cast<size_t>(i)] = d;
        d_max = std::max(d_max, d);
    }
    if (!(d_max > kMeshEps)) {
        // 细分后仍无内部点（极端退化）→ 无法鼓包，退回平面（用原始未细分网格）
        append_caps(out, xy, tris, z_top, z_bot, minx, miny, sx, sy);
        return false;
    }

    for (float& d : heights) {
        d = arch_height_from_d(d, d_max, H);
    }

    auto push_cap = [&](bool top) {
        const std::size_t part_begin = out.indices.size();
        const unsigned int base = static_cast<unsigned>(out.vertices.size());
        std::vector<float> nxv(static_cast<size_t>(vert_count), 0.f);
        std::vector<float> nyv(static_cast<size_t>(vert_count), 0.f);
        std::vector<float> nzv(static_cast<size_t>(vert_count), 0.f);

        for (size_t t = 0; t + 2 < refined_tris.size(); t += 3) {
            const unsigned i0 = refined_tris[t], i1 = refined_tris[t + 1], i2 = refined_tris[t + 2];
            const float x0 = refined_xy[i0 * 2], y0 = refined_xy[i0 * 2 + 1];
            const float x1 = refined_xy[i1 * 2], y1 = refined_xy[i1 * 2 + 1];
            const float x2 = refined_xy[i2 * 2], y2 = refined_xy[i2 * 2 + 1];
            if (top) {
                const float z0 = z_top + heights[i0];
                const float z1 = z_top + heights[i1];
                const float z2 = z_top + heights[i2];
                float nx = 0.f, ny = 0.f, nz = 0.f;
                accumulate_tri_normal(x0, y0, z0, x1, y1, z1, x2, y2, z2, nx, ny, nz);
                nxv[i0] += nx;
                nyv[i0] += ny;
                nzv[i0] += nz;
                nxv[i1] += nx;
                nyv[i1] += ny;
                nzv[i1] += nz;
                nxv[i2] += nx;
                nyv[i2] += ny;
                nzv[i2] += nz;
            } else {
                const float z0 = z_bot - heights[i0];
                const float z1 = z_bot - heights[i1];
                const float z2 = z_bot - heights[i2];
                float nx = 0.f, ny = 0.f, nz = 0.f;
                accumulate_tri_normal(x0, y0, z0, x2, y2, z2, x1, y1, z1, nx, ny, nz);
                nxv[i0] += nx;
                nyv[i0] += ny;
                nzv[i0] += nz;
                nxv[i1] += nx;
                nyv[i1] += ny;
                nzv[i1] += nz;
                nxv[i2] += nx;
                nyv[i2] += ny;
                nzv[i2] += nz;
            }
        }

        for (int i = 0; i < vert_count; ++i) {
            float nx = nxv[static_cast<size_t>(i)];
            float ny = nyv[static_cast<size_t>(i)];
            float nz = nzv[static_cast<size_t>(i)];
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nx = ny = 0.f;
                nz = top ? 1.f : -1.f;
            }
            const float x = refined_xy[static_cast<size_t>(i) * 2];
            const float y = refined_xy[static_cast<size_t>(i) * 2 + 1];
            float u = 0.f, v = 0.f;
            planar_uv(x, y, minx, miny, sx, sy, u, v);
            const float z = top ? (z_top + heights[static_cast<size_t>(i)])
                                : (z_bot - heights[static_cast<size_t>(i)]);
            push_vertex(out, x, y, z, nx, ny, nz, u, top ? v : (1.f - v));
        }
        if (top) {
            for (size_t t = 0; t + 2 < refined_tris.size(); t += 3) {
                out.indices.push_back(base + refined_tris[t]);
                out.indices.push_back(base + refined_tris[t + 1]);
                out.indices.push_back(base + refined_tris[t + 2]);
            }
            out.set_part(MeshPart::Front, part_begin, out.indices.size());
        } else {
            for (size_t t = 0; t + 2 < refined_tris.size(); t += 3) {
                out.indices.push_back(base + refined_tris[t]);
                out.indices.push_back(base + refined_tris[t + 2]);
                out.indices.push_back(base + refined_tris[t + 1]);
            }
            out.set_part(MeshPart::Back, part_begin, out.indices.size());
        }
    };

    push_cap(true);
    push_cap(false);
    return true;
}

}  // namespace text3d
