/*
 * 排版实现：按行 shape → 取轮廓挤出 → 字心本地化并写入 rest 落点。
 */

#include "text/text_layout.h"

#include "mesh/mesh_offset_tess.h"
#include "text/hb_shaper.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace text3d {
namespace {

void center_glyphs_as_block(std::vector<GlyphInstance>& glyphs) {
    if (glyphs.empty()) {
        return;
    }
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (const auto& g : glyphs) {
        for (const auto& v : g.mesh.vertices) {
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

bool append_shaped_line(const FontFace& font, const std::string& line,
                        const LayoutOptions& opt, float pen_y,
                        std::vector<GlyphInstance>& out_glyphs,
                        float& edge_r_cap, bool& have_edge_cap) {
    if (line.empty()) {
        return true;
    }

    std::vector<ShapedGlyph> shaped;
    if (!shape_text(font, line, ShapeOptions{}, shaped)) {
        std::cerr << "[text_layout] shape 失败 line_bytes=" << line.size() << "\n";
        return false;
    }

    float pen_x = 0.f;
    for (const ShapedGlyph& sg : shaped) {
        GlyphOutline outline;
        if (!font.load_glyph_outline_by_index(sg.glyph_index, outline, opt.flatness)) {
            std::cerr << "[text_layout] 跳过 glyph_index=" << sg.glyph_index << "\n";
            pen_x += sg.x_advance;
            continue;
        }

        // 位置用 HarfBuzz；空轮廓只推进 pen
        if (outline.contours.empty()) {
            pen_x += sg.x_advance;
            continue;
        }

        {
            const float cap = max_safe_edge_radius(outline, opt.extrude.depth);
            if (cap > 0.f) {
                edge_r_cap = std::min(edge_r_cap, cap);
                have_edge_cap = true;
            }
        }

        Mesh glyph_mesh;
        if (!build_extruded_mesh(outline, opt.extrude, glyph_mesh)) {
            std::cerr << "[text_layout] 挤出失败 glyph_index=" << sg.glyph_index << "\n";
            pen_x += sg.x_advance;
            continue;
        }

        float gmin_x = 1e9f, gmax_x = -1e9f, gmin_y = 1e9f, gmax_y = -1e9f;
        for (const auto& v : glyph_mesh.vertices) {
            gmin_x = std::min(gmin_x, v.px);
            gmax_x = std::max(gmax_x, v.px);
            gmin_y = std::min(gmin_y, v.py);
            gmax_y = std::max(gmax_y, v.py);
        }
        const float gcx = 0.5f * (gmin_x + gmax_x);
        const float gcy = 0.5f * (gmin_y + gmax_y);
        translate_mesh(glyph_mesh, -gcx, -gcy, 0.f);

        // HB 落点 = pen + offset；本地原点已是字心，rest 再叠回 gcx/gcy
        GlyphInstance inst;
        inst.mesh = std::move(glyph_mesh);
        inst.rest_x = pen_x + sg.x_offset + gcx;
        inst.rest_y = pen_y + sg.y_offset + gcy;
        out_glyphs.push_back(std::move(inst));

        pen_x += sg.x_advance;
    }
    return true;
}

}  // namespace

bool layout_text_glyphs(const FontFace& font, const std::string& text,
                        const LayoutOptions& opt, std::vector<GlyphInstance>& out_glyphs) {
    out_glyphs.clear();
    if (!font.ok() || text.empty()) {
        return false;
    }

    const float line_h = font.line_height();
    float pen_y = 0.f;
    float edge_r_cap = 1e30f;
    bool have_edge_cap = false;

    const std::vector<std::string> lines = split_lines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            pen_y -= line_h;
        }
        if (!append_shaped_line(font, lines[i], opt, pen_y, out_glyphs, edge_r_cap,
                                have_edge_cap)) {
            // 单行失败不整段 abort：继续后续行
            continue;
        }
    }

    if (out_glyphs.empty()) {
        return false;
    }

    if (opt.scale != 1.f) {
        for (auto& g : out_glyphs) {
            scale_mesh(g.mesh, opt.scale);
            g.rest_x *= opt.scale;
            g.rest_y *= opt.scale;
        }
    }

    center_glyphs_as_block(out_glyphs);

    if (opt.out_edge_r_cap) {
        *opt.out_edge_r_cap = have_edge_cap ? edge_r_cap : 0.f;
    }

    std::cout << "[text_layout] glyphs=" << out_glyphs.size()
              << " utf8_bytes=" << text.size()
              << " lines=" << lines.size();
    if (have_edge_cap) {
        std::cout << " edge_r_cap=" << edge_r_cap;
    }
    std::cout << "\n";
    return true;
}

bool layout_text(const FontFace& font, const std::string& text,
                 const LayoutOptions& opt, Mesh& out_mesh) {
    std::vector<GlyphInstance> glyphs;
    if (!layout_text_glyphs(font, text, opt, glyphs)) {
        out_mesh = Mesh{};
        return false;
    }

    std::vector<Mesh> parts;
    parts.reserve(glyphs.size());
    for (auto& g : glyphs) {
        translate_mesh(g.mesh, g.rest_x, g.rest_y, 0.f);
        parts.push_back(std::move(g.mesh));
    }
    out_mesh = merge_meshes(parts);

    std::cout << "[text_layout] merged verts=" << out_mesh.vertices.size()
              << " indices=" << out_mesh.indices.size() << "\n";
    return true;
}

}  // namespace text3d
