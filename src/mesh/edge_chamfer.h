#pragma once
/*
 * 平倒角（Chamfer / Bevel）：内@面 ──直线── 外@墙顶；正面仍平面。
 */

#include "mesh/edge_profile.h"

namespace text3d {

struct ChamferProfile : IEdgeProfile {
    bool append_caps(Mesh& out, const EdgeBuildContext& ctx) const override;
    void append_rims(Mesh& out, const EdgeBuildContext& ctx) const override;
};

}  // namespace text3d
