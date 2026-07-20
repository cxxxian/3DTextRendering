/*
 * 纹理加载实现：stb_image → GL_TEXTURE_2D（RGBA8 / SRGB8_ALPHA8 + mipmap）。
 */

#include "render/texture.h"

#include <iostream>

#include <glad/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace text3d {

void Texture2D::destroy() {
    if (id) {
        glDeleteTextures(1, &id);
        id = 0;
    }
    width = height = channels = 0;
}

bool load_texture_2d(const std::string& path, Texture2D& out, bool srgb) {
    out.destroy();

    stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        std::cerr << "[texture] 加载失败: " << path << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const GLenum internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    out.id = tex;
    out.width = w;
    out.height = h;
    out.channels = 4;
    std::cout << "[texture] loaded " << path << " " << w << "x" << h << "\n";
    return true;
}

}  // namespace text3d
