#pragma once
/*
 * HarfBuzz shaping：UTF-8 文本 → glyph index + 相对 pen 的偏移/步进。
 * FreeType 仍负责 outline；本模块只做 OpenType 选型与定位。
 */

#include <cstdint>
#include <string>
#include <vector>

namespace text3d {

class FontFace;

struct ShapedGlyph {
    uint32_t glyph_index = 0;
    float x_offset = 0.f;   // 相对 pen 的偏移（像素）
    float y_offset = 0.f;
    float x_advance = 0.f;  // 画完后 pen 水平推进
    float y_advance = 0.f;
};

struct ShapeOptions {
    // 空则按文本猜：含阿拉伯字母 → arab/rtl，否则 latn/ltr
    const char* script = nullptr;     // 如 "arab" / "latn"
    const char* direction = nullptr;  // "ltr" / "rtl"
    const char* language = nullptr;   // 如 "ar" / "en"
};

/* 对整段 UTF-8 做 shaping；换行符不进 buffer（由 layout 分段） */
bool shape_text(const FontFace& font, const std::string& utf8,
                const ShapeOptions& opt, std::vector<ShapedGlyph>& out);

}  // namespace text3d
