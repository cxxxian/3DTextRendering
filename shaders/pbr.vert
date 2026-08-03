#version 330 core
// pbr.vert —— 共用顶点着色器（Lambert / Phong / PBR / Glass）
//
// uUseInstance=0：整句 / 单次 draw，用 uMVP + uModel
// uUseInstance=1：逐字实例化，用 uVP + aModel（每 instance 一份）

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in mat4 aModel;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uVP;
uniform int uUseInstance;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;

void main() {
    mat4 model = (uUseInstance != 0) ? aModel : uModel;
    vec4 world = model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(model) * aNormal;
    vUV = aUV;
    if (uUseInstance != 0) {
        gl_Position = uVP * world;
    } else {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
}
