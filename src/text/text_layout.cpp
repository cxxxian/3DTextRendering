/*
 * 排版实现：按行 shape → 取轮廓挤出 → 字心本地化并写入 rest 落点。
 */

#include "text/text_layout.h"

#include "mesh/glyph_geometry_cache.h"
#include "mesh/glyph_mesh_pool.h"
#include "mesh/glyph_planar.h"
#include "mesh/mesh_offset_tess.h"
#include "mesh/outline_boolean.h"
#include "perf/perf_stats.h"
#include "text/hb_shaper.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace text3d {
namespace {

void center_glyphs_as_block(std::vector<GlyphInstance>& glyphs) {
    if (glyphs.empty()) {
        return;
    }
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (const auto& g : glyphs) {
        if (!g.mesh) {
            continue;
        }
        for (const auto& v : g.mesh->vertices) {
            const float wx = v.px + g.rest_x;
            const float wy = v.py + g.rest_y;
            min_x = std::min(min_x, wx);
            max_x = std::max(max_x, wx);
            min_y = std::min(min_y, wy);
            max_y = std::max(max_y, wy);
        }
    }
    const float cx = 0.5f * (min_x + max_x);
    const float cy = 0.5f * (min_y + max_y);
    for (auto& g : glyphs) {
        g.rest_x -= cx;
        g.rest_y -= cy;
    }
}

/* 按行切开（保留空行）；去掉 \r */
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    lines.push_back(cur);
    return lines;
}

constexpr int kOverlapNeighborK = 2;
constexpr float kOverlapAabbEps = 0.5f;
constexpr double kOverlapMinArea = 1e-2;
constexpr float kOverlapClipInflate = 0.25f;

struct PreparedGlyph {
    std::uint32_t glyph_index = 0;
    float origin_x = 0.f;
    float origin_y = 0.f;
    GlyphOutline cleaned_local;
    GlyphOutline work_layout;
    Aabb2 aabb;
    bool was_clipped = false;
};

bool load_cleaned_outline(const FontFace& font, std::uint32_t glyph_index, float flatness,
                          GlyphGeometryCache* cache, GlyphOutline& out_cleaned,
                          BuildResult* glyph_result, RebuildTimings* timings) {
    const OutlineKey ok_key{hash_font_path(font.path()), glyph_index, quantize_flatness(flatness)};
    if (cache) {
        if (const GlyphOutline* hit = cache->find_outline(ok_key)) {
            out_cleaned = *hit;
            return !out_cleaned.contours.empty();
        }
    }

    GlyphOutline outline;
    {
        ScopedTimer timer(timings ? &timings->stage_2d_ms : nullptr);
        if (!font.load_glyph_outline_by_index(glyph_index, outline, flatness)) {
            return false;
        }
    }
    if (outline.contours.empty()) {
        return false;
    }
    GlyphOutline cleaned_local;
    if (!build_glyph_cleaned_outline(outline, cleaned_local, glyph_result, timings)) {
        return false;
    }
    if (cache) {
        if (const GlyphOutline* stored = cache->put_outline(ok_key, std::move(cleaned_local))) {
            out_cleaned = *stored;
            return !out_cleaned.contours.empty();
        }
        return false;
    }
    out_cleaned = std::move(cleaned_local);
    return !out_cleaned.contours.empty();
}

void accumulate_radius_stats(float applied_r, float applied_h, float safe_cap, float& applied_r_min,
                             float& applied_r_max, float& safe_r_min, bool& have_radius_stats,
                             float& inflate_h_max) {
    if (safe_cap > 0.f) {
        safe_r_min = std::min(safe_r_min, safe_cap);
    }
    if (!have_radius_stats) {
        applied_r_min = applied_r;
        applied_r_max = applied_r;
        have_radius_stats = true;
    } else {
        applied_r_min = std::min(applied_r_min, applied_r);
        applied_r_max = std::max(applied_r_max, applied_r);
    }
    inflate_h_max = std::max(inflate_h_max, applied_h);
}

