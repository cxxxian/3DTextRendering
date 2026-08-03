#pragma once
/*
 * #14 Screen Present：全屏采样离屏色附件贴到当前 FB（默认窗口）。
 */

#include <string>

namespace text3d {

class ScreenPass {
public:
    ScreenPass() = default;
    ~ScreenPass();

    ScreenPass(const ScreenPass&) = delete;
    ScreenPass& operator=(const ScreenPass&) = delete;

    bool init(const std::string& shader_dir);
    void shutdown();

    /* gamma=1.0 passthrough；>1 时 out = pow(rgb, 1/gamma) */
    void draw(unsigned int color_tex, float gamma = 1.f) const;

    bool valid() const { return program_ != 0; }

private:
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    int loc_tex_ = -1;
    int loc_gamma_ = -1;
};

}  // namespace text3d
