/*
 * Appear Spin 时间轴与缓动；translate/rotate 在 main 里做。
 */

#include "anims/appear_spin.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace text3d {
namespace {

float ease_out_cubic(float u) {
    u = std::clamp(u, 0.f, 1.f);
    const float inv = 1.f - u;
    return 1.f - inv * inv * inv;
}

}  // namespace

void AppearSpinAnim::play() {
    playing_ = true;
    t_ = 0.f;
}

void AppearSpinAnim::stop_idle() {
    playing_ = false;
    t_ = 0.f;
}

void AppearSpinAnim::update(float dt) {
    if (!playing_) {
        return;
    }
    t_ += dt;
    if (t_ >= duration) {
        stop_idle();
    }
}

void AppearSpinAnim::sample(AnimSample& out) const {
    out.near_distance = near_distance;
    if (!playing_) {
        out.angle_y = 0.f;
        out.approach = 0.f;
        return;
    }

    const float u = t_ / std::max(duration, 1e-4f);
    if (u >= 1.f) {
        out.angle_y = 0.f;
        out.approach = 0.f;
        return;
    }

    const float e = ease_out_cubic(u);
    out.approach = 1.f - e;  // 1→0：近处 → 原位
    out.angle_y = static_cast<float>(2.0 * M_PI) * (1.f - e);  // 约转一圈落到 0
}

}  // namespace text3d
