/*
 * ScreenPass：空 VAO + gl_VertexID 全屏三角；采样 uTex。
 */

#include "render/screen_pass.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <glad/gl.h>

namespace text3d {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

unsigned int compile_shader(unsigned int type, const std::string& src) {
    const unsigned int sh = glCreateShader(type);
    const char* csrc = src.c_str();
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::cerr << "[screen] shader compile error:\n" << log << "\n";
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

}  // namespace

ScreenPass::~ScreenPass() {
    shutdown();
}

void ScreenPass::shutdown() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    loc_tex_ = -1;
    loc_gamma_ = -1;
}

bool ScreenPass::init(const std::string& shader_dir) {
    shutdown();

    const std::string vs_src = read_file(shader_dir + "/screen.vert");
    const std::string fs_src = read_file(shader_dir + "/screen.frag");
    if (vs_src.empty() || fs_src.empty()) {
        std::cerr << "[screen] 读不到着色器: " << shader_dir << "/screen.*\n";
        return false;
    }

    const unsigned int vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    const unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) {
            glDeleteShader(vs);
        }
        if (fs) {
            glDeleteShader(fs);
        }
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "[screen] program link error:\n" << log << "\n";
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    loc_tex_ = glGetUniformLocation(program_, "uTex");
    loc_gamma_ = glGetUniformLocation(program_, "uGamma");

    glGenVertexArrays(1, &vao_);
    std::cout << "[screen] present pass ready\n";
    return true;
}

void ScreenPass::draw(unsigned int color_tex, float gamma) const {
    if (!program_ || !color_tex) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(program_);
    if (loc_tex_ >= 0) {
        glUniform1i(loc_tex_, 0);
    }
    if (loc_gamma_ >= 0) {
        glUniform1f(loc_gamma_, gamma);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
}

}  // namespace text3d
