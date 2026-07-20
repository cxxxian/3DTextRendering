/*
 * 挤出编排：选直边或倒角/圆角路径，调 geom / offset / edge 策略。
 */

#include "mesh/mesh_extrude.h"

#include "mesh/edge_chamfer.h"
#include "mesh/edge_fillet.h"
#include "mesh/mesh_geom.h"
#include "mesh/mesh_offset_tess.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace text3d {

bool build_extruded_mesh(const GlyphOutline& outline, const ExtrudeOptions& opt, Mesh& out) {
    out = Mesh{};
    if (outline.contours.empty()) {
        return false;
    }

    float minx = 0.f, miny = 0.f, maxx = 0.f, maxy = 0.f;
    outline_bounds(outline, minx, miny, maxx, maxy);
    const float sx = std::max(maxx - minx, kMeshEps);
    const float sy = std::max(maxy - miny, kMeshEps);

    const float half = opt.depth * 0.5f;
    // fillet 优先；二者都变成内缩半径 R
    float fillet = clamp_edge_radius(opt.fillet, opt.depth);
    float bevel = (fillet > kMeshEps) ? 0.f : clamp_edge_radius(opt.bevel, opt.depth);
    float radius = (fillet > kMeshEps) ? fillet : bevel;
    const bool use_fillet = fillet > kMeshEps;

    GlyphOutline inner;
    std::vector<float> cap_xy;
    std::vector<unsigned int> cap_tris;

    if (radius > kMeshEps) {
        radius = resolve_inset_radius(outline, radius, inner, cap_xy, cap_tris);
        if (use_fillet) {
            fillet = radius;
        } else {
            bevel = radius;
        }
    }

    if (radius <= kMeshEps) {
        if (!try_tessellate(outline, cap_xy, cap_tris)) {
            std::cerr << "[mesh_extrude] tessTesselate 失败（检查轮廓是否自交/退化）\n";
            return false;
        }
        const float z_top = opt.z_center + half;
        const float z_bot = opt.z_center - half;
        append_caps(out, cap_xy, cap_tris, z_top, z_bot, minx, miny, sx, sy);
        append_straight_sides(out, outline, z_top, z_bot);
    } else {
        EdgeBuildContext ctx{outline,     inner,     cap_xy,          cap_tris, opt.z_center,
                             half,        radius,    minx,            miny,     sx,
                             sy};
        std::unique_ptr<IEdgeProfile> profile;
        if (use_fillet) {
            profile = std::make_unique<FilletProfile>();
        } else {
            profile = std::make_unique<ChamferProfile>();
        }
        profile->append_caps(out, ctx);
        profile->append_rims(out, ctx);
        const float z_wall_top = opt.z_center + (half - radius);
        const float z_wall_bot = opt.z_center - (half - radius);
        append_outer_walls(out, outline, z_wall_top, z_wall_bot);
    }

    std::cout << "[mesh_extrude] vertices=" << out.vertices.size()
              << " indices=" << out.indices.size() << " depth=" << opt.depth
              << " bevel=" << bevel << " fillet=" << fillet << "\n";
    return !out.indices.empty();
}

void translate_mesh(Mesh& mesh, float dx, float dy, float dz) {
    for (Vertex& v : mesh.vertices) {
        v.px += dx;
        v.py += dy;
        v.pz += dz;
    }
}

void scale_mesh(Mesh& mesh, float s) {
    for (Vertex& v : mesh.vertices) {
        v.px *= s;
        v.py *= s;
        v.pz *= s;
    }
}

Mesh merge_meshes(const std::vector<Mesh>& parts) {
    Mesh out;
    for (const Mesh& part : parts) {
        const unsigned int base = static_cast<unsigned>(out.vertices.size());
        out.vertices.insert(out.vertices.end(), part.vertices.begin(), part.vertices.end());
        for (unsigned int idx : part.indices) {
            out.indices.push_back(base + idx);
        }
    }
    return out;
}

}  // namespace text3d
