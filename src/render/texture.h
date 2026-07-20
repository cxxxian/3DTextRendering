#pragma once
/*
 * 2D 纹理：stb_image 读图并上传为 OpenGL Texture2D。
 */

#include <string>

namespace text3d {

struct Texture2D {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
    int channels = 0;

    bool valid() const { return id != 0; }
    void destroy();
};

/* srgb=true 用 GL_SRGB8_ALPHA8（albedo）；false 用线性 RGBA（如 ORM） */
bool load_texture_2d(const std::string& path, Texture2D& out, bool srgb);

}  // namespace text3d
