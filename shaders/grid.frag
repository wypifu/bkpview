#version 450

layout(location = 0) in vec3 inNear;
layout(location = 1) in vec3 inFar;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 lightDir;    /* xyz = direction (toward light), w = intensity */
    vec4 lightColor;  /* xyz = color, w = gridY */
    mat4 lightVP;
    vec4 extra;       /* x=shadowEnabled(0=off,1=plan only,2=plan+model), y=planMode(0=solid,1=checker), z=checkerScale */
    vec4 planColor;   /* xyz = plan color */
};
layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(location = 0) out vec4 outColor;

float gridLine(vec2 pos, float cellSize) {
    vec2 coord = pos / cellSize;
    vec2 g     = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    /* ray-plane intersection: Y = gridY (passed via lightColor.w) */
    float dY = inFar.y - inNear.y;
    if(abs(dY) < 1e-6) discard;

    float gridY = lightColor.w;
    float t = (gridY - inNear.y) / dY;
    if(t < 0.0) discard;

    vec3 pos = inNear + t * (inFar - inNear);

    /* write correct depth so grid is occluded by meshes */
    vec4 clip    = proj * view * vec4(pos, 1.0);
    gl_FragDepth = clip.z / clip.w;

    /* distance-based fade */
    float dist  = length(pos.xz - camPos.xz);
    float fade  = max(1.0 - dist / 100.0, 0.0);
    fade        = fade * fade;

    /* two scales: 1m cells + 0.1m sub-grid */
    float major = gridLine(pos.xz, 1.0);
    float minor = gridLine(pos.xz, 0.1) * 0.4;
    float g     = max(major, minor);

    vec3 color = vec3(0.45);

    /* axis highlight */
    float aw = fwidth(pos.x) * 1.5;
    if(abs(pos.x) < aw) color = vec3(0.25, 0.25, 0.85); /* Z-axis blue  */
    aw = fwidth(pos.z) * 1.5;
    if(abs(pos.z) < aw) color = vec3(0.85, 0.25, 0.25); /* X-axis red   */

    /* subtle shadow on grid lines */
    if(extra.x >= 0.5) {
        vec4 lsPos = lightVP * vec4(pos, 1.0);
        lsPos.xyz /= lsPos.w;
        if(lsPos.z >= 0.0 && lsPos.z <= 1.0) {
            vec2 suv = lsPos.xy * 0.5 + 0.5;
            if(suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0) {
                float d = texture(shadowMap, suv).r;
                if(lsPos.z > d + 0.001) color *= 0.75;
            }
        }
    }

    outColor = vec4(color, g * fade);
    if(outColor.a < 0.005) discard;
}
