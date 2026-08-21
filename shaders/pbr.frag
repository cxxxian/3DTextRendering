#version 330 core
// pbr.frag —— Cook-Torrance + Diffuse/ORM/Normal 贴图
//
// ORM（AO/Rough/Metal）：R=AO, G=Roughness, B=Metallic
// glTF MR（兼容）：G=Metallic, B=Roughness（uOrmLayout=1）
// 采样：uUseTriplanar=0 用顶点 UV；=1 用物体空间三平面（贴图跟字走，正侧连续）

in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vObjectPos;
in vec3 vObjectNormal;
in vec2 vUV;
in mat3 vTBN;
in mat3 vNormalMat;

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
uniform int uUseTriplanar;
uniform float uTriplanarScale;
uniform float uTriplanarSharpness;
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

vec3 triplanarWeights(vec3 N, float sharpness) {
    vec3 w = pow(abs(N), vec3(max(sharpness, 1.0)));
    float s = w.x + w.y + w.z;
    return w / max(s, 1e-5);
}

vec4 sampleTriplanar(sampler2D map, vec3 pos, vec3 N, float scale, float sharpness) {
    vec3 w = triplanarWeights(N, sharpness);
    vec3 p = pos * scale;
    vec4 cx = texture(map, p.yz);
    vec4 cy = texture(map, p.xz);
    vec4 cz = texture(map, p.xy);
    return cx * w.x + cy * w.y + cz * w.z;
}

/* 物体空间三平面法线，再由调用方乘 vNormalMat 到世界 */
vec3 sampleTriplanarNormalObj(sampler2D nmap, vec3 pos, vec3 N, float scale, float sharpness) {
    vec3 w = triplanarWeights(N, sharpness);
    vec3 p = pos * scale;

    vec3 tx = texture(nmap, p.yz).xyz * 2.0 - 1.0;
    vec3 ty = texture(nmap, p.xz).xyz * 2.0 - 1.0;
    vec3 tz = texture(nmap, p.xy).xyz * 2.0 - 1.0;

    vec3 nx = vec3(tx.z * sign(N.x), tx.x, tx.y);
    vec3 ny = vec3(ty.x, ty.z * sign(N.y), ty.y);
    vec3 nz = vec3(tz.x, tz.y, tz.z * sign(N.z));

    return normalize(nx * w.x + ny * w.y + nz * w.z);
}

void main() {
    vec3 Nobj = normalize(vObjectNormal);
    float tpScale = max(uTriplanarScale, 1e-6);
    float tpSharp = max(uTriplanarSharpness, 1.0);
    bool useTp = (uUseTriplanar != 0);

    vec3 albedo = uAlbedo;
    if (uUseAlbedoMap != 0) {
        if (useTp) {
            albedo = sampleTriplanar(uAlbedoMap, vObjectPos, Nobj, tpScale, tpSharp).rgb;
        } else {
            albedo = texture(uAlbedoMap, vUV).rgb;
        }
    }

    float metallic = uMetallic;
    float roughness = uRoughness;
    float ao = 1.0;
    if (uUseOrmMap != 0) {
        vec3 packed;
        if (useTp) {
            packed = sampleTriplanar(uOrmMap, vObjectPos, Nobj, tpScale, tpSharp).rgb;
        } else {
            packed = texture(uOrmMap, vUV).rgb;
        }
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
        if (useTp) {
            vec3 nObj = sampleTriplanarNormalObj(uNormalMap, vObjectPos, Nobj, tpScale, tpSharp);
            N = normalize(vNormalMat * nObj);
        } else {
            vec3 mapN = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
            N = normalize(vTBN * mapN);
        }
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
