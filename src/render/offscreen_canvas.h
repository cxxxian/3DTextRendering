#pragma once
/*
 * #14 空白离屏画布：同尺寸 FBO 复用；每帧 Clear 作输入，不回灌。
 */

namespace text3d {

class OffscreenCanvas {
public:
    OffscreenCanvas() = default;
    ~OffscreenCanvas();

    OffscreenCanvas(const OffscreenCanvas&) = delete;
    OffscreenCanvas& operator=(const OffscreenCanvas&) = delete;

    /* 尺寸未变则复用；变则重建附件。失败返回 false。 */
    bool ensure(int width, int height);

    void begin() const;
    void end() const;

    unsigned int color_tex() const { return color_tex_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return fbo_ != 0 && width_ > 0 && height_ > 0; }

    void destroy();

private:
    unsigned int fbo_ = 0;
    unsigned int color_tex_ = 0;
    unsigned int depth_rbo_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace text3d
