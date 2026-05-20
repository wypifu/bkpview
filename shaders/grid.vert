#version 450

/* Infinite grid on Y=0 plane via NDC fullscreen quad + ray / plane intersection.
   No vertex buffer needed — positions generated from gl_VertexIndex. */

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

layout(location = 0) out vec3 outNear;
layout(location = 1) out vec3 outFar;

const vec3 kQuad[6] = vec3[6](
    vec3(-1.0, -1.0, 0.0),
    vec3( 1.0, -1.0, 0.0),
    vec3( 1.0,  1.0, 0.0),
    vec3(-1.0, -1.0, 0.0),
    vec3( 1.0,  1.0, 0.0),
    vec3(-1.0,  1.0, 0.0)
);

vec3 unproject(float x, float y, float z) {
    mat4 invView = inverse(view);
    mat4 invProj = inverse(proj);
    vec4 p = invView * invProj * vec4(x, y, z, 1.0);
    return p.xyz / p.w;
}

void main() {
    vec3 ndc = kQuad[gl_VertexIndex];
    outNear  = unproject(ndc.x, ndc.y, 0.0);
    outFar   = unproject(ndc.x, ndc.y, 1.0);
    gl_Position = vec4(ndc.xy, 0.0, 1.0);
}
