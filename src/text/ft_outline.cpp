/*
 * FreeType 轮廓实现：FT_Load_Glyph（NO_BITMAP）→ Decompose → 曲线细分 → GlyphOutline。
 */

#include "text/ft_outline.h"

#include <cmath>
#include <cstdio>
#include <iostream>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

namespace text3d {
namespace {

/* FreeType 26.6 定点 → 像素 float */
inline float ft26x6_to_float(FT_Pos v) {
    return static_cast<float>(v) / 64.0f;
}

/* FT_Outline_Decompose 回调上下文：把走笔事件攒成 Contour */
struct DecomposeContext {
    GlyphOutline* out = nullptr;
    float flatness = 0.5f;
    Contour current;
    Vec2 pen;

    void begin_contour(const Vec2& p) {
        flush_contour();
        current.points.clear();
        current.points.push_back(p);
        pen = p;
    }

    void line_to(const Vec2& p) {
        // 去掉近重合点，避免后续三角化出现退化边
        if (!current.points.empty()) {
            const Vec2& last = current.points.back();
            const float dx = p.x - last.x;
            const float dy = p.y - last.y;
            if (dx * dx + dy * dy < 1e-8f) {
                return;
            }
        }
        current.points.push_back(p);
        pen = p;
    }

    void flush_contour() {
        if (current.points.size() >= 3) {
            const Vec2& a = current.points.front();
            const Vec2& b = current.points.back();
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            if (dx * dx + dy * dy < 1e-8f) {
                current.points.pop_back();
            }
        }
        if (current.points.size() >= 3) {
            out->contours.push_back(current);
        }
        current.points.clear();
    }
};

/*
 * 二次贝塞尔细分：曲线中点到弦中点距离 > flatness 则对半切开，直到够直或 depth>16。
 */
void subdivide_conic(const Vec2& p0, const Vec2& p1, const Vec2& p2,
                     float flatness, DecomposeContext& ctx, int depth) {
    const Vec2 mid{
        0.25f * p0.x + 0.5f * p1.x + 0.25f * p2.x,
        0.25f * p0.y + 0.5f * p1.y + 0.25f * p2.y,
    };
    const Vec2 chord{(p0.x + p2.x) * 0.5f, (p0.y + p2.y) * 0.5f};
    const float dx = mid.x - chord.x;
    const float dy = mid.y - chord.y;

    if (depth > 16 || dx * dx + dy * dy <= flatness * flatness) {
        ctx.line_to(p2);
        return;
    }

    const Vec2 a{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    const Vec2 c{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
    const Vec2 b{(a.x + c.x) * 0.5f, (a.y + c.y) * 0.5f};
    subdivide_conic(p0, a, b, flatness, ctx, depth + 1);
    subdivide_conic(b, c, p2, flatness, ctx, depth + 1);
}

/* 三次贝塞尔：用控制点到弦的距离判断是否继续 de Casteljau 对半切 */
void subdivide_cubic(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3,
                     float flatness, DecomposeContext& ctx, int depth) {
    auto dist2_to_chord = [&](const Vec2& p) {
        const float cx = p3.x - p0.x;
        const float cy = p3.y - p0.y;
        const float len2 = cx * cx + cy * cy;
        if (len2 < 1e-12f) {
            const float dx = p.x - p0.x;
            const float dy = p.y - p0.y;
            return dx * dx + dy * dy;
        }
        float t = ((p.x - p0.x) * cx + (p.y - p0.y) * cy) / len2;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        const float qx = p0.x + t * cx;
        const float qy = p0.y + t * cy;
        const float dx = p.x - qx;
        const float dy = p.y - qy;
        return dx * dx + dy * dy;
    };

    const float d = std::max(dist2_to_chord(p1), dist2_to_chord(p2));
    if (depth > 16 || d <= flatness * flatness) {
        ctx.line_to(p3);
        return;
    }

    const Vec2 a01{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    const Vec2 a12{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
    const Vec2 a23{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
    const Vec2 b012{(a01.x + a12.x) * 0.5f, (a01.y + a12.y) * 0.5f};
    const Vec2 b123{(a12.x + a23.x) * 0.5f, (a12.y + a23.y) * 0.5f};
    const Vec2 c{(b012.x + b123.x) * 0.5f, (b012.y + b123.y) * 0.5f};
    subdivide_cubic(p0, a01, b012, c, flatness, ctx, depth + 1);
    subdivide_cubic(c, b123, a23, p3, flatness, ctx, depth + 1);
}

/* FT_Outline_Funcs：返回 0 继续，非 0 中止 */
int move_to(const FT_Vector* to, void* user) {
    auto* ctx = static_cast<DecomposeContext*>(user);
    ctx->begin_contour({ft26x6_to_float(to->x), ft26x6_to_float(to->y)});
    return 0;
}

int line_to(const FT_Vector* to, void* user) {
    auto* ctx = static_cast<DecomposeContext*>(user);
    ctx->line_to({ft26x6_to_float(to->x), ft26x6_to_float(to->y)});
    return 0;
}

int conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto* ctx = static_cast<DecomposeContext*>(user);
    const Vec2 p0 = ctx->pen;
    const Vec2 p1{ft26x6_to_float(control->x), ft26x6_to_float(control->y)};
    const Vec2 p2{ft26x6_to_float(to->x), ft26x6_to_float(to->y)};
    subdivide_conic(p0, p1, p2, ctx->flatness, *ctx, 0);
    return 0;
}

int cubic_to(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
    auto* ctx = static_cast<DecomposeContext*>(user);
    const Vec2 p0 = ctx->pen;
    const Vec2 p1{ft26x6_to_float(c1->x), ft26x6_to_float(c1->y)};
    const Vec2 p2{ft26x6_to_float(c2->x), ft26x6_to_float(c2->y)};
    const Vec2 p3{ft26x6_to_float(to->x), ft26x6_to_float(to->y)};
    subdivide_cubic(p0, p1, p2, p3, ctx->flatness, *ctx, 0);
    return 0;
}

}  // namespace

FontFace::~FontFace() {
    // FreeType 要求先 Done Face，再 Done Library
    if (face_) {
        FT_Done_Face(static_cast<FT_Face>(face_));
        face_ = nullptr;
    }
    if (library_) {
        FT_Done_FreeType(static_cast<FT_Library>(library_));
        library_ = nullptr;
    }
}

bool FontFace::load(const std::string& font_path, int pixel_size) {
    if (face_) {
        FT_Done_Face(static_cast<FT_Face>(face_));
        face_ = nullptr;
    }
    if (library_) {
        FT_Done_FreeType(static_cast<FT_Library>(library_));
        library_ = nullptr;
    }
    path_.clear();

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        std::cerr << "[ft_outline] FT_Init_FreeType 失败\n";
        return false;
    }

    FT_Face face = nullptr;
    if (FT_New_Face(library, font_path.c_str(), 0, &face) != 0) {
        std::cerr << "[ft_outline] 打不开字体: " << font_path << "\n";
        FT_Done_FreeType(library);
        return false;
    }

    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size)) != 0) {
        std::cerr << "[ft_outline] FT_Set_Pixel_Sizes 失败\n";
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return false;
    }

