#version 330 core
// pbr.frag —— Cook-Torrance + Diffuse/ORM/Normal 贴图
//
// ORM（AO/Rough/Metal）：R=AO, G=Roughness, B=Metallic
// glTF MR（兼容）：G=Metallic, B=Roughness（uOrmLayout=1）

in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uCameraPos;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAmbient;

uniform int uUseAlbedoMap;
uniform int uUseOrmMap;
uniform int uUseNormalMap;
uniform int uOrmLayout;  // 0=ORM, 1=glTF MR
uniform sampler2D uAlbedoMap;
uniform sampler2D uOrmMap;
uniform sampler2D uNormalMap;

out vec4 FragColor;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-7);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 无切线时用屏幕空间导数建 TBN（Mikkelsen cotangent frame）
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

vec3 applyNormalMap(vec3 N, vec3 p, vec2 uv, sampler2D nmap) {
    vec3 mapN = texture(nmap, uv).xyz * 2.0 - 1.0;
    mat3 TBN = cotangentFrame(normalize(N), p, uv);
    return normalize(TBN * mapN);
}

void main() {
    vec3 albedo = uAlbedo;
    if (uUseAlbedoMap != 0) {
        albedo = texture(uAlbedoMap, vUV).rgb;
    }

    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = 1.0;
    if (uUseOrmMap != 0) {
        vec3 packed = texture(uOrmMap, vUV).rgb;
        if (uOrmLayout == 0) {
            ao = packed.r;
            roughness = packed.g;
            metallic = packed.b;
        } else {
            metallic = packed.g;
            roughness = packed.b;
        }
    }
    roughness = max(roughness, 0.04);

    vec3 N = normalize(vNormal);
    if (uUseNormalMap != 0) {
        N = applyNormalMap(N, vWorldPos, vUV, uNormalMap);
    }

    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-4;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * uLightColor * NdotL;

    vec3 ambient = uAmbient * albedo * ao * (1.0 - metallic * 0.5);
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
