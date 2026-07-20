#pragma once
/*
 * 轨道相机：yaw / pitch / distance 绕目标点。
 */

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace text3d {

struct OrbitCamera {
    float yaw_deg = 25.f;
    float pitch_deg = 20.f;
    float distance = 3.5f;
    glm::vec3 target{0.f, 0.f, 0.f};

    glm::vec3 eye() const {
        const float yaw = glm::radians(yaw_deg);
        const float pitch = glm::radians(pitch_deg);
        return glm::vec3{
            target.x + distance * std::cos(pitch) * std::sin(yaw),
            target.y + distance * std::sin(pitch),
            target.z + distance * std::cos(pitch) * std::cos(yaw),
        };
    }

    glm::mat4 view() const {
        return glm::lookAt(eye(), target, glm::vec3(0.f, 1.f, 0.f));
    }

    glm::mat4 projection(float aspect, float fovy_deg = 45.f, float near_plane = 0.05f,
                         float far_plane = 100.f) const {
        return glm::perspective(glm::radians(fovy_deg), aspect, near_plane, far_plane);
    }
};

}  // namespace text3d
