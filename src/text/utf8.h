#pragma once
/*
 * 最小 UTF-8 解码：字节串 → Unicode codepoint 序列。
 */

#include <string>
#include <vector>

namespace text3d {

/* 非法序列会被跳过 */
std::vector<char32_t> utf8_to_codepoints(const std::string& text);

}  // namespace text3d
