#pragma once
/*
 * 真圆角（Fillet）：圆心在内点，P = inner+(outer-inner)*sinθ，z ∝ cosθ（1/4 圆弧）。
 * 只做棱带；帽面平面，鼓包仅由 Inflate 控制。勿 XY/Z 同用 cos（会退化成直线斜面）。
 */

#include "mesh/edge_profile.h"

namespace text3d {

struct FilletProfile : IEdgeProfile {
    bool append_caps(Mesh& out, const EdgeBuildContext& ctx) const override;
    void append_rims(Mesh& out, const EdgeBuildContext& ctx) const override;
};

}  // namespace text3d