bool extrude_prepared_glyph(const FontFace& font, const PreparedGlyph& prep,
                            const LayoutOptions& opt, ExtrudeOptions& extrude,
                            std::shared_ptr<const Mesh>& mesh_sp, float& applied_r,
                            float& applied_h, float& safe_cap, float& layout_cx, float& layout_cy,
                            BuildResult& glyph_result) {
    mesh_sp.reset();
    applied_r = 0.f;
    applied_h = 0.f;
    safe_cap = 0.f;
    layout_cx = 0.f;
    layout_cy = 0.f;
    extrude.out_applied_radius = &applied_r;
    extrude.out_applied_inflate_h = &applied_h;

    const OutlineKey ok_key{hash_font_path(font.path()), prep.glyph_index,
                            quantize_flatness(opt.flatness)};
    const MeshKey mesh_key =
        make_mesh_key(ok_key, extrude.tess_mode, extrude.depth, extrude.bevel, extrude.fillet,
                      extrude.inflate, opt.scale);

    const bool can_use_pool = !prep.was_clipped;
    if (can_use_pool && opt.mesh_pool) {
        if (const PooledGlyphMesh* hit = opt.mesh_pool->find(mesh_key)) {
            mesh_sp = hit->mesh;
            applied_r = hit->applied_radius;
            applied_h = hit->applied_inflate_h;
            safe_cap = hit->safe_radius_cap;
            layout_cx = hit->layout_cx;
            layout_cy = hit->layout_cy;
            return static_cast<bool>(mesh_sp);
        }
    }

    GlyphOutline extrude_src = prep.cleaned_local;
    if (prep.was_clipped) {
        extrude_src = prep.work_layout;
        translate_outline(extrude_src, -prep.origin_x, -prep.origin_y);
    }

    Mesh glyph_mesh;
    bool ok = false;
    if (opt.cache && !prep.was_clipped) {
        safe_cap = max_safe_edge_radius(extrude_src, opt.extrude.depth);
        const PlanarKey pk = make_planar_key(ok_key, extrude.tess_mode, extrude.depth, extrude.bevel,
                                            extrude.fillet);
        const GlyphPlanar2D* planar_ptr = opt.cache->find_planar(pk);
        GlyphPlanar2D planar_local;
        if (!planar_ptr) {
            if (!build_glyph_planar_from_cleaned(extrude_src, extrude, planar_local, &glyph_result)) {
                return false;
            }
            if (opt.write_planar_cache) {
                planar_ptr = opt.cache->put_planar(pk, std::move(planar_local));
            } else {
                planar_ptr = &planar_local;
            }
        }
        if (!planar_ptr) {
            return false;
        }
        ok = build_glyph_3d_from_planar(*planar_ptr, extrude, glyph_mesh, &glyph_result);
        applied_r = planar_ptr->applied_radius;
    } else if (opt.cache && prep.was_clipped) {
        safe_cap = max_safe_edge_radius(extrude_src, opt.extrude.depth);
        GlyphPlanar2D planar_local;
        if (!build_glyph_planar_from_cleaned(extrude_src, extrude, planar_local, &glyph_result)) {
            return false;
        }
        ok = build_glyph_3d_from_planar(planar_local, extrude, glyph_mesh, &glyph_result);
        applied_r = planar_local.applied_radius;
    } else {
        safe_cap = max_safe_edge_radius(extrude_src, opt.extrude.depth);
        ok = build_extruded_mesh(extrude_src, extrude, glyph_mesh, &glyph_result);
    }

    if (!ok) {
        return false;
    }

    float gmin_x = 1e9f, gmax_x = -1e9f, gmin_y = 1e9f, gmax_y = -1e9f;
    for (const auto& v : glyph_mesh.vertices) {
        gmin_x = std::min(gmin_x, v.px);
        gmax_x = std::max(gmax_x, v.px);
        gmin_y = std::min(gmin_y, v.py);
        gmax_y = std::max(gmax_y, v.py);
    }
    layout_cx = 0.5f * (gmin_x + gmax_x);
    layout_cy = 0.5f * (gmin_y + gmax_y);
    translate_mesh(glyph_mesh, -layout_cx, -layout_cy, 0.f);
    if (opt.scale != 1.f) {
        scale_mesh(glyph_mesh, opt.scale);
    }

    auto owned = std::make_shared<Mesh>(std::move(glyph_mesh));
    if (can_use_pool && opt.mesh_pool && opt.write_mesh_pool) {
        PooledGlyphMesh entry;
        entry.mesh = owned;
        entry.applied_radius = applied_r;
        entry.applied_inflate_h = applied_h;
        entry.safe_radius_cap = safe_cap;
        entry.layout_cx = layout_cx;
        entry.layout_cy = layout_cy;
        opt.mesh_pool->put(mesh_key, std::move(entry));
    }
    mesh_sp = std::move(owned);
    return true;
}

