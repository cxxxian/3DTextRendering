/*
 * 真圆角棱带；帽面只跟 Inflate（无 fillet 自带轻拱）。
 * 棱带 UV 用 planar_uv(xy)，与 Front/Back 同一套；底面 V 翻转对齐 Back。
 */

#include "mesh/edge_fillet.h"

#include "mesh/mesh_geom.h"
#include "mesh/mesh_inflate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace text3d {
namespace {

constexpr int kFilletArcSeg = 8;  // 1/4 圆分段数

void rim_uv(const EdgeBuildContext& ctx, float x, float y, bool flip_v, float& u, float& v) {
    planar_uv(x, y, ctx.minx, ctx.miny, ctx.sx, ctx.sy, u, v);
    if (flip_v) {
        v = 1.f - v;
    }
}

}  // namespace

bool FilletProfile::append_caps(Mesh& out, const EdgeBuildContext& ctx) const {
    const float z_face_top = ctx.z_center + ctx.half;
    const float z_face_bot = ctx.z_center - ctx.half;
    return append_inflated_caps(out, ctx.inner, ctx.cap_xy, ctx.cap_tris, z_face_top, z_face_bot,
                                ctx.inflate_h, ctx.minx, ctx.miny, ctx.sx, ctx.sy);
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

    const std::size_t rounded_begin = out.indices.size();
    const size_t nc = std::min(ctx.outer.contours.size(), ctx.inner.contours.size());
    for (size_t ci = 0; ci < nc; ++ci) {
        const Contour& o = ctx.outer.contours[ci];
        const Contour& in = ctx.inner.contours[ci];
        const size_t n = std::min(o.points.size(), in.points.size());
        if (n < 2) {
            continue;
        }
        for (size_t i = 0; i < n; ++i) {
            const Vec2& A = o.points[i];
            const Vec2& B = o.points[(i + 1) % n];
            const Vec2& a = in.points[i];
            const Vec2& b = in.points[(i + 1) % n];

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

                float ua, va, ub, vb, uc, vc, ud, vd;
                rim_uv(ctx, ax, ay, false, ua, va);
                rim_uv(ctx, bx, by, false, ub, vb);
                rim_uv(ctx, cx, cy, false, uc, vc);
                rim_uv(ctx, dxp, dyp, false, ud, vd);

                const unsigned base = static_cast<unsigned>(out.vertices.size());
                push_vertex(out, ax, ay, az, nax, nay, naz, ua, va);
                push_vertex(out, bx, by, bz, nbx, nby, nbz, ub, vb);
                push_vertex(out, cx, cy, cz, ncx, ncy, ncz, uc, vc);
                push_vertex(out, dxp, dyp, dzp, ndx, ndy, ndz, ud, vd);
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

                float ua, va, ub, vb, uc, vc, ud, vd;
                rim_uv(ctx, ax, ay, true, ua, va);
                rim_uv(ctx, bx, by, true, ub, vb);
                rim_uv(ctx, cx, cy, true, uc, vc);
                rim_uv(ctx, dxp, dyp, true, ud, vd);

                const unsigned base = static_cast<unsigned>(out.vertices.size());
                push_vertex(out, bx, by, bz, nbx, nby, nbz, ub, vb);
                push_vertex(out, ax, ay, az, nax, nay, naz, ua, va);
                push_vertex(out, dxp, dyp, dzp, ndx, ndy, ndz, ud, vd);
                push_vertex(out, cx, cy, cz, ncx, ncy, ncz, uc, vc);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 1);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 0);
                out.indices.push_back(base + 2);
                out.indices.push_back(base + 3);
            }
        }
    }
    if (out.indices.size() > rounded_begin) {
        out.set_part(MeshPart::Rounded, rounded_begin, out.indices.size());
    }
}

}  // namespace text3d
