#pragma once
/*
 * Appear Spin：全体字共用进度，近处绕 Y 转约一周后落回 rest。
 */

#include "anims/i_text_anim.h"

namespace text3d {

class AppearSpinAnim : public ITextAnim {
public:
    const char* id() const override { return "appear_spin"; }
    const char* display_name() const override { return "Appear Spin"; }
    bool needs_per_glyph() const override { return true; }

    void play() override;
    void stop_idle() override;
    void update(float dt) override;
    bool playing() const override { return playing_; }
    void sample(AnimSample& out) const override;

    float duration = 0.9f;       // 片段总时长（秒）
    float near_distance = 1.6f;  // 起始相对 rest 朝相机的最大位移

private:
    bool playing_ = false;
    float t_ = 0.f;  // 片段内已播放时间
};

}  // namespace text3d
