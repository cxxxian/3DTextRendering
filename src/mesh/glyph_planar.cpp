/*
 * 二维平面构建 + 由平面挤出 3D（从 mesh_extrude 拆出，便于缓存接缝）。
 */

#include "mesh/glyph_planar.h"

#include "mesh/mesh_extrude.h"
#include "mesh/contour_clean.h"
#include "mesh/edge_chamfer.h"
#include "mesh/edge_fillet.h"
#include "mesh/mesh_geom.h"
#include "mesh/mesh_inflate.h"
#include "mesh/mesh_offset_tess.h"
#include "perf/perf_stats.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace text3d {

bool build_glyph_cleaned_outline(const GlyphOutline& raw, GlyphOutline& out_cleaned,
                                 BuildResult* result, RebuildTimings* timings) {
    out_cleaned = raw;
    {
        ScopedTimer timer(timings ? &timings->stage_2d_ms : nullptr);
        if (!clean_glyph_outline(out_cleaned)) {
            if (result) {
                result->set_fail(BuildStage::Outline, "outline clean removed all contours");
            }
            return false;
        }
    }
    return true;
}

bool build_glyph_planar_from_cleaned(const GlyphOutline& cleaned, const ExtrudeOptions& opt,
                                     GlyphPlanar2D& out, BuildResult* result) {
    out = GlyphPlanar2D{};
    if (cleaned.contours.empty()) {
        if (result) {
            result->set_fail(BuildStage::Outline, "empty contours");
        }
        return false;
    }

    out.cleaned = cleaned;

    float minx = 0.f, miny = 0.f, maxx = 0.f, maxy = 0.f;
    outline_bounds(out.cleaned, minx, miny, maxx, maxy);
    out.minx = minx;
    out.miny = miny;
    out.sx = std::max(maxx - minx, kMeshEps);
    out.sy = std::max(maxy - miny, kMeshEps);

    const float fillet_s = std::max(0.f, std::min(1.f, opt.fillet));
    const float bevel_s = std::max(0.f, std::min(1.f, opt.bevel));
    const bool use_fillet = fillet_s > kMeshEps;
    const float strength = use_fillet ? fillet_s : bevel_s;
    float radius = edge_radius_from_strength(strength, out.cleaned, opt.depth);

    RebuildTimings* timings = opt.timings;
    std::string tess_fallback;

    {
        ScopedTimer timer(timings ? &timings->stage_2d_ms : nullptr);
        if (radius > kMeshEps) {
            radius = resolve_inset_radius(out.cleaned, radius, opt.depth, opt.tess_mode, out.inner,
                                         out.cap_xy, out.cap_tris, &tess_fallback);
        }

        if (radius <= kMeshEps) {
            out.inner = GlyphOutline{};
            if (!try_tessellate(out.cleaned, opt.tess_mode, out.cap_xy, out.cap_tris,
                                &tess_fallback)) {
                std::cerr << "[glyph_planar] tessTesselate 失败（检查轮廓是否自交/退化）\n";
                if (result) {
                    result->set_fail(BuildStage::Tessellate,
                                     tess_fallback.empty() ? "tessellate failed"
                                                           : tess_fallback.c_str());
                }
                return false;
            }
        }
    }

    out.applied_radius = radius;
    out.tess_fallback = std::move(tess_fallback);

    if (opt.out_applied_radius) {
        *opt.out_applied_radius = out.applied_radius;
    }

    if (result) {
        result->set_ok(0, 0);
        if (!out.tess_fallback.empty()) {
            result->fallback_reason = out.tess_fallback;
        }
    }
    return true;
}

bool build_glyph_3d_from_planar(const GlyphPlanar2D& planar, const ExtrudeOptions& opt, Mesh& out,
                                BuildResult* result) {
    out = Mesh{};
    if (planar.cleaned.contours.empty() || planar.cap_tris.empty()) {
        if (result) {
            result->set_fail(BuildStage::Extrude, "empty planar input");
        }
        return false;
    }

    const float half = opt.depth * 0.5f;
    const float radius = planar.applied_radius;
    const float fillet_s = std::max(0.f, std::min(1.f, opt.fillet));
    const bool use_fillet = fillet_s > kMeshEps;

    const float inflate_s = std::max(0.f, std::min(1.f, opt.inflate));
    float inflate_h = inflate_height_from_strength(inflate_s, opt.depth);
    bool inflate_applied = false;

    RebuildTimings* timings = opt.timings;
    {
        ScopedTimer timer(timings ? &timings->stage_3d_ms : nullptr);
        if (radius <= kMeshEps) {
            const float z_top = opt.z_center + half;
            const float z_bot = opt.z_center - half;
            inflate_applied =
                append_inflated_caps(out, planar.cleaned, planar.cap_xy, planar.cap_tris, z_top,
                                     z_bot, inflate_h, planar.minx, planar.miny, planar.sx,
                                     planar.sy);
            append_straight_sides(out, planar.cleaned, z_top, z_bot);
        } else {
            EdgeBuildContext ctx{planar.cleaned, planar.inner,  planar.cap_xy, planar.cap_tris,
                                 opt.z_center,   half,          radius,        planar.minx,
                                 planar.miny,    planar.sx,     planar.sy,     inflate_h};
            std::unique_ptr<IEdgeProfile> profile;
            if (use_fillet) {
                profile = std::make_unique<FilletProfile>();
            } else {
                profile = std::make_unique<ChamferProfile>();
            }
            inflate_applied = profile->append_caps(out, ctx);
            profile->append_rims(out, ctx);
            const float z_wall_top = opt.z_center + (half - radius);
            const float z_wall_bot = opt.z_center - (half - radius);
            append_outer_walls(out, planar.cleaned, z_wall_top, z_wall_bot);
        }
    }

    if (opt.out_applied_radius) {
        *opt.out_applied_radius = radius;
    }
    if (opt.out_applied_inflate_h) {
        *opt.out_applied_inflate_h = inflate_applied ? inflate_h : 0.f;
    }

    if (out.indices.empty()) {
        if (result) {
            result->set_fail(BuildStage::Extrude, "empty mesh after extrude");
        }
        return false;
    }

    const int verts = static_cast<int>(out.vertices.size());
    const int tris = static_cast<int>(out.indices.size() / 3);
    if (result) {
        result->set_ok(verts, tris);
        if (!planar.tess_fallback.empty()) {
            result->fallback_reason = planar.tess_fallback;
        }
    }
    std::cout << "[mesh_extrude] vertices=" << verts << " indices=" << out.indices.size()
              << " depth=" << opt.depth << " applied_R=" << radius
              << (use_fillet ? " (fillet)" : " (bevel)") << " inflate_H=" << inflate_h;
    if (!planar.tess_fallback.empty()) {
        std::cout << " fallback=" << planar.tess_fallback;
    }
    std::cout << "\n[mesh_parts] " << mesh_parts_format(out);
    if (inflate_h <= kMeshEps && !out.part_range(MeshPart::Front).empty()) {
        std::cout << " front_flat=" << (mesh_front_normals_are_flat(out) ? "yes" : "no");
    }
    std::cout << "\n";
    return true;
}

}  // namespace text3d
