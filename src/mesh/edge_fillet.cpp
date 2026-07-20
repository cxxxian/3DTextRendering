/*
 * 真圆角棱带 + 正面 d/d_max 轻拱。
 */

#include "mesh/edge_fillet.h"

#include "mesh/mesh_geom.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace text3d {
namespace {

constexpr int kFilletArcSeg = 8;         // 1/4 圆分段数
constexpr float kFaceDomeFrac = 0.35f;  // 拱高 ≈ 0.35 * R

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

/* 正面轻拱：t=d/d_max，h=H*(1-(1-t)^2)；边上天 h=0 接圆角起点 */
void append_soft_caps(Mesh& out, const GlyphOutline& cap_outline, const std::vector<float>& xy,
                      const std::vector<unsigned int>& tris, float z_face_top, float z_face_bot,
                      float H, float minx, float miny, float sx, float sy) {
    const int vert_count = static_cast<int>(xy.size() / 2);
    std::vector<float> heights(static_cast<size_t>(vert_count), 0.f);
    float d_max = 0.f;
    for (int i = 0; i < vert_count; ++i) {
        const float x = xy[static_cast<size_t>(i) * 2];
        const float y = xy[static_cast<size_t>(i) * 2 + 1];
        const float d = dist_to_boundary(cap_outline, x, y);
        heights[static_cast<size_t>(i)] = d;
        d_max = std::max(d_max, d);
    }
    if (d_max < kMeshEps || H <= kMeshEps) {
        ::text3d::append_caps(out, xy, tris, z_face_top, z_face_bot, minx, miny, sx, sy);
        return;
    }
    for (float& d : heights) {
        float t = d / d_max;
        t = std::max(0.f, std::min(1.f, t));
        const float u = 1.f - t;
        d = H * (1.f - u * u);
    }

    auto push_cap = [&](bool top) {
        const unsigned int base = static_cast<unsigned>(out.vertices.size());
        std::vector<float> nxv(static_cast<size_t>(vert_count), 0.f);
        std::vector<float> nyv(static_cast<size_t>(vert_count), 0.f);
        std::vector<float> nzv(static_cast<size_t>(vert_count), 0.f);

        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            const unsigned i0 = tris[t], i1 = tris[t + 1], i2 = tris[t + 2];
            const float x0 = xy[i0 * 2], y0 = xy[i0 * 2 + 1];
            const float x1 = xy[i1 * 2], y1 = xy[i1 * 2 + 1];
            const float x2 = xy[i2 * 2], y2 = xy[i2 * 2 + 1];
            if (top) {
                const float z0 = z_face_top + heights[i0];
                const float z1 = z_face_top + heights[i1];
                const float z2 = z_face_top + heights[i2];
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
                const float z0 = z_face_bot - heights[i0];
                const float z1 = z_face_bot - heights[i1];
                const float z2 = z_face_bot - heights[i2];
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
            const float x = xy[static_cast<size_t>(i) * 2];
            const float y = xy[static_cast<size_t>(i) * 2 + 1];
            float u = 0.f, v = 0.f;
            planar_uv(x, y, minx, miny, sx, sy, u, v);
            const float z = top ? (z_face_top + heights[static_cast<size_t>(i)])
                                : (z_face_bot - heights[static_cast<size_t>(i)]);
            push_vertex(out, x, y, z, nx, ny, nz, u, top ? v : (1.f - v));
        }
        if (top) {
            for (size_t t = 0; t + 2 < tris.size(); t += 3) {
                out.indices.push_back(base + tris[t]);
                out.indices.push_back(base + tris[t + 1]);
                out.indices.push_back(base + tris[t + 2]);
            }
        } else {
            for (size_t t = 0; t + 2 < tris.size(); t += 3) {
                out.indices.push_back(base + tris[t]);
                out.indices.push_back(base + tris[t + 2]);
                out.indices.push_back(base + tris[t + 1]);
            }
        }
    };

    push_cap(true);
    push_cap(false);
}

}  // namespace

void FilletProfile::append_caps(Mesh& out, const EdgeBuildContext& ctx) const {
    const float z_face_top = ctx.z_center + ctx.half;
    const float z_face_bot = ctx.z_center - ctx.half;
    const float dome_H = kFaceDomeFrac * ctx.radius;
    append_soft_caps(out, ctx.inner, ctx.cap_xy, ctx.cap_tris, z_face_top, z_face_bot, dome_H,
                     ctx.minx, ctx.miny, ctx.sx, ctx.sy);
}

