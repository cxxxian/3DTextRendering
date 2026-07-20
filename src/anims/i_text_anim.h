#pragma once
/*
 * 文字入场动画接口：只产出 AnimSample 标量，矩阵由 main 组装。
 */

namespace text3d {

/* 一帧采样结果（不含 glm 矩阵） */
struct AnimSample {
    float angle_y = 0.f;       // 绕字自身 Y 轴（弧度）
    float approach = 0.f;      // 1=靠相机最近，0=已回 rest
    float near_distance = 1.6f;  // approach==1 时相对 rest 朝相机的位移
};

/* 入场动画契约；具体效果放在 src/anims/ 下 */
class ITextAnim {
public:
    virtual ~ITextAnim() = default;

    virtual const char* id() const = 0;
    virtual const char* display_name() const = 0;

    /* true：必须逐字 mesh；false：可用整句 merged */
    virtual bool needs_per_glyph() const = 0;

    virtual void play() = 0;
    virtual void stop_idle() = 0;
    virtual void update(float dt) = 0;
    virtual bool playing() const = 0;
    virtual void sample(AnimSample& out) const = 0;
};

}  // namespace text3d