bool append_shaped_line(const FontFace& font, const std::string& line,
                        const LayoutOptions& opt, float pen_y,
                        std::vector<GlyphInstance>& out_glyphs, float& applied_r_min,
                        float& applied_r_max, float& safe_r_min, bool& have_radius_stats,
                        float& inflate_h_max, BuildResult* result) {
    if (line.empty()) {
        return true;
    }

    std::vector<ShapedGlyph> shaped;
    {
        ScopedTimer timer(opt.timings ? &opt.timings->stage_2d_ms : nullptr);
        if (!shape_text(font, line, ShapeOptions{}, shaped)) {
            std::cerr << "[text_layout] shape 失败 line_bytes=" << line.size() << "\n";
            if (result) {
                result->set_fail(BuildStage::Shape, "shape_text failed");
            }
            return false;
        }
    }

    // Phase A：取 L0 cleaned，摆到布局坐标
    std::vector<PreparedGlyph> prepared;
    prepared.reserve(shaped.size());
    float pen_x = 0.f;
    for (const ShapedGlyph& sg : shaped) {
        BuildResult glyph_result;
        GlyphOutline cleaned;
        if (!load_cleaned_outline(font, sg.glyph_index, opt.flatness, opt.cache, cleaned,
                                  &glyph_result, opt.timings)) {
            if (glyph_result.stage != BuildStage::None && !glyph_result.ok) {
                std::cerr << "[text_layout] clean/outline 失败 glyph_index=" << sg.glyph_index
                          << " " << build_result_format(glyph_result) << "\n";
                if (result) {
                    if (result->stage == BuildStage::None || result->ok) {
                        *result = glyph_result;
                        result->glyph_index = sg.glyph_index;
                    }
                }
            } else if (result && result->fallback_reason.empty()) {
                result->fallback_reason =
                    "skipped outline glyph=" + std::to_string(sg.glyph_index);
            }
            pen_x += sg.x_advance;
            continue;
        }

        PreparedGlyph prep;
        prep.glyph_index = sg.glyph_index;
        prep.origin_x = pen_x + sg.x_offset;
        prep.origin_y = pen_y + sg.y_offset;
        prep.cleaned_local = std::move(cleaned);
        prep.work_layout = prep.cleaned_local;
        translate_outline(prep.work_layout, prep.origin_x, prep.origin_y);
        prep.aabb = compute_outline_aabb(prep.work_layout);
        prepared.push_back(std::move(prep));
        pen_x += sg.x_advance;
    }

    // Phase A2：相邻重叠差集（后字挖掉与先字重叠墨）
    int clipped_count = 0;
    for (size_t j = 0; j < prepared.size(); ++j) {
        std::vector<const GlyphOutline*> clips;
        for (size_t i = 0; i < j; ++i) {
            if (static_cast<int>(j - i) > kOverlapNeighborK) {
                continue;
            }
            if (!aabb_overlaps(prepared[i].aabb, prepared[j].aabb, kOverlapAabbEps)) {
                continue;
            }
            if (outline_intersection_area(prepared[i].work_layout, prepared[j].work_layout) <
                kOverlapMinArea) {
                continue;
            }
            clips.push_back(&prepared[i].work_layout);
        }
        if (clips.empty()) {
            continue;
        }

        GlyphOutline clipped;
        if (!difference_outlines(prepared[j].work_layout, clips, clipped, kOverlapClipInflate)) {
            if (result && result->fallback_reason.empty()) {
                result->fallback_reason = "overlap_clip_failed";
            }
            continue;
        }
        prepared[j].work_layout = std::move(clipped);
        prepared[j].aabb = compute_outline_aabb(prepared[j].work_layout);
        prepared[j].was_clipped = true;
        ++clipped_count;
    }

    // Phase B：挤出
    for (const PreparedGlyph& prep : prepared) {
        ExtrudeOptions extrude = opt.extrude;
        extrude.timings = opt.timings;
        float applied_r = 0.f;
        float applied_h = 0.f;
        float safe_cap = 0.f;
        float layout_cx = 0.f;
        float layout_cy = 0.f;
        BuildResult glyph_result;
        std::shared_ptr<const Mesh> mesh_sp;
        if (!extrude_prepared_glyph(font, prep, opt, extrude, mesh_sp, applied_r, applied_h,
                                    safe_cap, layout_cx, layout_cy, glyph_result)) {
            std::cerr << "[text_layout] 挤出失败 glyph_index=" << prep.glyph_index << " "
                      << build_result_format(glyph_result) << "\n";
            if (result) {
                if (result->stage == BuildStage::None || result->ok) {
                    *result = glyph_result;
                    result->glyph_index = prep.glyph_index;
                }
            }
            continue;
        }
        if (result && result->fallback_reason.empty() && !glyph_result.fallback_reason.empty()) {
            result->fallback_reason = glyph_result.fallback_reason;
        }

        accumulate_radius_stats(applied_r, applied_h, safe_cap, applied_r_min, applied_r_max,
                                safe_r_min, have_radius_stats, inflate_h_max);

        GlyphInstance inst;
        inst.mesh = std::move(mesh_sp);
        inst.rest_x = prep.origin_x + layout_cx;
        inst.rest_y = prep.origin_y + layout_cy;
        inst.glyph_index = prep.glyph_index;
        out_glyphs.push_back(std::move(inst));
    }

    if (clipped_count > 0) {
        std::cout << "[text_layout] overlap_clipped=" << clipped_count
                  << " of_prepared=" << prepared.size() << "\n";
    }
    return true;
}

