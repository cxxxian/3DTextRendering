#pragma once
/*
 * 文本排版：HarfBuzz shape → FreeType outline → 挤出；提供整句与逐字两种出口。
 */

#include "text/ft_outline.h"
#include "mesh/mesh_extrude.h"

#include <string>
#include <vector>

namespace text3d {

struct LayoutOptions {
    ExtrudeOptions extrude;
    float flatness = 0.5f;
    float scale = 1.f;
    float* out_edge_r_cap = nullptr;  // 可选：Bevel/Fillet 安全半径上限（字体单位，取各字最紧）
};

/* 单个可见字：几何在本地（原点=字心）；rest 为静止时的世界落点 */
struct GlyphInstance {
    Mesh mesh;
    float rest_x = 0.f;
    float rest_y = 0.f;
};

/* 逐字出口：入场动画用（每字一份 mesh + 落点） */
bool layout_text_glyphs(const FontFace& font, const std::string& text,
                        const LayoutOptions& opt, std::vector<GlyphInstance>& out_glyphs);

/* 整句出口：内部先 glyphs 再 merge，无动画时一次 draw */
bool layout_text(const FontFace& font, const std::string& text,
                 const LayoutOptions& opt, Mesh& out_mesh);

}  // namespace text3d
