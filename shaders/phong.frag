#version 330 core
// phong.frag —— Blinn-Phong（ambient + diffuse + specular）

in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uCameraPos;
uniform vec3 uAlbedo;
uniform float uAmbient;
uniform float uShininess;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), max(uShininess, 1.0));

    vec3 ambient = uAmbient * uAlbedo;
    vec3 diffuse = NdotL * uAlbedo;
    vec3 specular = spec * vec3(1.0);

    vec3 rgb = (ambient + diffuse + specular) * uLightColor;
    FragColor = vec4(rgb, 1.0);
}
