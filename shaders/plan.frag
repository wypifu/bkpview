#version 450

layout(location = 0) in vec3 inNear;
layout(location = 1) in vec3 inFar;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 lightDir;
    vec4 lightColor;  /* w = gridY */
    mat4 lightVP;
    vec4 extra;       /* x=shadowEnabled, y=planMode(0=solid 1=checker), z=checkerScale */
    vec4 planColor;
};
layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(location = 0) out vec4 outColor;

float shadowFactor(vec3 worldPos) {
    if(extra.x < 0.5) return 1.0;
    vec4 lsPos = lightVP * vec4(worldPos, 1.0);
    lsPos.xyz /= lsPos.w;
    if(lsPos.z < 0.0 || lsPos.z > 1.0) return 1.0;
    vec2 uv = lsPos.xy * 0.5 + 0.5;
    if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float bias = 0.001;
    for(int x = -1; x <= 1; x++) {
        for(int y = -1; y <= 1; y++) {
            float d = texture(shadowMap, uv + vec2(x, y) * texelSize).r;
            shadow += (lsPos.z - bias) > d ? 0.5 : 1.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    float dY = inFar.y - inNear.y;
    if(abs(dY) < 1e-6) discard;
    float gridY = lightColor.w;
    float t = (gridY - inNear.y) / dY;
    if(t < 0.0) discard;
    vec3 pos = inNear + t * (inFar - inNear);

    vec4 clip    = proj * view * vec4(pos, 1.0);
    gl_FragDepth = clip.z / clip.w;

    float dist = length(pos.xz - camPos.xz);
    float fade = max(1.0 - dist / 80.0, 0.0);
    fade = fade * fade;
    if(fade < 0.01) discard;

    vec3 color;
    if(int(extra.y) == 1) {
        float scale = max(extra.z, 0.1);
        vec2  c2    = floor(pos.xz / scale);
        float check = mod(c2.x + c2.y, 2.0);
        color = mix(planColor.rgb, planColor.rgb * 0.45, check);
    } else {
        color = planColor.rgb;
    }

    color *= shadowFactor(pos);
    outColor = vec4(color, fade);
}
