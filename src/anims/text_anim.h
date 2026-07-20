#pragma once
/*
 * 动画注册表与 TextAnimPlayer：切换效果并转发 play / update / sample。
 */

#include "anims/i_text_anim.h"

#include <memory>
#include <string>
#include <vector>

namespace text3d {

/* ImGui 下拉一项；首位 id 为空表示 None / merged */
struct AnimOption {
    std::string id;    // 空 = 无动画
    std::string label;
    bool needs_per_glyph = false;
};

std::vector<AnimOption> list_anim_options();
std::unique_ptr<ITextAnim> create_anim(const std::string& id);

class TextAnimPlayer {
public:
    void set_by_index(int option_index);
    void set_by_id(const std::string& id);

    int selection_index() const { return selection_index_; }
    const std::string& selection_id() const { return selection_id_; }

    bool needs_per_glyph() const;
    bool has_anim() const { return anim_ != nullptr; }
    bool playing() const;

    void play();
    void stop_idle();
    void update(float dt);
    void sample(AnimSample& out) const;

    ITextAnim* get() { return anim_.get(); }

private:
    int selection_index_ = 0;
    std::string selection_id_;
    std::unique_ptr<ITextAnim> anim_;
};

}  // namespace text3d
