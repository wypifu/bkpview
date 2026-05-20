#version 450

layout(location = 0) in vec3 inPos;

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

layout(push_constant) uniform MeshPC {
    mat4  model;
    vec4  baseColor;
    float metallic;
    float roughness;
    int   flags;
    int   renderMode;
};

void main() {
    gl_Position = lightVP * model * vec4(inPos, 1.0);
}