/* θ: 0=内面边缘 → π/2=外墙；法线 = 圆心(内点) → 表面点 */
void FilletProfile::append_rims(Mesh& out, const EdgeBuildContext& ctx) const {
    int arc_seg = kFilletArcSeg;
    if (arc_seg < 2) {
        arc_seg = 2;
    }
    const float R = ctx.radius;
    const float half = ctx.half;
    const float z_center = ctx.z_center;
    const float pi_half = 1.57079632679f;

    const size_t nc = std::min(ctx.outer.contours.size(), ctx.inner.contours.size());
    for (size_t ci = 0; ci < nc; ++ci) {
        const Contour& o = ctx.outer.contours[ci];
        const Contour& in = ctx.inner.contours[ci];
        const size_t n = std::min(o.points.size(), in.points.size());
        if (n < 2) {
            continue;
        }
        const float peri = std::max(contour_perimeter(o), kMeshEps);
        float acc = 0.f;
        for (size_t i = 0; i < n; ++i) {
            const Vec2& A = o.points[i];
            const Vec2& B = o.points[(i + 1) % n];
            const Vec2& a = in.points[i];
            const Vec2& b = in.points[(i + 1) % n];

            const float edx = B.x - A.x;
            const float edy = B.y - A.y;
            const float edge_len = std::sqrt(edx * edx + edy * edy);
            const float u0 = acc / peri;
            const float u1 = (acc + edge_len) / peri;
            acc += edge_len;

            auto sample_top = [&](float th, float& x0, float& y0, float& z0, float& x1, float& y1,
                                  float& z1, float& nx0, float& ny0, float& nz0, float& nx1,
                                  float& ny1, float& nz1) {
                const float s = std::sin(th);
                const float c = std::cos(th);
                x0 = a.x + (A.x - a.x) * s;
                y0 = a.y + (A.y - a.y) * s;
                z0 = z_center + (half - R) + R * c;
                x1 = b.x + (B.x - b.x) * s;
                y1 = b.y + (B.y - b.y) * s;
                z1 = z0;
                nx0 = x0 - a.x;
                ny0 = y0 - a.y;
                nz0 = z0 - (z_center + (half - R));
                nx1 = x1 - b.x;
                ny1 = y1 - b.y;
                nz1 = z1 - (z_center + (half - R));
                const float l0 = std::sqrt(nx0 * nx0 + ny0 * ny0 + nz0 * nz0);
                const float l1 = std::sqrt(nx1 * nx1 + ny1 * ny1 + nz1 * nz1);
                if (l0 > 1e-8f) {
                    nx0 /= l0;
                    ny0 /= l0;
                    nz0 /= l0;
                }
                if (l1 > 1e-8f) {
                    nx1 /= l1;
                    ny1 /= l1;
                    nz1 /= l1;
                }
            };

            for (int seg = 0; seg < arc_seg; ++seg) {
                const float t0 = static_cast<float>(seg) / static_cast<float>(arc_seg);
                const float t1 = static_cast<float>(seg + 1) / static_cast<float>(arc_seg);
                float ax, ay, az, bx, by, bz, cx, cy, cz, dxp, dyp, dzp;
                float nax, nay, naz, nbx, nby, nbz, ncx, ncy, ncz, ndx, ndy, ndz;
                sample_top(pi_half * t0, ax, ay, az, bx, by, bz, nax, nay, naz, nbx, nby, nbz);
                sample_top(pi_half * t1, dxp, dyp, dzp, cx, cy, cz, ndx, ndy, ndz, ncx, ncy, ncz);

                const unsigned base = static_cast<unsigned>(out.vertices.size());
                push_vertex(out, ax, ay, az, nax, nay, naz, u0, t0);
                push_vertex(out, bx, by, bz, nbx, nby, nbz, u1, t0);
                push_vertex(out, cx, cy, cz, ncx, ncy, ncz, u1, t1);
                push_vertex(out, dxp, dyp, dzp, ndx, ndy, ndz, u0, t1);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 1);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 3);
            }

            auto sample_bot = [&](float th, float& x0, float& y0, float& z0, float& x1, float& y1,
                                  float& z1, float& nx0, float& ny0, float& nz0, float& nx1,
                                  float& ny1, float& nz1) {
                const float s = std::sin(th);
                const float c = std::cos(th);
                x0 = a.x + (A.x - a.x) * s;
                y0 = a.y + (A.y - a.y) * s;
                z0 = z_center - (half - R) - R * c;
                x1 = b.x + (B.x - b.x) * s;
                y1 = b.y + (B.y - b.y) * s;
                z1 = z0;
                nx0 = x0 - a.x;
                ny0 = y0 - a.y;
                nz0 = z0 - (z_center - (half - R));
                nx1 = x1 - b.x;
                ny1 = y1 - b.y;
                nz1 = z1 - (z_center - (half - R));
                const float l0 = std::sqrt(nx0 * nx0 + ny0 * ny0 + nz0 * nz0);
                const float l1 = std::sqrt(nx1 * nx1 + ny1 * ny1 + nz1 * nz1);
                if (l0 > 1e-8f) {
                    nx0 /= l0;
                    ny0 /= l0;
                    nz0 /= l0;
                }
                if (l1 > 1e-8f) {
                    nx1 /= l1;
                    ny1 /= l1;
                    nz1 /= l1;
                }
            };

            for (int seg = 0; seg < arc_seg; ++seg) {
                const float t0 = static_cast<float>(seg) / static_cast<float>(arc_seg);
                const float t1 = static_cast<float>(seg + 1) / static_cast<float>(arc_seg);
                float ax, ay, az, bx, by, bz, cx, cy, cz, dxp, dyp, dzp;
                float nax, nay, naz, nbx, nby, nbz, ncx, ncy, ncz, ndx, ndy, ndz;
                sample_bot(pi_half * t0, ax, ay, az, bx, by, bz, nax, nay, naz, nbx, nby, nbz);
                sample_bot(pi_half * t1, dxp, dyp, dzp, cx, cy, cz, ndx, ndy, ndz, ncx, ncy, ncz);

                const unsigned base = static_cast<unsigned>(out.vertices.size());
                push_vertex(out, bx, by, bz, nbx, nby, nbz, u1, t0);
                push_vertex(out, ax, ay, az, nax, nay, naz, u0, t0);
                push_vertex(out, dxp, dyp, dzp, ndx, ndy, ndz, u0, t1);
                push_vertex(out, cx, cy, cz, ncx, ncy, ncz, u1, t1);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 1);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 3);
            }
        }
    }
}

}  // namespace text3d
