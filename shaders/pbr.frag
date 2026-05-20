#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inT;
layout(location = 3) in vec3 inB;
layout(location = 4) in vec3 inN;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 lightDir;      /* xyz = direction (toward light), w = intensity */
    vec4 lightColor;    /* xyz = color, w = gridY */
    mat4 lightVP;       /* light view-proj for shadow map */
    vec4 extra;         /* x=shadowEnabled(0=off,1=plan only,2=plan+model), y=planMode(0=solid,1=checker), z=checkerScale */
    vec4 planColor;     /* xyz = plan color */
};
layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(push_constant) uniform MeshPC {
    mat4    model;
    vec4    baseColor;
    float   metallic;
    float   roughness;
    int     flags;       /* bit0=albedo, bit1=normal, bit2=orm, bit3=emissive */
    int     renderMode;
};

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D ormMap;       /* R=ao, G=rough, B=metal */
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float shadowFactor(vec3 worldPos) {
    if(extra.x < 1.5) return 1.0;
    vec4 lsPos = lightVP * vec4(worldPos, 1.0);
    lsPos.xyz /= lsPos.w;
    if(lsPos.z < 0.0 || lsPos.z > 1.0) return 1.0;
    vec2 uv = lsPos.xy * 0.5 + 0.5;
    if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float bias = max(0.005 * (1.0 - dot(normalize(inN), normalize(lightDir.xyz))), 0.002);
    for(int x = -1; x <= 1; x++) {
        for(int y = -1; y <= 1; y++) {
            float d = texture(shadowMap, uv + vec2(x, y) * texelSize).r;
            shadow += (lsPos.z - bias) > d ? 0.5 : 1.0;
        }
    }
    return shadow / 9.0;
}

float D_GGX(float NdotH, float rough) {
    float a  = rough * rough;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_Schlick(float NdotV, float k) {
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float rough) {
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return G_Schlick(max(NdotV, 0.0), k) * G_Schlick(max(NdotL, 0.0), k);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    /* ----- material properties ----- */
    vec4  albedo4    = ((flags & 1) != 0) ? texture(albedoMap, inUV) : baseColor;
    vec3  albedo     = pow(albedo4.rgb, vec3(2.2)); /* sRGB -> linear */
    float metal      = metallic;
    float rough      = roughness;
    float ao         = 1.0;

    if((flags & 4) != 0) {
        vec3 orm = texture(ormMap, inUV).rgb;
        ao    = orm.r;
        rough = orm.g;
        metal = orm.b;
    }

    /* ----- normal ----- */
    vec3 N;
    if((flags & 2) != 0) {
        vec3 tn = texture(normalMap, inUV).rgb * 2.0 - 1.0;
        N = normalize(mat3(inT, inB, inN) * tn);
    } else {
        N = normalize(inN);
    }

    /* ----- quick mode overrides ----- */
    if(renderMode == 2) {           /* normal visualisation */
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    /* ----- cook-torrance PBR ----- */
    vec3 V   = normalize(camPos.xyz - inWorldPos);
    vec3 L   = normalize(lightDir.xyz);
    vec3 H   = normalize(V + L);
    float intensity = lightDir.w;

    if(renderMode == 1) {           /* solid — Blinn-Phong, baseColor only */
        float NdotL = max(dot(N, L), 0.0);
        float spec  = pow(max(dot(N, H), 0.0), 32.0) * 0.35;
        vec3 col    = baseColor.rgb;
        float ambStr = 0.005 + 0.007 * intensity;
        vec3 color  = col * ambStr
                    + col * NdotL * intensity * lightColor.rgb
                    + vec3(spec)  * intensity * lightColor.rgb;
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
        outColor = vec4(color, baseColor.a);
        return;
    }

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metal);

    float D = D_GGX(NdotH, max(rough, 0.04));
    float G = G_Smith(NdotV, NdotL, max(rough, 0.04));
    vec3  F = F_Schlick(HdotV, F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metal);

    vec3 specular  = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 diffuse   = kD * albedo / PI;
    vec3 radiance  = lightColor.rgb * intensity;
    vec3 direct    = (diffuse + specular) * radiance * NdotL;

    /* ambient scales with sun intensity — dim light → dim ambient */
    vec3 ambient = (0.005 + 0.007 * intensity) * albedo * ao;

    /* emissive */
    vec3 emissive = vec3(0.0);
    if((flags & 8) != 0)
        emissive = texture(emissiveMap, inUV).rgb;

    vec3 color = ambient + direct * shadowFactor(inWorldPos) + emissive;

    /* tone map (Reinhard) + gamma */
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, albedo4.a);
}
