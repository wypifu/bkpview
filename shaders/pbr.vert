#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inUV;       /* z unused, matches BkpVertex.uv */
layout(location = 3) in vec3 inTangent;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;        /* w unused */
    vec4 lightDir;      /* w = intensity */
    vec4 lightColor;    /* w = gridY */
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
    int     renderMode;  /* 0=pbr, 1=flat, 2=normals */
};

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outT;
layout(location = 3) out vec3 outB;
layout(location = 4) out vec3 outN;

void main() {
    vec4 worldPos = model * vec4(inPos, 1.0);
    outWorldPos   = worldPos.xyz;
    outUV         = inUV.xy;

    mat3 normalMat = transpose(inverse(mat3(model)));
    vec3 N = normalize(normalMat * inNormal);
    vec3 T = normalize(normalMat * inTangent);
    T = normalize(T - dot(T, N) * N);  /* re-orthogonalize */
    vec3 B = cross(N, T);

    outT = T;
    outB = B;
    outN = N;

    gl_Position = proj * view * worldPos;
}
