#version 330 core
// pbr.vert —— 共用顶点着色器（Lambert / Phong / PBR 共用）
//
// 输入：
//   aPos    顶点位置（来自 Mesh）
//   aNormal 顶点法线（来自 Mesh，用来算光照）
//   aUV     纹理坐标（顶/底 bbox；侧面沿轮廓×厚度）
// 输出给片元着色器：
//   vNormal / vWorldPos / vUV
//
// uMVP   = Projection * View * Model，把物体坐标变到裁剪空间（最终到屏幕）
// uModel = 只有模型变换，用来做法线旋转

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    // 本 demo 只有旋转+均匀缩放，用 mat3(uModel) 转法线就够
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
