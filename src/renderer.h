#pragma once

#include <stdint.h>
#include <vector>

extern "C" {
#include <include/blukpast.h>
#include <gfx/vulkan/include/vk_pipeline.h>
#include <loaders/include/bkp_loaders.h>
}

#include "camera.h"
#include "animator.h"

/* ---- GPU-side per-frame uniform buffer -------------------------------- */
struct alignas(16) FrameUBO {
    BkpMat4 view;
    BkpMat4 proj;
    BkpVec4 camPos;       /* w unused */
    BkpVec4 lightDir;     /* w = intensity */
    BkpVec4 lightColor;   /* w = gridY (aabbMin.y) */
    BkpMat4 lightVP;
    BkpVec4 extra;        /* x=shadowEnabled(0/1/2), y=planMode(0=solid,1=checker), z=checkerScale */
    BkpVec4 planColor;    /* xyz = plan color */
};

/* ---- AABB wireframe push constants (112 bytes) ------------------------ */
struct AabbPC {
    float model[16];   /* BkpMat4 bytes = correct GLSL mat4 */
    float boxMin[4];   /* xyz + pad */
    float boxMax[4];   /* xyz + pad */
    float color[4];
};

/* ---- push constants (96 bytes) ---------------------------------------- */
struct MeshPC {
    float   model[16];
    float   baseColor[4];
    float   metallic;
    float   roughness;
    int32_t flags;         /* bit0=albedo,bit1=normal,bit2=orm,bit3=emissive */
    int32_t renderMode;    /* 0=PBR, 1=flat, 2=normals */
};

/* ---- settings --------------------------------------------------------- */
enum class RenderMode { PBR = 0, Flat = 1, Normals = 2 };

struct LightParams {
    float azimuth   =  0.8f;
    float elevation =  0.8f;
    float intensity =  3.5f;
    float color[3]  = {1.0f, 0.98f, 0.95f};
};

/* ---- per-material GPU handle ------------------------------------------ */
struct MatGPU { VkDescriptorSet ds = VK_NULL_HANDLE; };

/* ---- Renderer --------------------------------------------------------- */
struct Renderer {
    static constexpr uint32_t MAX_FRAMES = 2;
    static constexpr uint32_t MAX_JOINTS = 128;
    static constexpr uint32_t SHADOW_DIM = 2048;

    /* --- static (non-skinned) PBR pipeline --- */
    BkpPipelineGraphic pbrPipeline    = {};
    /* --- LBS skinned PBR pipeline (pbr_skin.vert) --- */
    BkpPipelineGraphic pbrPipelineLBS = {};
    /* --- grid pipeline --- */
    BkpPipelineGraphic gridPipeline   = {};
    /* --- plan pipeline (reuses gridVert + planFrag) --- */
    BkpPipelineGraphic planPipeline   = {};
    /* --- shadow pipelines (depth-only) --- */
    BkpPipelineGraphic shadowPipeline     = {};  /* BkpVertex stride     */
    BkpPipelineGraphic shadowPipelineSkin = {};  /* BkpVertexSkin stride */
    /* --- AABB wireframe pipeline --- */
    BkpPipelineGraphic aabbPipeline = {};

    /* shaders */
    BkpShaderModule  pbrVert  = {}, pbrFrag     = {};
    BkpShaderModule  skinVert = {};
    BkpShaderModule  gridVert = {}, gridFrag     = {};
    BkpShaderModule  planFrag = {};
    BkpShaderModule  shadowVert = {}, shadowSkinVert = {}, shadowFrag = {};
    BkpShaderModule  aabbVert = {}, aabbFrag = {};

    BkpShaderProgram pbrProg    = {};
    BkpShaderProgram skinProg   = {};
    BkpShaderProgram gridProg   = {};
    BkpShaderProgram planProg   = {};
    BkpShaderProgram shadowProg = {}, shadowSkinProg = {};
    BkpShaderProgram aabbProg   = {};

    /* descriptor pool */
    BkpDescriptorPool descPool = {};

    /* per-frame UBO + frame descriptor sets (set 0: UBO + shadowMap) */
    BkpBufferGPU    frameUbo[MAX_FRAMES]    = {};
    VkDescriptorSet frameDSet[MAX_FRAMES]   = {};

    /* shadow descriptor sets (set 0: UBO only, for shadow pass) */
    VkDescriptorSet shadowDSet[MAX_FRAMES]  = {};

