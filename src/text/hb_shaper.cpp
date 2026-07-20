/*
 * HarfBuzz + FreeType face（hb-ft）的 shaping 实现。
 */

#include "text/hb_shaper.h"

#include "text/ft_outline.h"
#include "text/utf8.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include <cstring>
#include <iostream>

namespace text3d {
namespace {

bool contains_arabic(const std::vector<char32_t>& cps) {
    for (char32_t cp : cps) {
        // Arabic + Arabic Presentation Forms
        if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
            (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
            (cp >= 0xFE70 && cp <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

hb_script_t parse_script(const char* s, bool arabic_hint) {
    if (s && s[0]) {
        return hb_script_from_string(s, -1);
    }
    return arabic_hint ? HB_SCRIPT_ARABIC : HB_SCRIPT_LATIN;
}

hb_direction_t parse_direction(const char* s, bool arabic_hint) {
    if (s && s[0]) {
        if (std::strcmp(s, "rtl") == 0) {
            return HB_DIRECTION_RTL;
        }
        if (std::strcmp(s, "ltr") == 0) {
            return HB_DIRECTION_LTR;
        }
    }
    return arabic_hint ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
}

hb_language_t parse_language(const char* s, bool arabic_hint) {
    if (s && s[0]) {
        return hb_language_from_string(s, -1);
    }
    return hb_language_from_string(arabic_hint ? "ar" : "en", -1);
}

}  // namespace

bool shape_text(const FontFace& font, const std::string& utf8,
                const ShapeOptions& opt, std::vector<ShapedGlyph>& out) {
    out.clear();
    if (!font.ok() || utf8.empty()) {
        return false;
    }

    FT_Face ft_face = static_cast<FT_Face>(font.raw_face());
    if (!ft_face) {
        return false;
    }

    const std::vector<char32_t> cps = utf8_to_codepoints(utf8);
    const bool arabic = contains_arabic(cps);

    hb_font_t* hb_font = hb_ft_font_create_referenced(ft_face);
    if (!hb_font) {
        std::cerr << "[hb_shaper] hb_ft_font_create 失败\n";
        return false;
    }
    // 与 FreeType 当前像素字号对齐；hb-ft 默认读 FT size
    hb_ft_font_set_load_flags(hb_font, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8.c_str(), static_cast<int>(utf8.size()), 0,
                       static_cast<int>(utf8.size()));

    const bool explicit_dir = opt.direction && opt.direction[0];
    const bool explicit_script = opt.script && opt.script[0];
    const bool explicit_lang = opt.language && opt.language[0];

    if (arabic || explicit_dir || explicit_script || explicit_lang) {
        hb_buffer_set_direction(buf, parse_direction(opt.direction, arabic));
        hb_buffer_set_script(buf, parse_script(opt.script, arabic));
        hb_buffer_set_language(buf, parse_language(opt.language, arabic));
    } else {
        hb_buffer_guess_segment_properties(buf);
    }

    hb_shape(hb_font, buf, nullptr, 0);

    const unsigned int count = hb_buffer_get_length(buf);
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, nullptr);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, nullptr);

    out.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        ShapedGlyph g;
        g.glyph_index = infos[i].codepoint;
        // HarfBuzz 位置单位：26.6（与 FT 一致），÷64 → 像素
        g.x_offset = static_cast<float>(positions[i].x_offset) / 64.f;
        g.y_offset = static_cast<float>(positions[i].y_offset) / 64.f;
        g.x_advance = static_cast<float>(positions[i].x_advance) / 64.f;
        g.y_advance = static_cast<float>(positions[i].y_advance) / 64.f;
        out.push_back(g);
    }

    std::cout << "[hb_shaper] glyphs=" << out.size()
              << " arabic=" << (arabic ? "yes" : "no")
              << " dir=" << (hb_buffer_get_direction(buf) == HB_DIRECTION_RTL ? "rtl" : "ltr")
              << "\n";

    hb_buffer_destroy(buf);
    hb_font_destroy(hb_font);
    return !out.empty();
}

}  // namespace text3d
