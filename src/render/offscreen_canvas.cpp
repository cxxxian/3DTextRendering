/*
 * OffscreenCanvas：RGBA8 色附件 + depth RBO，同尺寸跳过重建。
 */

#include "render/offscreen_canvas.h"

#include <iostream>

#include <glad/gl.h>

namespace text3d {

OffscreenCanvas::~OffscreenCanvas() {
    destroy();
}

void OffscreenCanvas::destroy() {
    if (depth_rbo_) {
        glDeleteRenderbuffers(1, &depth_rbo_);
        depth_rbo_ = 0;
    }
    if (color_tex_) {
        glDeleteTextures(1, &color_tex_);
        color_tex_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

bool OffscreenCanvas::ensure(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (fbo_ && width_ == width && height_ == height) {
        return true;
    }

    destroy();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);

    glGenRenderbuffers(1, &depth_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                              depth_rbo_);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[offscreen] FBO incomplete: 0x" << std::hex << status << std::dec << "\n";
        destroy();
        return false;
    }

    width_ = width;
    height_ = height;
    std::cout << "[offscreen] canvas ready " << width_ << "x" << height_ << "\n";
    return true;
}

void OffscreenCanvas::begin() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void OffscreenCanvas::end() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace text3d
