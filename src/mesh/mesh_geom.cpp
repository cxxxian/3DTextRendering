/*
 * 挤出共用几何：法线、UV、条带、平面 cap、直墙与外墙。
 */

#include "mesh/mesh_geom.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace text3d {

void push_vertex(Mesh& m, float x, float y, float z, float nx, float ny, float nz, float u,
                 float v) {
    m.vertices.push_back({x, y, z, nx, ny, nz, u, v});
}

void side_normal(float x0, float y0, float x1, float y1, float& nx, float& ny) {
    const float ex = x1 - x0;
    const float ey = y1 - y0;
    nx = ey;
    ny = -ex;
    const float len = std::sqrt(nx * nx + ny * ny);
    if (len > 1e-8f) {
        nx /= len;
        ny /= len;
    } else {
        nx = 0.f;
        ny = 0.f;
    }
}

void outline_bounds(const GlyphOutline& outline, float& minx, float& miny, float& maxx,
                    float& maxy) {
    minx = miny = 1e30f;
    maxx = maxy = -1e30f;
    for (const Contour& c : outline.contours) {
        for (const Vec2& p : c.points) {
            minx = std::min(minx, p.x);
            miny = std::min(miny, p.y);
            maxx = std::max(maxx, p.x);
            maxy = std::max(maxy, p.y);
        }
    }
}

void planar_uv(float x, float y, float minx, float miny, float sx, float sy, float& u, float& v) {
    u = (x - minx) / sx;
    v = (y - miny) / sy;
}

float contour_perimeter(const Contour& c) {
    const size_t n = c.points.size();
    if (n < 2) {
        return 0.f;
    }
    float peri = 0.f;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = c.points[i];
        const Vec2& b = c.points[(i + 1) % n];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        peri += std::sqrt(dx * dx + dy * dy);
    }
    return peri;
}

void append_band_quad(Mesh& out, float ax, float ay, float az, float bx, float by, float bz,
                      float cx, float cy, float cz, float dx, float dy, float dz, float u0,
                      float u1, float v0, float v1, float nx, float ny, float nz) {
    const unsigned int base = static_cast<unsigned>(out.vertices.size());
    push_vertex(out, ax, ay, az, nx, ny, nz, u0, v0);
    push_vertex(out, bx, by, bz, nx, ny, nz, u1, v0);
    push_vertex(out, cx, cy, cz, nx, ny, nz, u1, v1);
    push_vertex(out, dx, dy, dz, nx, ny, nz, u0, v1);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 3);
}

void face_normal_from_quad(float ax, float ay, float az, float bx, float by, float bz, float dx,
                           float dy, float dz, float& nx, float& ny, float& nz) {
    const float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    const float e2x = dx - ax, e2y = dy - ay, e2z = dz - az;
    nx = e1y * e2z - e1z * e2y;
    ny = e1z * e2x - e1x * e2z;
    nz = e1x * e2y - e1y * e2x;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-8f) {
        nx /= len;
        ny /= len;
        nz /= len;
    } else {
        nx = ny = 0.f;
        nz = 1.f;
    }
}

void append_caps(Mesh& out, const std::vector<float>& xy, const std::vector<unsigned int>& tris,
                 float z_top, float z_bot, float minx, float miny, float sx, float sy) {
    const std::size_t front_begin = out.indices.size();
    const unsigned int top_base = static_cast<unsigned>(out.vertices.size());
    const int vert_count = static_cast<int>(xy.size() / 2);
    for (int i = 0; i < vert_count; ++i) {
        const float x = xy[static_cast<size_t>(i) * 2];
        const float y = xy[static_cast<size_t>(i) * 2 + 1];
        float u = 0.f, v = 0.f;
        planar_uv(x, y, minx, miny, sx, sy, u, v);
        push_vertex(out, x, y, z_top, 0.f, 0.f, 1.f, u, v);
    }
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        out.indices.push_back(top_base + tris[t]);
        out.indices.push_back(top_base + tris[t + 1]);
        out.indices.push_back(top_base + tris[t + 2]);
    }
    out.set_part(MeshPart::Front, front_begin, out.indices.size());

    const std::size_t back_begin = out.indices.size();
    const unsigned int bot_base = static_cast<unsigned>(out.vertices.size());
    for (int i = 0; i < vert_count; ++i) {
        const float x = xy[static_cast<size_t>(i) * 2];
        const float y = xy[static_cast<size_t>(i) * 2 + 1];
        float u = 0.f, v = 0.f;
        planar_uv(x, y, minx, miny, sx, sy, u, v);
        push_vertex(out, x, y, z_bot, 0.f, 0.f, -1.f, u, 1.f - v);
    }
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        out.indices.push_back(bot_base + tris[t]);
        out.indices.push_back(bot_base + tris[t + 2]);
        out.indices.push_back(bot_base + tris[t + 1]);
    }
    out.set_part(MeshPart::Back, back_begin, out.indices.size());
}

void append_straight_sides(Mesh& out, const GlyphOutline& outline, float z_top, float z_bot) {
    const std::size_t side_begin = out.indices.size();
    for (const Contour& c : outline.contours) {
        const size_t n = c.points.size();
        if (n < 2) {
            continue;
        }
        const float peri = std::max(contour_perimeter(c), kMeshEps);
        float acc = 0.f;
        for (size_t i = 0; i < n; ++i) {
            const Vec2& a = c.points[i];
            const Vec2& b = c.points[(i + 1) % n];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float edge_len = std::sqrt(dx * dx + dy * dy);
            const float u0 = acc / peri;
            const float u1 = (acc + edge_len) / peri;
            acc += edge_len;

            float nx = 0.f, ny = 0.f;
            side_normal(a.x, a.y, b.x, b.y, nx, ny);
            // side_normal 指向内侧，侧面朝外光照需取反
            append_band_quad(out, a.x, a.y, z_top, b.x, b.y, z_top, b.x, b.y, z_bot, a.x, a.y,
                             z_bot, u0, u1, 0.f, 1.f, -nx, -ny, 0.f);
        }
    }
    if (out.indices.size() > side_begin) {
        out.set_part(MeshPart::Side, side_begin, out.indices.size());
    }
}

void append_outer_walls(Mesh& out, const GlyphOutline& outer, float z_wall_top, float z_wall_bot) {
    if (std::fabs(z_wall_top - z_wall_bot) < 0.05f) {
        return;
    }
    const std::size_t side_begin = out.indices.size();
    for (const Contour& c : outer.contours) {
        const size_t n = c.points.size();
        if (n < 2) {
            continue;
        }
        const float peri = std::max(contour_perimeter(c), kMeshEps);
        float acc = 0.f;
        for (size_t i = 0; i < n; ++i) {
            const Vec2& A = c.points[i];
            const Vec2& B = c.points[(i + 1) % n];
            const float dx = B.x - A.x;
            const float dy = B.y - A.y;
            const float edge_len = std::sqrt(dx * dx + dy * dy);
            const float u0 = acc / peri;
            const float u1 = (acc + edge_len) / peri;
            acc += edge_len;

            float wx = 0.f, wy = 0.f;
            side_normal(A.x, A.y, B.x, B.y, wx, wy);
            append_band_quad(out, A.x, A.y, z_wall_top, B.x, B.y, z_wall_top, B.x, B.y, z_wall_bot,
                             A.x, A.y, z_wall_bot, u0, u1, 0.f, 1.f, -wx, -wy, 0.f);
        }
    }
    if (out.indices.size() > side_begin) {
        out.set_part(MeshPart::Side, side_begin, out.indices.size());
    }
}

}  // namespace text3d
