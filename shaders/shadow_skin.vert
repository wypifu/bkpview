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
    vec4 extra;
    vec4 planColor;
};

#define MAX_JOINTS 128
layout(set = 2, binding = 0) uniform JointUBO {
    mat4 joints[MAX_JOINTS];
};

layout(push_constant) uniform MeshPC {
    mat4  model;
    vec4  baseColor;
    float metallic;
    float roughness;
    int   flags;
    int   renderMode;
};

void main() {
    mat4 skinMat =
        inWeights.x * joints[inJoints.x] +
        inWeights.y * joints[inJoints.y] +
        inWeights.z * joints[inJoints.z] +
        inWeights.w * joints[inJoints.w];

    gl_Position = lightVP * model * skinMat * vec4(inPos, 1.0);
}
