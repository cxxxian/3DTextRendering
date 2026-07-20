#version 330 core
// glass.frag —— 假玻璃 + 程序化环境反射（产品照 studio 高光）
//
// 用 reflect(-V,N) 采样假天空/地/灯带，再用 Fresnel 混合；不做屏空间折射。

in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uCameraPos;
uniform vec3 uAlbedo;
uniform float uRoughness;
uniform float uAmbient;
uniform float uOpacity;
uniform float uEnvStrength;  // 环境反射强度

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

// 程序化环境：天空 / 地面 + 主光镜面亮斑 + 辅灯 + 横向灯带
vec3 sampleEnv(vec3 R, vec3 L, vec3 lightColor, float roughness) {
    R = normalize(R);
    float y = R.y;

    vec3 zenith = vec3(0.45, 0.62, 0.95);
    vec3 horizon = vec3(0.78, 0.82, 0.88);
    vec3 ground = vec3(0.16, 0.16, 0.18);
    vec3 col = (y > 0.0)
                   ? mix(horizon, zenith, pow(y, 0.55))
                   : mix(horizon, ground, pow(-y, 0.4));

    // 主光 → 反射球上的硬高光（亚克力最关键的一笔）
    float sun_exp = mix(420.0, 28.0, clamp(roughness * 3.0, 0.0, 1.0));
    float sun = pow(max(dot(R, normalize(L)), 0.0), sun_exp);
    col += lightColor * sun * 6.0;

    // 副光（对侧偏冷）
    vec3 L2 = normalize(vec3(-L.x, abs(L.y) * 0.4 + 0.45, -L.z));
    float fill_exp = mix(96.0, 16.0, clamp(roughness * 3.0, 0.0, 1.0));
    float fill = pow(max(dot(R, L2), 0.0), fill_exp);
    col += vec3(0.75, 0.85, 1.0) * fill * 2.2;

    // 横向 studio 灯带：扫过曲面时出现一条亮带
    float strip_w = 0.045 + roughness * roughness * 0.35;
    float strip = exp(-((R.y - 0.22) * (R.y - 0.22)) / max(strip_w, 1e-4));
    strip *= 0.55 + 0.45 * (0.5 + 0.5 * R.x);
    col += vec3(1.0, 0.98, 0.95) * strip * 1.6;

    // 顶部软填充
    col += vec3(0.2, 0.22, 0.28) * max(R.y, 0.0) * 0.35;

    return col;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    float NdotV = max(dot(N, V), 0.0);
    float roughness = max(uRoughness, 0.04);
    float rim = pow(1.0 - NdotV, 3.5);

    vec3 F0 = vec3(0.05);  // 略抬一点，塑料更容易读出反射
    vec3 Fv = fresnelSchlick(NdotV, F0);

    vec3 tint = mix(uAlbedo, uAlbedo * uAlbedo * 1.1, rim * 0.5);
    vec3 transmit = tint * (0.55 + 0.45 * NdotV);

    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // 解析高光（补环境里不够尖的部分）
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 Fs = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (NDF * G * Fs) / (4.0 * max(NdotV, 0.0) * NdotL + 1e-4);
    spec *= uLightColor * NdotL * 1.6;

    // 环境反射：粗糙时把 R 往 N 拉一点当作便宜模糊
    vec3 R = reflect(-V, N);
    R = normalize(mix(R, N, roughness * roughness * 0.85));
    vec3 env = sampleEnv(R, L, uLightColor, roughness) * uEnvStrength;

    // 透射 lit + Fresnel 反射
    vec3 ambient = uAmbient * transmit * 1.2;
    vec3 diffuse = transmit * (0.2 + 0.5 * NdotL) * uLightColor;
    vec3 base = ambient + diffuse;

    // Fv 控制反射占比；掠射角几乎全是环境
    float reflect_w = clamp(max(max(Fv.r, Fv.g), Fv.b) * 1.35 + rim * 0.25, 0.0, 1.0);
    vec3 color = mix(base, env, reflect_w) + spec * (0.55 + 0.45 * reflect_w);

    // 高光处抬 alpha，避免亮斑被掏空
    float gloss = clamp(max(dot(env, vec3(0.333)), 0.0) * reflect_w
                        + max(dot(spec, vec3(0.333)), 0.0), 0.0, 2.0);
    float alpha = mix(uOpacity, min(uOpacity + 0.65, 0.98), rim);
    alpha = clamp(alpha + gloss * 0.22, 0.04, 1.0);

    // 轻度 tonemap，保留高光尖峰
    color = color / (color + vec3(0.75));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(color, alpha);
}