int count_glyph_stats(const std::vector<GlyphInstance>& glyphs, int& out_tris) {
    int verts = 0;
    out_tris = 0;
    for (const auto& g : glyphs) {
        if (!g.mesh) {
            continue;
        }
        verts += static_cast<int>(g.mesh->vertices.size());
        out_tris += static_cast<int>(g.mesh->indices.size() / 3);
    }
    return verts;
}

}  // namespace

bool layout_text_glyphs(const FontFace& font, const std::string& text,
                        const LayoutOptions& opt, std::vector<GlyphInstance>& out_glyphs,
                        BuildResult* result) {
    out_glyphs.clear();
    if (result) {
        result->reset();
    }
    if (!font.ok() || text.empty()) {
        if (result) {
            result->set_fail(BuildStage::Shape, font.ok() ? "empty text" : "font not loaded");
        }
        return false;
    }

    const float line_h = font.line_height();
    float pen_y = 0.f;
    float applied_r_min = 0.f;
    float applied_r_max = 0.f;
    float safe_r_min = 1e30f;
    float inflate_h_max = 0.f;
    bool have_radius_stats = false;

    // 用于收集单字失败；若最终有可见字则整体仍算成功
    BuildResult first_glyph_fail;
    first_glyph_fail.reset();

    const std::vector<std::string> lines = split_lines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            pen_y -= line_h;
        }
        BuildResult line_result;
        if (!append_shaped_line(font, lines[i], opt, pen_y, out_glyphs, applied_r_min,
                                applied_r_max, safe_r_min, have_radius_stats, inflate_h_max,
                                &line_result)) {
            if (first_glyph_fail.stage == BuildStage::None) {
                first_glyph_fail = line_result;
            }
            continue;
        }
        if (!line_result.ok && line_result.stage != BuildStage::None &&
            first_glyph_fail.stage == BuildStage::None) {
            first_glyph_fail = line_result;
        }
        if (!line_result.fallback_reason.empty() && first_glyph_fail.fallback_reason.empty()) {
            first_glyph_fail.fallback_reason = line_result.fallback_reason;
        }
    }

    if (out_glyphs.empty()) {
        if (result) {
            if (first_glyph_fail.stage != BuildStage::None) {
                *result = first_glyph_fail;
            } else {
                result->set_fail(BuildStage::Outline, "no visible glyphs");
            }
        }
        return false;
    }

    if (opt.scale != 1.f) {
        for (auto& g : out_glyphs) {
            // Mesh 已在池构建时按 scale 写入；此处只缩放落点
            g.rest_x *= opt.scale;
            g.rest_y *= opt.scale;
        }
    }

    center_glyphs_as_block(out_glyphs);

    if (opt.out_applied_radius_min) {
        *opt.out_applied_radius_min = have_radius_stats ? applied_r_min : 0.f;
    }
    if (opt.out_applied_radius_max) {
        *opt.out_applied_radius_max = have_radius_stats ? applied_r_max : 0.f;
    }
    if (opt.out_safe_radius_min) {
        *opt.out_safe_radius_min = (safe_r_min < 1e29f) ? safe_r_min : 0.f;
    }
    if (opt.out_applied_inflate_h_max) {
        *opt.out_applied_inflate_h_max = inflate_h_max;
    }

    int tris = 0;
    const int verts = count_glyph_stats(out_glyphs, tris);
    if (result) {
        result->set_ok(verts, tris);
        if (!first_glyph_fail.fallback_reason.empty()) {
            result->fallback_reason = first_glyph_fail.fallback_reason;
        } else if (!first_glyph_fail.ok && first_glyph_fail.stage != BuildStage::None) {
            result->fallback_reason =
                std::string("partial glyph fail @") +
                build_stage_name(first_glyph_fail.stage) +
                (first_glyph_fail.glyph_index
                     ? (" glyph=" + std::to_string(first_glyph_fail.glyph_index))
                     : "");
        }
    }

    std::cout << "[text_layout] glyphs=" << out_glyphs.size()
              << " utf8_bytes=" << text.size() << " lines=" << lines.size()
              << " verts=" << verts << " tris=" << tris;
    if (have_radius_stats) {
        std::cout << " applied_R=[" << applied_r_min << "," << applied_r_max << "]";
    }
    if (safe_r_min < 1e29f) {
        std::cout << " safe_R_min=" << safe_r_min;
    }
    if (inflate_h_max > 0.f) {
        std::cout << " inflate_H=" << inflate_h_max;
    }
    if (result && !result->fallback_reason.empty()) {
        std::cout << " fallback=" << result->fallback_reason;
    }
    std::cout << "\n";
    return true;
}

