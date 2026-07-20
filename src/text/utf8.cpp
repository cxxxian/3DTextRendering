/*
 * UTF-8 解码实现（不依赖 ICU 等库）。
 */

#include "text/utf8.h"

namespace text3d {

std::vector<char32_t> utf8_to_codepoints(const std::string& text) {
    std::vector<char32_t> out;
    out.reserve(text.size());

    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char* end = p + text.size();

    while (p < end) {
        const unsigned char c0 = *p++;

        if (c0 < 0x80) {
            out.push_back(c0);
            continue;
        }

        int need = 0;
        char32_t cp = 0;
        if ((c0 & 0xE0) == 0xC0) {
            need = 1;
            cp = c0 & 0x1F;
        } else if ((c0 & 0xF0) == 0xE0) {
            need = 2;
            cp = c0 & 0x0F;
        } else if ((c0 & 0xF8) == 0xF0) {
            need = 3;
            cp = c0 & 0x07;
        } else {
            continue;  // 非法起始字节
        }

        bool ok = true;
        for (int i = 0; i < need; ++i) {
            if (p >= end || (*p & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (*p++ & 0x3F);
        }
        if (ok) {
            out.push_back(cp);
        }
    }
    return out;
}

}  // namespace text3d