    /* joint UBO + descriptor sets (set 2: joint matrices) */
    BkpBufferGPU    jointUbo[MAX_FRAMES]    = {};
    VkDescriptorSet jointDSet[MAX_FRAMES]   = {};

    /* shadow map */
    BkpImageResource shadowMapImg = {};
    BkpSampler       shadowSmp    = {};

    /* depth buffer (main scene) */
    BkpImageResource depth = {};

    /* default 1×1 textures */
    BkpImageResource defAlbedo = {}, defNormal = {}, defORM = {}, defEmissive = {};
    BkpSampler       sampler   = {};

    /* per-material descriptor sets */
    std::vector<MatGPU> matGPU;

    /* scene AABB */
    BkpVec3 aabbMin = {}, aabbMax = {};

    /* settings */
    RenderMode            renderMode    = RenderMode::PBR;
    bool                  showGrid      = true;
    VkSampleCountFlagBits msaaSamples   = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits maxMsaaSamples= VK_SAMPLE_COUNT_1_BIT;
    LightParams light;
    float       flatColor[3] = {0.72f, 0.72f, 0.72f};

    bool  showPlan     = true;
    bool  planChecker  = false;
    float planColor[3] = {0.35f, 0.35f, 0.35f};
    float checkerScale = 1.0f;

    bool showShadow    = true;
    bool shadowOnModel = true;

    bool showSceneAabb = false;
    bool showMeshAabb  = false;

    /* ---- public API ---- */
    void init        (BkpGpuAdapter adp, VkFormat colorFmt);
    void handleResize(BkpGpuAdapter adp);
    void loadModel   (BkpGpuAdapter adp, BkpModel* model);
    void unloadModel (BkpGpuAdapter adp, BkpModel* model);
    void setMSAA     (BkpGpuAdapter adp, VkSampleCountFlagBits samples);

    /* Call before the color pass (transitions shadow map, runs shadow pass) */
    void drawShadow(BkpGpuAdapter adp, VkCommandBuffer cmd,
                    uint32_t frame, BkpModel* model,
                    const BkpMat4& rootTransform, Animator* anim);

    /* Call inside vkCmdBeginRendering */
    void draw(BkpGpuAdapter adp, VkCommandBuffer cmd,
              uint32_t frame, float aspect,
              BkpModel* model, Camera& cam,
              const BkpMat4& rootTransform, Animator* anim);

    VkRenderingAttachmentInfo depthAttachment()                      const;
    VkRenderingAttachmentInfo buildColorAttachment(VkImageView view) const;
    void addMsaaBarrier(VkCommandBuffer cmd)                         const;

    void cleanup(BkpGpuAdapter adp);

private:
    VkDevice                _dev    = VK_NULL_HANDLE;
    VkAllocationCallbacks * _alloc  = nullptr;
    VkFormat                _colorFmt = VK_FORMAT_UNDEFINED;

    BkpImageResource _msaa = {};

    bool _shadowMapReady = false;   /* true after first UNDEFINED→READ_ONLY transition */

    BkpMat4 _lightVP = {};
    std::vector<BkpMat4> _worldTransforms;

    void _createMsaaBuffer (BkpGpuAdapter adp, uint32_t w, uint32_t h);
    void _destroyMsaaBuffer(BkpGpuAdapter adp);
    void _destroyPipelines (BkpGpuAdapter adp);
    void _createPipelines  (BkpGpuAdapter adp, VkFormat colorFmt);
    void _createDefaultTextures(BkpGpuAdapter adp);
    void _bindMaterialDS   (BkpGpuAdapter adp, uint32_t matIdx, BkpModel* model);
    void _allocFrameDS     (BkpGpuAdapter adp);
    void _rebuildDescPool  (BkpGpuAdapter adp, uint32_t matCapacity);

    uint32_t _poolMatCapacity = 0;

    BkpMat4 _computeLightVP() const;
    void _buildWorldTransforms(BkpModel* model, int nodeIdx, const BkpMat4& parent);
    void _uploadJointMatrices (BkpGpuAdapter adp, uint32_t frame,
                               BkpModel* model, Animator* anim);

    void _drawNode      (VkCommandBuffer cmd, BkpModel* model, int nodeIdx, uint32_t frame);
    void _drawNodeShadow(VkCommandBuffer cmd, BkpModel* model, int nodeIdx, uint32_t frame);
};
