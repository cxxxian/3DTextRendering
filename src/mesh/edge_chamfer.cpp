/*
 * 平倒角：上下各一条斜面棱带。
 */

#include "mesh/edge_chamfer.h"

#include "mesh/mesh_geom.h"

#include <algorithm>
#include <cmath>

namespace text3d {

void ChamferProfile::append_caps(Mesh& out, const EdgeBuildContext& ctx) const {
    const float z_top = ctx.z_center + ctx.half;
    const float z_bot = ctx.z_center - ctx.half;
    // 调自由函数，避免与成员同名递归
    ::text3d::append_caps(out, ctx.cap_xy, ctx.cap_tris, z_top, z_bot, ctx.minx, ctx.miny, ctx.sx,
                          ctx.sy);
}

void ChamferProfile::append_rims(Mesh& out, const EdgeBuildContext& ctx) const {
    const float z_face_top = ctx.z_center + ctx.half;
    const float z_wall_top = ctx.z_center + (ctx.half - ctx.radius);
    const float z_wall_bot = ctx.z_center - (ctx.half - ctx.radius);
    const float z_face_bot = ctx.z_center - ctx.half;

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

            float nx = 0.f, ny = 0.f, nz = 0.f;
            face_normal_from_quad(a.x, a.y, z_face_top, b.x, b.y, z_face_top, A.x, A.y, z_wall_top,
                                  nx, ny, nz);
            append_band_quad(out, a.x, a.y, z_face_top, b.x, b.y, z_face_top, B.x, B.y, z_wall_top,
                             A.x, A.y, z_wall_top, u0, u1, 0.f, 1.f, nx, ny, nz);

            face_normal_from_quad(B.x, B.y, z_wall_bot, A.x, A.y, z_wall_bot, b.x, b.y, z_face_bot,
                                  nx, ny, nz);
            append_band_quad(out, B.x, B.y, z_wall_bot, A.x, A.y, z_wall_bot, a.x, a.y, z_face_bot,
                             b.x, b.y, z_face_bot, u1, u0, 0.f, 1.f, nx, ny, nz);
        }
    }
}

}  // namespace text3d
