#version 450

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec3  inUV;
layout(location = 3) in vec3  inTangent;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4  inWeights;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 camPos;
    vec4 lightDir;
    vec4 lightColor;
    mat4 lightVP;
    vec4 extra;       /* x=shadowEnabled(0=off,1=plan only,2=plan+model), y=planMode(0=solid,1=checker), z=checkerScale */
    vec4 planColor;   /* xyz = plan color */
};

#define MAX_JOINTS 128
layout(set = 2, binding = 0) uniform JointUBO {
    mat4 joints[MAX_JOINTS];
};

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(push_constant) uniform MeshPC {
    mat4  model;
    vec4  baseColor;
    float metallic;
    float roughness;
    int   flags;
    int   renderMode;
};

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outT;
layout(location = 3) out vec3 outB;
layout(location = 4) out vec3 outN;

void main() {
    mat4 skinMat =
        inWeights.x * joints[inJoints.x] +
        inWeights.y * joints[inJoints.y] +
        inWeights.z * joints[inJoints.z] +
        inWeights.w * joints[inJoints.w];

    vec4 localPos = skinMat * vec4(inPos, 1.0);
    vec4 worldPos = model * localPos;
    outWorldPos   = worldPos.xyz;
    outUV         = inUV.xy;

    mat3 normalMat = transpose(inverse(mat3(model * skinMat)));
    vec3 N = normalize(normalMat * inNormal);
    vec3 T = normalize(normalMat * inTangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    outT = T;
    outB = B;
    outN = N;

    gl_Position = proj * view * worldPos;
}
