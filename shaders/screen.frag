#version 330 core
// screen.frag —— 采样离屏画布；uGamma=1 passthrough，否则 encode

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTex;
uniform float uGamma;  // 1.0 = off；典型显示矫正用 2.2

void main() {
    vec4 c = texture(uTex, vUV);
    if (uGamma > 1.0001) {
        c.rgb = pow(max(c.rgb, vec3(0.0)), vec3(1.0 / uGamma));
    }
    FragColor = c;
}
