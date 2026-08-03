#pragma once
/*
 * 单字二维平面结果：clean 轮廓 +（可选）内缩 + 帽面三角化。
 * 供 #8 L0/L1 缓存与 3D 挤出复用。
 */

#include "mesh/build_result.h"
#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <string>
#include <vector>

namespace text3d {

struct ExtrudeOptions;
struct RebuildTimings;

/* 平面阶段产物；3D 挤出只读此结构 + ExtrudeOptions 中的 depth/inflate/剖面选择 */
struct GlyphPlanar2D {
    GlyphOutline cleaned;  // clean 后外轮廓（侧墙 / Edge 的 src）
    GlyphOutline inner;    // 内缩轮廓；applied_radius≈0 时可空
    std::vector<float> cap_xy;
    std::vector<unsigned int> cap_tris;
    float minx = 0.f;
    float miny = 0.f;
    float sx = 1.f;
    float sy = 1.f;
    float applied_radius = 0.f;
    std::string tess_fallback;
};

/* 原始轮廓 → clean（失败则 result 写 Outline） */
bool build_glyph_cleaned_outline(const GlyphOutline& raw, GlyphOutline& out_cleaned,
                                 BuildResult* result = nullptr,
                                 RebuildTimings* timings = nullptr);

/*
 * 已 clean 轮廓 → 内缩（若需）+ 三角化。
 * 半径由 opt.bevel/fillet/depth 决定；写入 planar.cleaned = cleaned 副本。
 */
bool build_glyph_planar_from_cleaned(const GlyphOutline& cleaned, const ExtrudeOptions& opt,
                                     GlyphPlanar2D& out, BuildResult* result = nullptr);

/* 平面结果 → 立体 Mesh（始终现算，不进 L1） */
bool build_glyph_3d_from_planar(const GlyphPlanar2D& planar, const ExtrudeOptions& opt, Mesh& out,
                                BuildResult* result = nullptr);

}  // namespace text3d