bool layout_text(const FontFace& font, const std::string& text, const LayoutOptions& opt,
                 Mesh& out_mesh, BuildResult* result) {
    std::vector<GlyphInstance> glyphs;
    if (!layout_text_glyphs(font, text, opt, glyphs, result)) {
        out_mesh = Mesh{};
        return false;
    }

    std::vector<Mesh> parts;
    parts.reserve(glyphs.size());
    for (auto& g : glyphs) {
        if (!g.mesh) {
            continue;
        }
        Mesh copy = *g.mesh;
        translate_mesh(copy, g.rest_x, g.rest_y, 0.f);
        parts.push_back(std::move(copy));
    }
    {
        ScopedTimer timer(opt.timings ? &opt.timings->stage_merge_ms : nullptr);
        out_mesh = merge_meshes(parts);
    }

    if (out_mesh.indices.empty()) {
        if (result) {
            result->set_fail(BuildStage::Merge, "merged mesh empty");
        }
        out_mesh = Mesh{};
        return false;
    }

    const int verts = static_cast<int>(out_mesh.vertices.size());
    const int tris = static_cast<int>(out_mesh.indices.size() / 3);
    if (result) {
        const std::string fallback = result->fallback_reason;
        result->set_ok(verts, tris);
        result->fallback_reason = fallback;
    }

    std::cout << "[text_layout] merged verts=" << verts << " indices=" << out_mesh.indices.size()
              << "\n";
    return true;
}

}  // namespace text3d
