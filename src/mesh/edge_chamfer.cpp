/*
 * 平倒角：上下各一条斜面棱带。
 * 棱带 UV 用与帽面相同的 planar_uv(xy)，接缝处与 Front/Back 对齐。
 */

#include "mesh/edge_chamfer.h"

#include "mesh/mesh_geom.h"
#include "mesh/mesh_inflate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace text3d {
namespace {

void push_rim_quad_planar(Mesh& out, const EdgeBuildContext& ctx, float ax, float ay, float az,
                          float bx, float by, float bz, float cx, float cy, float cz, float dx,
                          float dy, float dz, float nx, float ny, float nz, bool flip_v) {
    float ua = 0.f, va = 0.f, ub = 0.f, vb = 0.f, uc = 0.f, vc = 0.f, ud = 0.f, vd = 0.f;
    planar_uv(ax, ay, ctx.minx, ctx.miny, ctx.sx, ctx.sy, ua, va);
    planar_uv(bx, by, ctx.minx, ctx.miny, ctx.sx, ctx.sy, ub, vb);
    planar_uv(cx, cy, ctx.minx, ctx.miny, ctx.sx, ctx.sy, uc, vc);
    planar_uv(dx, dy, ctx.minx, ctx.miny, ctx.sx, ctx.sy, ud, vd);
    if (flip_v) {
        va = 1.f - va;
        vb = 1.f - vb;
        vc = 1.f - vc;
        vd = 1.f - vd;
    }
    const unsigned int base = static_cast<unsigned>(out.vertices.size());
    push_vertex(out, ax, ay, az, nx, ny, nz, ua, va);
    push_vertex(out, bx, by, bz, nx, ny, nz, ub, vb);
    push_vertex(out, cx, cy, cz, nx, ny, nz, uc, vc);
    push_vertex(out, dx, dy, dz, nx, ny, nz, ud, vd);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 3);
}

}  // namespace

bool ChamferProfile::append_caps(Mesh& out, const EdgeBuildContext& ctx) const {
    const float z_top = ctx.z_center + ctx.half;
    const float z_bot = ctx.z_center - ctx.half;
    return append_inflated_caps(out, ctx.inner, ctx.cap_xy, ctx.cap_tris, z_top, z_bot,
                                ctx.inflate_h, ctx.minx, ctx.miny, ctx.sx, ctx.sy);
}

void ChamferProfile::append_rims(Mesh& out, const EdgeBuildContext& ctx) const {
    const float z_face_top = ctx.z_center + ctx.half;
    const float z_wall_top = ctx.z_center + (ctx.half - ctx.radius);
    const float z_wall_bot = ctx.z_center - (ctx.half - ctx.radius);
    const float z_face_bot = ctx.z_center - ctx.half;

    const std::size_t bevel_begin = out.indices.size();
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

            float nx = 0.f, ny = 0.f, nz = 0.f;
            face_normal_from_quad(a.x, a.y, z_face_top, b.x, b.y, z_face_top, A.x, A.y, z_wall_top,
                                  nx, ny, nz);
            push_rim_quad_planar(out, ctx, a.x, a.y, z_face_top, b.x, b.y, z_face_top, B.x, B.y,
                                 z_wall_top, A.x, A.y, z_wall_top, nx, ny, nz, false);

            face_normal_from_quad(B.x, B.y, z_wall_bot, A.x, A.y, z_wall_bot, b.x, b.y, z_face_bot,
                                  nx, ny, nz);
            push_rim_quad_planar(out, ctx, B.x, B.y, z_wall_bot, A.x, A.y, z_wall_bot, a.x, a.y,
                                 z_face_bot, b.x, b.y, z_face_bot, nx, ny, nz, true);
        }
    }
    if (out.indices.size() > bevel_begin) {
        out.set_part(MeshPart::Bevel, bevel_begin, out.indices.size());
    }
}

}  // namespace text3d