    library_ = library;
    face_ = face;
    path_ = font_path;
    return true;
}

void FontFace::dump_info() const {
    if (!face_) {
        std::cout << "[ft_outline] Face 未加载\n";
        return;
    }
    auto* face = static_cast<FT_Face>(face_);
    std::cout << "[ft_outline] family=" << (face->family_name ? face->family_name : "?")
              << " style=" << (face->style_name ? face->style_name : "?")
              << " glyphs=" << face->num_glyphs << "\n";
}

float FontFace::line_height() const {
    if (!face_) {
        return 0.f;
    }
    auto* face = static_cast<FT_Face>(face_);
    return ft26x6_to_float(face->size->metrics.height);
}

bool FontFace::load_glyph_outline_by_index(unsigned int glyph_index, GlyphOutline& out,
                                           float flatness) const {
    out = GlyphOutline{};
    if (!face_) {
        return false;
    }
    if (glyph_index == 0) {
        std::cerr << "[ft_outline] glyph_index=0 (.notdef?)\n";
    }
    auto* face = static_cast<FT_Face>(face_);

    const FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;
    if (FT_Load_Glyph(face, glyph_index, load_flags) != 0) {
        std::cerr << "[ft_outline] FT_Load_Glyph 失败 index=" << glyph_index << "\n";
        return false;
    }

    FT_GlyphSlot slot = face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
        std::cerr << "[ft_outline] glyph 不是 outline 格式（可能是 bitmap 字体）\n";
        return false;
    }

    out.advance_x = ft26x6_to_float(slot->metrics.horiAdvance);
    out.bearing_x = ft26x6_to_float(slot->metrics.horiBearingX);
    out.bearing_y = ft26x6_to_float(slot->metrics.horiBearingY);

    DecomposeContext ctx;
    ctx.out = &out;
    ctx.flatness = flatness;

    FT_Outline_Funcs funcs{};
    funcs.move_to = move_to;
    funcs.line_to = line_to;
    funcs.conic_to = conic_to;
    funcs.cubic_to = cubic_to;
    funcs.shift = 0;
    funcs.delta = 0;

    if (FT_Outline_Decompose(&slot->outline, &funcs, &ctx) != 0) {
        std::cerr << "[ft_outline] FT_Outline_Decompose 失败\n";
        return false;
    }
    ctx.flush_contour();

    std::cout << "[ft_outline] glyph_index=" << glyph_index
              << " contours=" << out.contours.size()
              << " advance=" << out.advance_x << "\n";
    return true;
}

bool FontFace::load_glyph_outline(char32_t codepoint, GlyphOutline& out,
                                  float flatness) const {
    out = GlyphOutline{};
    if (!face_) {
        return false;
    }
    auto* face = static_cast<FT_Face>(face_);

    // 无 shaping；复杂文种请走 HarfBuzz
    const FT_UInt glyph_index = FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
    if (glyph_index == 0) {
        std::cerr << "[ft_outline] 字体里没有该字符 codepoint="
                  << static_cast<unsigned>(codepoint) << "\n";
        return false;
    }
    return load_glyph_outline_by_index(glyph_index, out, flatness);
}

}  // namespace text3d
