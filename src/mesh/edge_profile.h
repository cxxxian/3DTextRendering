#pragma once
/*
 * 棱剖面策略接口（Bevel / Fillet）。
 * 公共管线只调 append_caps / append_rims，再统一 append_outer_walls；
 * 策略不写外轮廓直墙，避免与 geom 重复。
 */

#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <vector>

namespace text3d {

/* 一次挤出里给策略用的上下文 */
struct EdgeBuildContext {
    const GlyphOutline& outer;                          // 原轮廓（直墙在此）
    const GlyphOutline& inner;                          // 内缩轮廓（正/背面）
    const std::vector<float>& cap_xy;                   // 内轮廓 tess 顶点 xy 交错
    const std::vector<unsigned int>& cap_tris;          // 三角索引
    float z_center = 0.f;
    float half = 0.f;                                   // depth/2
    float radius = 0.f;                                 // 已钳制的 R
    float minx = 0.f, miny = 0.f, sx = 1.f, sy = 1.f;  // 外轮廓 bbox → UV
};

struct IEdgeProfile {
    virtual ~IEdgeProfile() = default;

    /* 写正/背面（平面或轻拱） */
    virtual void append_caps(Mesh& out, const EdgeBuildContext& ctx) const = 0;

    /* 写上下棱过渡带：内面边缘 ↔ 外墙顶/底（不含直墙） */
    virtual void append_rims(Mesh& out, const EdgeBuildContext& ctx) const = 0;
};

}  // namespace text3d
