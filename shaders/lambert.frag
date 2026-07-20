#version 330 core
// lambert.frag —— 半 Lambert 漫反射（保留旧观感）

in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAlbedo;
uniform float uAmbient;

out vec4 FragColor;

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(uLightDir);
    float ndotl = dot(n, l);
    float half_lambert = ndotl * 0.5 + 0.5;
    float diffuse = half_lambert * half_lambert;
    vec3 rgb = uAlbedo * uLightColor * (uAmbient + (1.0 - uAmbient) * diffuse);
    FragColor = vec4(rgb, 1.0);
}
