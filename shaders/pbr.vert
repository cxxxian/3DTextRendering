#version 330 core
// pbr.vert —— 共用顶点着色器（Lambert / Phong / PBR / Glass）
//
// uUseInstance=0：整句 / 单次 draw，用 uMVP + uModel
// uUseInstance=1：逐字实例化，用 uVP + aModel（每 instance 一份）
//
// location：0 pos / 1 normal / 2 uv / 3 tangent(xyzw) / 4..7 instance mat4

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in mat4 aModel;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uVP;
uniform int uUseInstance;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vObjectPos;
out vec3 vObjectNormal;
out vec2 vUV;
out mat3 vTBN;
out mat3 vNormalMat;

void main() {
    mat4 model = (uUseInstance != 0) ? aModel : uModel;
    vec4 world = model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vObjectPos = aPos;
    vObjectNormal = normalize(aNormal);
    vUV = aUV;

    mat3 normalMat = mat3(model);
    vNormalMat = normalMat;
    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent.xyz);
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * aTangent.w;
    vNormal = N;
    vTBN = mat3(T, B, N);

    if (uUseInstance != 0) {
        gl_Position = uVP * world;
    } else {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
}
