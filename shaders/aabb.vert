#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 lightDir;
    vec4 lightColor;
    mat4 lightVP;
    vec4 extra;
    vec4 planColor;
};

layout(push_constant) uniform PC {
    mat4 model;
    vec4 boxMin;
    vec4 boxMax;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

/* 12 edges × 2 endpoints, corners encoded as bitmask (bit0=X, bit1=Y, bit2=Z) */
const int kEdge[24] = int[24](
    0, 1,  2, 3,  4, 5,  6, 7,   /* X-parallel edges */
    0, 2,  1, 3,  4, 6,  5, 7,   /* Y-parallel edges */
    0, 4,  1, 5,  2, 6,  3, 7    /* Z-parallel edges */
);

void main() {
    int ci = kEdge[gl_VertexIndex];
    vec3 p;
    p.x = (ci & 1) != 0 ? pc.boxMax.x : pc.boxMin.x;
    p.y = (ci & 2) != 0 ? pc.boxMax.y : pc.boxMin.y;
    p.z = (ci & 4) != 0 ? pc.boxMax.z : pc.boxMin.z;
    gl_Position = proj * view * pc.model * vec4(p, 1.0);
    outColor = pc.color;
}
