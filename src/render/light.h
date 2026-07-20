#pragma once
/*
 * 方向光参数（传给着色器）。
 */

#include <glm/glm.hpp>

namespace text3d {

struct DirectionalLight {
    glm::vec3 direction{0.35f, 0.7f, 0.55f};  // 指向光源
    glm::vec3 color{1.f, 1.f, 1.f};
    float ambient = 0.08f;
};

}  // namespace text3d
