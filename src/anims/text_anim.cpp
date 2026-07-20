/*
 * 内置动画登记与 TextAnimPlayer 实现；新效果在 list / create 各加一行。
 */

#include "anims/text_anim.h"

#include "anims/appear_spin.h"

#include <iostream>

namespace text3d {

std::vector<AnimOption> list_anim_options() {
    return {
        AnimOption{"", "None (merged mesh)", false},
        AnimOption{"appear_spin", "Appear Spin", true},
    };
}

std::unique_ptr<ITextAnim> create_anim(const std::string& id) {
    if (id.empty() || id == "none") {
        return nullptr;
    }
    if (id == "appear_spin") {
        return std::make_unique<AppearSpinAnim>();
    }
    std::cerr << "[text_anim] 未知动画 id: " << id << "\n";
    return nullptr;
}

void TextAnimPlayer::set_by_index(int option_index) {
    const auto opts = list_anim_options();
    if (option_index < 0 || option_index >= static_cast<int>(opts.size())) {
        option_index = 0;
    }
    selection_index_ = option_index;
    selection_id_ = opts[static_cast<size_t>(option_index)].id;
    anim_ = create_anim(selection_id_);
    if (anim_) {
        std::cout << "[text_anim] 当前动画: " << anim_->display_name() << "\n";
    } else {
        std::cout << "[text_anim] 当前动画: None\n";
    }
}

void TextAnimPlayer::set_by_id(const std::string& id) {
    const auto opts = list_anim_options();
    int idx = 0;
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (opts[static_cast<size_t>(i)].id == id) {
            idx = i;
            break;
        }
    }
    set_by_index(idx);
}

bool TextAnimPlayer::needs_per_glyph() const {
    return anim_ && anim_->needs_per_glyph();
}

bool TextAnimPlayer::playing() const {
    return anim_ && anim_->playing();
}

void TextAnimPlayer::play() {
    if (anim_) {
        anim_->play();
    }
}

void TextAnimPlayer::stop_idle() {
    if (anim_) {
        anim_->stop_idle();
    }
}

void TextAnimPlayer::update(float dt) {
    if (anim_) {
        anim_->update(dt);
    }
}

void TextAnimPlayer::sample(AnimSample& out) const {
    if (anim_) {
        anim_->sample(out);
    } else {
        out = AnimSample{};  // 无动画时清零，避免脏数据
    }
}

}  // namespace text3d
