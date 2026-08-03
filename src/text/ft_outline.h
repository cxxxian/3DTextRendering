#pragma once
/*
 * FreeType 字形轮廓：加载字体，把曲线细分成折线，输出 GlyphOutline。
 * 不做三角化、挤出或渲染；与 2D 流程不同，这里只要矢量 outline。
 */

#include <string>
#include <vector>

namespace text3d {

/* 平面点，单位为像素 float（FreeType 26.6 定点读出后 /64） */
struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

/*
 * 一条封闭轮廓环。points 为折线顶点（曲线已细分），首尾不重复存同一点。
 * TrueType（Y 轴向上）常见绕序：外环顺时针，内孔逆时针，三角化靠绕序区分实心与洞。
 */
struct Contour {
    std::vector<Vec2> points;
};

/* 单字 2D 轮廓结果（尚无厚度） */
struct GlyphOutline {
    std::vector<Contour> contours;
    float advance_x = 0.f;  // 水平步进（horiAdvance）
    float bearing_x = 0.f;
    float bearing_y = 0.f;
};

/* FreeType 库实例 + Face 的薄封装 */
class FontFace {
public:
    FontFace() = default;
    ~FontFace();

    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    /* 打开字体并设置像素字号（高度方向） */
    bool load(const std::string& font_path, int pixel_size);

    /* 按 Unicode 取轮廓并细分曲线；flatness 为细分容差（像素），越小点越多 */
    bool load_glyph_outline(char32_t codepoint, GlyphOutline& out,
                            float flatness = 0.5f) const;

    /* 按 glyph index 取轮廓（HarfBuzz shaping 之后走这条） */
    bool load_glyph_outline_by_index(unsigned int glyph_index, GlyphOutline& out,
                                     float flatness = 0.5f) const;

    void dump_info() const;
    bool ok() const { return face_ != nullptr; }

    /* 供 hb-ft 使用，实际类型为 FT_Face */
    void* raw_face() const { return face_; }

    /* 推荐行高（像素） */
    float line_height() const;

    /* 当前加载的字体路径（缓存键用） */
    const std::string& path() const { return path_; }

private:
    // 用 void* 避免头文件依赖 FreeType；.cpp 中转为 FT_Library / FT_Face
    void* library_ = nullptr;
    void* face_ = nullptr;
    std::string path_;
};

}  // namespace text3d
