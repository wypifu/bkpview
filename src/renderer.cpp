#include "renderer.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>

extern "C" {
#include <gfx/vulkan/include/vulkan.h>
#include <gfx/vulkan/include/pipelines_helper.h>
#include <gfx/vulkan/include/vk_pipeline.h>
#include <gfx/vulkan/include/vk_cmd.h>
#include <gfx/vulkan/include/vkAllocator.h>
#include <include/type.h>
#include <system/include/bkp_path.h>
}

/* ---- helpers ---------------------------------------------------------- */


static void make1x1(BkpGpuAdapter adp, BkpImageResource* res,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t px[4] = {r, g, b, a};
    uint8_t* ptr  = px;
    bkpSetDefaultTextureInfo(res);
    res->mipType              = eMIPMAP_NONE;
    res->imageInfo.mipLevels  = 1;          /* fix: 1×1 can't have 8 mip levels */
    bkpCreateTextureLayersFromData(adp, &ptr, 1, 1, 1, 1, res);
}



/* ---- pipeline builder ------------------------------------------------- */

static BkpPipelineGraphic makePipeline(
    BkpGpuAdapter adp, BkpShaderProgram* prog,
    VkFormat colorFmt,                                 /* VK_FORMAT_UNDEFINED = depth-only */
    VkVertexInputBindingDescription*    binding,   uint32_t bindingCount,
    VkVertexInputAttributeDescription*  attrs,     uint32_t attrCount,
    VkPolygonMode polyMode,
    VkBool32 depthTest, VkBool32 depthWrite, VkCompareOp depthOp,
    bool blend,
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    bool dynamicCull = false,
    BkpPipelineLayout* sharedLayout = nullptr)
{
    BkpPipelineGraphic ppl = {};
    VkDynamicState dynSt[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    ppl.info.shaderProgram                     = prog;
    ppl.info.vertexLayout.vertexBindings       = binding;
    ppl.info.vertexLayout.vertexBindingCount   = bindingCount;
    ppl.info.vertexLayout.vertexAttributes     = attrs;
    ppl.info.vertexLayout.vertexAttributeCount = attrCount;

    ppl.info.inputAssembly = bkpDefaultPipelineInputAssembly(topology);
    ppl.info.viewportState = bkpDefaultPipelineViewport(nullptr, 1, nullptr, 1);
    ppl.info.rasterizer    = bkpDefaultPipelineRasterization(polyMode, cullMode,
                                VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
    ppl.info.multisampler  = bkpDefaultPipelineMultisample(VK_FALSE, samples, 1.0f);
    ppl.info.depthStencil  = bkpDefaultPipelineDepthStenscil(depthTest, depthWrite, depthOp);
    ppl.info.depthStencilEnabled  = VK_TRUE;
    ppl.info.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    if(colorFmt != VK_FORMAT_UNDEFINED) {
        ppl.info.colorAttachmentFormat = colorFmt;
        VkPipelineColorBlendAttachmentState ba = bkpDefaultPipelineColorBlendAttachement();
        if(blend) {
            ba.blendEnable         = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp        = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        ppl.info.colorAttachments[0]  = ba;
        ppl.info.colorAttachmentCount = 1;
        ppl.info.colorBlending = bkpDefaultPipelineColorBlend(ppl.info.colorAttachments, 1);
    } else {
        ppl.info.colorAttachmentCount = 0;
        ppl.info.colorBlending = bkpDefaultPipelineColorBlend(nullptr, 0);
    }

    ppl.info.dynamicStates[0] = dynSt[0];
    ppl.info.dynamicStates[1] = dynSt[1];
    uint32_t dynCount = 2;
    if(dynamicCull)
        ppl.info.dynamicStates[dynCount++] = VK_DYNAMIC_STATE_CULL_MODE;
    ppl.info.dynamicState        = bkpDefaultDynamicStates(ppl.info.dynamicStates, dynCount);
    ppl.info.dynamicStateEnabled = VK_TRUE;

    if(sharedLayout)
        ppl.pipelineLayout = *sharedLayout;
    else
        bkpCreatePipelineLayoutFromShader(adp, prog, &ppl.pipelineLayout);
    ppl.pipelineCache.info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    bkpCreatePipelineCache(adp, &ppl.pipelineCache);
    bkpCreatePipelineGraphic(adp, &ppl);
    return ppl;
}

/* ======================================================================
   public API
   ====================================================================== */

void Renderer::init(BkpGpuAdapter adp, VkFormat colorFmt) {
    _dev      = adp->device;
    _alloc    = adp->allocator;
    _colorFmt = colorFmt;

    VkSampleCountFlags c = adp->deviceProperties.limits.framebufferColorSampleCounts
                         & adp->deviceProperties.limits.framebufferDepthSampleCounts;
    if(c & VK_SAMPLE_COUNT_16_BIT)     maxMsaaSamples = VK_SAMPLE_COUNT_16_BIT;
    else if(c & VK_SAMPLE_COUNT_8_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_8_BIT;
    else if(c & VK_SAMPLE_COUNT_4_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_4_BIT;
    else if(c & VK_SAMPLE_COUNT_2_BIT) maxMsaaSamples = VK_SAMPLE_COUNT_2_BIT;
    else                               maxMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

    aabbMin = bkpVec3(0.0f, 0.0f, 0.0f);
    aabbMax = bkpVec3(0.0f, 0.0f, 0.0f);

    _createPipelines(adp, colorFmt);

    /* descriptor pool — accounts for frame/shadow/joint DSes + material DSes */
    {
        VkDescriptorPoolSize poolSz[2] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_FRAMES * 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES + 256 * 4},
        };
        descPool.info = bkpDefaultDescriptorPool(poolSz, 2, MAX_FRAMES * 3 + 256);
        descPool.info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        bkpCreateDescriptorPool(adp, &descPool);
    }
    _poolMatCapacity = 256;

    /* shadow map */
    shadowMapImg = bkpDefaultDepthBuffer();
    shadowMapImg.imageInfo.extent = {SHADOW_DIM, SHADOW_DIM, 1};
    shadowMapImg.imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    bkpCreateDepthResources(adp, &shadowMapImg);

    shadowSmp.info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadowSmp.info.magFilter    = VK_FILTER_NEAREST;
    shadowSmp.info.minFilter    = VK_FILTER_NEAREST;
    shadowSmp.info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadowSmp.info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSmp.info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSmp.info.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    shadowSmp.info.maxLod        = 1.0f;
    shadowSmp.info.maxAnisotropy = 1.0f;
    bkpCreateSampler(adp, &shadowSmp);

    /* per-frame UBOs */
    for(uint32_t i = 0; i < MAX_FRAMES; i++) {
        frameUbo[i].size  = sizeof(FrameUBO);
        frameUbo[i].count = 1;
        bkpCreateBuffersGPU(adp, &frameUbo[i],
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            eBUFFER_CPU_GPU, BKP_DO_MAP);

        jointUbo[i].size  = MAX_JOINTS * sizeof(BkpMat4);
        jointUbo[i].count = 1;
        bkpCreateBuffersGPU(adp, &jointUbo[i],
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            eBUFFER_CPU_GPU, BKP_DO_MAP);
    }
    _allocFrameDS(adp);

    /* scene sampler */
    sampler.info.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.info.magFilter     = VK_FILTER_LINEAR;
    sampler.info.minFilter     = VK_FILTER_LINEAR;
    sampler.info.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.info.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.info.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.info.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.info.maxLod        = VK_LOD_CLAMP_NONE;
    sampler.info.maxAnisotropy = 16.0f;
    bkpCreateSampler(adp, &sampler);

    _createDefaultTextures(adp);

    depth = bkpDefaultDepthBuffer();
    depth.imageInfo.extent  = {(uint32_t)adp->frameInfo.winWidth,
                               (uint32_t)adp->frameInfo.winHeight, 1};
    depth.imageInfo.samples = msaaSamples;
    bkpCreateDepthResources(adp, &depth);
}

void Renderer::handleResize(BkpGpuAdapter adp) {
    _destroyMsaaBuffer(adp);
    bkpDestroyImageResource(adp, &depth);

    uint32_t w = (uint32_t)adp->frameInfo.winWidth;
    uint32_t h = (uint32_t)adp->frameInfo.winHeight;

    depth = bkpDefaultDepthBuffer();
    depth.imageInfo.extent  = {w, h, 1};
    depth.imageInfo.samples = msaaSamples;
    bkpCreateDepthResources(adp, &depth);

    if((msaaSamples != VK_SAMPLE_COUNT_1_BIT)) _createMsaaBuffer(adp, w, h);
}

void Renderer::_createDefaultTextures(BkpGpuAdapter adp) {
    make1x1(adp, &defAlbedo,   255, 255, 255, 255);
    make1x1(adp, &defNormal,   128, 128, 255, 255);
    make1x1(adp, &defORM,      255, 128,   0, 255);
    make1x1(adp, &defEmissive,   0,   0,   0, 255);
}

void Renderer::_createPipelines(BkpGpuAdapter adp, VkFormat colorFmt)
{
    auto shaderPath = [](const char* relative) -> const char* {
#ifdef BKPVIEW_SHADER_DIR
        static char path[512];
        snprintf(path, sizeof(path), "%s/%s", BKPVIEW_SHADER_DIR, relative);
        return path;
#else
        return bkpExePath(relative);
#endif
    };
    /* shaders */
    bkpCreateShaderModule(adp, shaderPath("shaders/pbr.vert.spv"),      &pbrVert);
    bkpCreateShaderModule(adp, shaderPath("shaders/pbr.frag.spv"),      &pbrFrag);
    bkpCreateShaderModule(adp, shaderPath("shaders/pbr_skin.vert.spv"), &skinVert);
    bkpCreateShaderModule(adp, shaderPath("shaders/grid.vert.spv"),     &gridVert);
    bkpCreateShaderModule(adp, shaderPath("shaders/grid.frag.spv"),     &gridFrag);
    bkpCreateShaderModule(adp, shaderPath("shaders/plan.frag.spv"),     &planFrag);
    bkpCreateShaderModule(adp, shaderPath("shaders/shadow.vert.spv"),      &shadowVert);
    bkpCreateShaderModule(adp, shaderPath("shaders/shadow_skin.vert.spv"), &shadowSkinVert);
    bkpCreateShaderModule(adp, shaderPath("shaders/shadow.frag.spv"),      &shadowFrag);
    bkpCreateShaderModule(adp, shaderPath("shaders/aabb.vert.spv"),     &aabbVert);
    bkpCreateShaderModule(adp, shaderPath("shaders/aabb.frag.spv"),     &aabbFrag);

    BkpShaderModule* pbrMods[]    = {&pbrVert,    &pbrFrag};
    BkpShaderModule* skinMods[]   = {&skinVert,   &pbrFrag};
    BkpShaderModule* gridMods[]   = {&gridVert,   &gridFrag};
    BkpShaderModule* planMods[]   = {&gridVert,   &planFrag};
    BkpShaderModule* shadowMods[]     = {&shadowVert,     &shadowFrag};
    BkpShaderModule* shadowSkinMods[] = {&shadowSkinVert, &shadowFrag};
    BkpShaderModule* aabbMods[]       = {&aabbVert,       &aabbFrag};

    bkpCreateShader(adp, pbrMods,         2, &pbrProg);
    bkpCreateShader(adp, skinMods,         2, &skinProg);
    bkpCreateShader(adp, gridMods,         2, &gridProg);
    bkpCreateShader(adp, planMods,         2, &planProg);
    bkpCreateShader(adp, shadowMods,       2, &shadowProg);
    bkpCreateShader(adp, shadowSkinMods,   2, &shadowSkinProg);
    bkpCreateShader(adp, aabbMods,         2, &aabbProg);

    /* vertex attributes for BkpVertex (4 attrs) */
    VkVertexInputAttributeDescription attrs[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(BkpVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(BkpVertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(BkpVertex, uv)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(BkpVertex, tangent)},
    };

    /* vertex attributes for BkpVertexSkin (6 attrs) */
    VkVertexInputAttributeDescription skinAttrs[6] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(BkpVertexSkin, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(BkpVertexSkin, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(BkpVertexSkin, uv)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT,    (uint32_t)offsetof(BkpVertexSkin, tangent)},
        {4, 0, VK_FORMAT_R32G32B32A32_UINT,   (uint32_t)offsetof(BkpVertexSkin, joints)},
        {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(BkpVertexSkin, weights)},
    };

    VkVertexInputBindingDescription binding     = {0, (uint32_t)sizeof(BkpVertex),     VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputBindingDescription bindingSkin = {0, (uint32_t)sizeof(BkpVertexSkin), VK_VERTEX_INPUT_RATE_VERTEX};

    /* shadow only needs position */
    VkVertexInputAttributeDescription shadowAttr = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};

    pbrPipeline    = makePipeline(adp, &pbrProg, colorFmt, &binding, 1, attrs, 4,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
                        false, VK_CULL_MODE_BACK_BIT, msaaSamples,
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true);

    pbrPipelineBlend = makePipeline(adp, &pbrProg, colorFmt, &binding, 1, attrs, 4,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS,
                        true, VK_CULL_MODE_BACK_BIT, msaaSamples,
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true,
                        &pbrPipeline.pipelineLayout);

    pbrPipelineLBS = makePipeline(adp, &skinProg, colorFmt, &bindingSkin, 1, skinAttrs, 6,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
                        false, VK_CULL_MODE_BACK_BIT, msaaSamples,
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true);

    pbrPipelineLBSBlend = makePipeline(adp, &skinProg, colorFmt, &bindingSkin, 1, skinAttrs, 6,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS,
                        true, VK_CULL_MODE_BACK_BIT, msaaSamples,
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true,
                        &pbrPipelineLBS.pipelineLayout);

    gridPipeline   = makePipeline(adp, &gridProg, colorFmt, nullptr, 0, nullptr, 0,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL,
                        true, VK_CULL_MODE_NONE, msaaSamples);

    planPipeline   = makePipeline(adp, &planProg, colorFmt, nullptr, 0, nullptr, 0,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL,
                        true, VK_CULL_MODE_NONE, msaaSamples);

    /* depth-only shadow pipelines */
    shadowPipeline = makePipeline(adp, &shadowProg,
                        VK_FORMAT_UNDEFINED, &binding, 1, &shadowAttr, 1,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
                        false, VK_CULL_MODE_BACK_BIT);

    shadowPipelineSkin = makePipeline(adp, &shadowSkinProg,
                        VK_FORMAT_UNDEFINED, &bindingSkin, 1, skinAttrs, 6,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS,
                        false, VK_CULL_MODE_BACK_BIT);

    aabbPipeline = makePipeline(adp, &aabbProg, colorFmt, nullptr, 0, nullptr, 0,
                        VK_POLYGON_MODE_FILL, VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL,
                        false, VK_CULL_MODE_NONE, msaaSamples,
                        VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
}

/* ---- model loading ---------------------------------------------------- */

void Renderer::_allocFrameDS(BkpGpuAdapter adp) {
    for(uint32_t i = 0; i < MAX_FRAMES; i++) {
        VkBuffer     vkBuf; VkDeviceSize off;
        bkpGetBuffer(frameUbo[i].buffer, &vkBuf);
        bkpGetBufferOffset(frameUbo[i].buffer, &off);

        bkpAllocDescriptorSet(adp, &descPool, pbrProg.layouts[0].layout, &frameDSet[i]);
        bkpWriteDescriptorBuffer(adp, frameDSet[i], 0, vkBuf, off, sizeof(FrameUBO));
        bkpWriteDescriptorImage(adp, frameDSet[i], 1,
                                shadowSmp.sampler, shadowMapImg.imageViews[0],
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        bkpAllocDescriptorSet(adp, &descPool, shadowProg.layouts[0].layout, &shadowDSet[i]);
        bkpWriteDescriptorBuffer(adp, shadowDSet[i], 0, vkBuf, off, sizeof(FrameUBO));

        VkBuffer     jBuf; VkDeviceSize jOff;
        bkpGetBuffer(jointUbo[i].buffer, &jBuf);
        bkpGetBufferOffset(jointUbo[i].buffer, &jOff);

        bkpAllocDescriptorSet(adp, &descPool, skinProg.layouts[2].layout, &jointDSet[i]);
        bkpWriteDescriptorBuffer(adp, jointDSet[i], 0, jBuf, jOff, MAX_JOINTS * sizeof(BkpMat4));
    }
}

void Renderer::_rebuildDescPool(BkpGpuAdapter adp, uint32_t matCapacity) {
    for(uint32_t i = 0; i < MAX_FRAMES; i++) {
        if(frameDSet[i]  != VK_NULL_HANDLE) bkpFreeDescriptorSet(adp, &descPool, frameDSet[i]);
        if(shadowDSet[i] != VK_NULL_HANDLE) bkpFreeDescriptorSet(adp, &descPool, shadowDSet[i]);
        if(jointDSet[i]  != VK_NULL_HANDLE) bkpFreeDescriptorSet(adp, &descPool, jointDSet[i]);
        frameDSet[i] = shadowDSet[i] = jointDSet[i] = VK_NULL_HANDLE;
    }
    bkpDestroyDescriptorPool(adp, &descPool);

    VkDescriptorPoolSize poolSz[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_FRAMES * 3},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES + matCapacity * 4},
    };
    descPool.info = bkpDefaultDescriptorPool(poolSz, 2, MAX_FRAMES * 3 + matCapacity);
    descPool.info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    bkpCreateDescriptorPool(adp, &descPool);

    _allocFrameDS(adp);
    _poolMatCapacity = matCapacity;
}

void Renderer::loadModel(BkpGpuAdapter adp, BkpModel* model) {
    fprintf(stdout, "  [R] free old DS\n"); fflush(stdout);
    for(auto& m : matGPU)
        if(m.ds != VK_NULL_HANDLE)
            bkpFreeDescriptorSet(adp, &descPool, m.ds);
    matGPU.clear();

    uint32_t n = std::max(model->materialCount, 1u);
    fprintf(stdout, "  [R] %u materials — pool cap %u\n", n, _poolMatCapacity); fflush(stdout);
    if(n > _poolMatCapacity) {
        uint32_t cap = _poolMatCapacity ? _poolMatCapacity : 256;
        while(cap < n) cap <<= 1;
        fprintf(stdout, "  [R] rebuildDescPool cap=%u\n", cap); fflush(stdout);
        _rebuildDescPool(adp, cap);
        fprintf(stdout, "  [R] rebuildDescPool done\n"); fflush(stdout);
    }

    matGPU.resize(n);
    fprintf(stdout, "  [R] binding %u material DS\n", n); fflush(stdout);
    for(uint32_t i = 0; i < n; i++)
        _bindMaterialDS(adp, i, model);
    fprintf(stdout, "  [R] all DS bound\n"); fflush(stdout);

    aabbMin = bkpVec3(model->aabbMin[0], model->aabbMin[1], model->aabbMin[2]);
    aabbMax = bkpVec3(model->aabbMax[0], model->aabbMax[1], model->aabbMax[2]);
}

void Renderer::_bindMaterialDS(BkpGpuAdapter adp, uint32_t idx, BkpModel* model) {
    MatGPU& m = matGPU[idx];
    bkpAllocDescriptorSet(adp, &descPool, pbrProg.layouts[1].layout, &m.ds);

    auto texView = [&](int ti, BkpImageResource* def) -> VkImageView {
        if(ti >= 0 && (uint32_t)ti < model->textureCount)
            return model->textures[ti].image.imageViews[0];
        return def->imageViews[0];
    };

    BkpMaterial*  mat = (idx < model->materialCount) ? &model->materials[idx] : nullptr;
    VkSampler     smp = sampler.sampler;
    VkImageLayout lay = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    bkpWriteDescriptorImage(adp, m.ds, 0, smp, texView(mat ? mat->albedoIdx            : -1, &defAlbedo),   lay);
    bkpWriteDescriptorImage(adp, m.ds, 1, smp, texView(mat ? mat->normalIdx            : -1, &defNormal),   lay);
    bkpWriteDescriptorImage(adp, m.ds, 2, smp, texView(mat ? mat->metallicRoughnessIdx : -1, &defORM),      lay);
    bkpWriteDescriptorImage(adp, m.ds, 3, smp, texView(mat ? mat->emissiveIdx          : -1, &defEmissive), lay);
}

void Renderer::unloadModel(BkpGpuAdapter adp, BkpModel* model) {
    for(auto& m : matGPU)
        if(m.ds != VK_NULL_HANDLE)
            bkpFreeDescriptorSet(adp, &descPool, m.ds);
    matGPU.clear();
    _worldTransforms.clear();
    bkpUnloadModel(adp, model);
}

/* ---- light VP --------------------------------------------------------- */

BkpMat4 Renderer::_computeLightVP() const {
    float ce = cosf(light.elevation), se = sinf(light.elevation);
    float ca = cosf(light.azimuth),   sa = sinf(light.azimuth);
    BkpVec3 ld = bkpVec3(ce*sa, se, ce*ca);
    ld = bkpNormalize3D(&ld);

    BkpVec3 center = bkpVec3(
        (aabbMin.x + aabbMax.x) * 0.5f,
        (aabbMin.y + aabbMax.y) * 0.5f,
        (aabbMin.z + aabbMax.z) * 0.5f);

    BkpVec3 ext = bkpVec3(aabbMax.x - aabbMin.x,
                           aabbMax.y - aabbMin.y,
                           aabbMax.z - aabbMin.z);
    float r    = bkpMagnitude3D(&ext) * 0.8f + 2.0f;
    float dist = r * 2.5f + 5.0f;

    BkpVec3 lightPos = bkpVec3(center.x + ld.x * dist,
                                center.y + ld.y * dist,
                                center.z + ld.z * dist);
    BkpVec3 up = bkpVec3(0.0f, 1.0f, 0.0f);
    if(fabsf(ld.y) > 0.98f) up = bkpVec3(0.0f, 0.0f, 1.0f);

    BkpMat4 lv = bkpLookAt(lightPos, center, up);
    BkpMat4 lp = bkpOrtho(-r, r, -r, r, 0.1f, dist * 2.5f);
    return bkpMat4DotMat4(&lp, &lv);
}

/* ---- world transform pre-pass ---------------------------------------- */

void Renderer::_buildWorldTransforms(BkpModel* model, int nodeIdx, const BkpMat4& parent) {
    if(nodeIdx < 0 || nodeIdx >= (int)model->nodeCount) return;
    BkpNode& node = model->nodes[nodeIdx];
    BkpMat4 local;
    memcpy(&local, node.localMatrix, sizeof(BkpMat4));
    _worldTransforms[(uint32_t)nodeIdx] = bkpMat4DotMat4(&parent, &local);

    for(uint32_t c = 0; c < node.childCount; c++)
        _buildWorldTransforms(model, node.children[c], _worldTransforms[(uint32_t)nodeIdx]);
}

void Renderer::_uploadJointMatrices(BkpGpuAdapter adp, uint32_t frame,
                                    BkpModel* model, Animator* /*anim*/) {
    if(!model || model->skinCount == 0) return;
    static BkpMat4 jmats[MAX_JOINTS];

    BkpSkin& skin = model->skins[0];
    uint32_t n = std::min(skin.jointCount, MAX_JOINTS);
    for(uint32_t i = 0; i < n; i++) {
        int ji = skin.joints[i];
        if(ji < 0 || ji >= (int)_worldTransforms.size()) {
            jmats[i] = bkpIdentityMat4(); continue;
        }
        const float* ibm = &skin.inverseBindMatrices[i * 16];
        BkpMat4 ibmMat;
        ibmMat.mLine0 = bkpVec4(ibm[0],  ibm[1],  ibm[2],  ibm[3]);
        ibmMat.mLine1 = bkpVec4(ibm[4],  ibm[5],  ibm[6],  ibm[7]);
        ibmMat.mLine2 = bkpVec4(ibm[8],  ibm[9],  ibm[10], ibm[11]);
        ibmMat.mLine3 = bkpVec4(ibm[12], ibm[13], ibm[14], ibm[15]);
        jmats[i] = bkpMat4DotMat4(&_worldTransforms[(uint32_t)ji], &ibmMat);
    }
    bkpUploadBufferData(adp, jointUbo[frame].buffer, jmats, 0, n * sizeof(BkpMat4));
}

/* ---- shadow pass ------------------------------------------------------ */

void Renderer::drawShadow(BkpGpuAdapter adp, VkCommandBuffer cmd,
                          uint32_t frame, BkpModel* model,
                          const BkpMat4& rootTransform, Animator* anim) {
    /* first call: transition shadow map from UNDEFINED → READ_ONLY so
       frameDSet binding 1 is in a valid state even when shadows are off */
    if(!_shadowMapReady)
    {
        BkpImageBarrierInfo b = {
            .image     = shadowMapImg.images[0],
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_NONE, .srcAccess = VK_ACCESS_2_NONE,
            .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccess = VK_ACCESS_2_SHADER_READ_BIT,
            .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1
        };
        bkpCmdBarrierImages(cmd, &b, 1);
        _shadowMapReady = true;
    }

    if(!showShadow || !model) return;

    /* pre-pass: compute world transforms (used by shadow draw + main draw) */
    if(anim && !anim->worldTransforms.empty())
    {
        _worldTransforms = anim->worldTransforms;
    }
    else
    {
        _worldTransforms.resize(model->nodeCount);
        for(uint32_t r = 0; r < model->rootNodeCount; r++)
            _buildWorldTransforms(model, model->rootNodes[r], rootTransform);
    }
    _uploadJointMatrices(adp, frame, model, anim);

    /* update FrameUBO with lightVP (needed for shadow pass viewproj) */
    _lightVP = _computeLightVP();

    /* READ_ONLY → DEPTH_ATTACHMENT */
    {
        BkpImageBarrierInfo b = {
            .image     = shadowMapImg.images[0],
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccess = VK_ACCESS_2_SHADER_READ_BIT,
            .dstStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1
        };
        bkpCmdBarrierImages(cmd, &b, 1);
    }

    VkRenderingAttachmentInfo shadowDepthAtt = {};
    shadowDepthAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    shadowDepthAtt.imageView   = shadowMapImg.imageViews[0];
    shadowDepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowDepthAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    shadowDepthAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    shadowDepthAtt.clearValue  = {.depthStencil = {1.0f, 0}};

    VkRenderingInfo ri = {};
    ri.sType             = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea        = {{0,0}, {SHADOW_DIM, SHADOW_DIM}};
    ri.layerCount        = 1;
    ri.pDepthAttachment  = &shadowDepthAtt;

    bkpCmdBeginRendering(cmd, &ri);

    VkViewport vp = {0, 0, (float)SHADOW_DIM, (float)SHADOW_DIM, 0.0f, 1.0f};
    VkRect2D   sc = {{0,0}, {SHADOW_DIM, SHADOW_DIM}};
    bkpCmdSetViewport(cmd, &vp);
    bkpCmdSetScissor (cmd, &sc);

    bkpCmdBindDescriptorSets(cmd, shadowPipeline.pipelineLayout.layout, 0, 1, &shadowDSet[frame]);

    for(uint32_t r = 0; r < model->rootNodeCount; r++)
        _drawNodeShadow(cmd, model, model->rootNodes[r], frame);

    bkpCmdEndRendering(cmd);

    /* DEPTH_ATTACHMENT → READ_ONLY */
    {
        BkpImageBarrierInfo b = {
            .image     = shadowMapImg.images[0],
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccess = VK_ACCESS_2_SHADER_READ_BIT,
            .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1
        };
        bkpCmdBarrierImages(cmd, &b, 1);
    }
}

void Renderer::_drawNodeShadow(VkCommandBuffer cmd, BkpModel* model,
                                int nodeIdx, uint32_t frame) {
    if(nodeIdx < 0 || nodeIdx >= (int)model->nodeCount) return;
    BkpNode& node = model->nodes[nodeIdx];

    for(uint32_t p = 0; p < node.meshCount; p++) {
        uint32_t mi = (uint32_t)node.meshIdx + p;
        if(mi >= model->meshCount) break;
        BkpModelMesh& mesh = model->meshes[mi];

        bool             isSkin = (mesh.isSkinned != 0);
        VkPipeline       pl  = isSkin ? shadowPipelineSkin.pipeline : shadowPipeline.pipeline;
        VkPipelineLayout lay = isSkin ? shadowPipelineSkin.pipelineLayout.layout
                                      : shadowPipeline.pipelineLayout.layout;

        bkpCmdBindPipeline(cmd, pl);
        bkpCmdBindDescriptorSets(cmd, lay, 0, 1, &shadowDSet[frame]);
        if(isSkin)
            bkpCmdBindDescriptorSets(cmd, lay, 2, 1, &jointDSet[frame]);

        MeshPC pc = {};
        if(mesh.isSkinned) {
            BkpMat4 ident = bkpIdentityMat4();
            memcpy(pc.model, &ident, sizeof(BkpMat4));
        } else {
            const BkpMat4& world = (nodeIdx < (int)_worldTransforms.size())
                                   ? _worldTransforms[(uint32_t)nodeIdx]
                                   : bkpIdentityMat4();
            memcpy(pc.model, &world, sizeof(BkpMat4));
        }
        bkpCmdPushConstants(cmd, lay, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPC), &pc);

        VkBuffer vkBuf; VkDeviceSize off;
        bkpGetBuffer(mesh.geo.buffer, &vkBuf);
        bkpGetBufferOffset(mesh.geo.buffer, &off);
        bkpCmdBindVertexBuffer(cmd, 0, vkBuf, off);

        if(mesh.geo.hasIndices) {
            VkDeviceSize idx = off + (VkDeviceSize)mesh.geo.indicesOffset;
            bkpCmdBindIndexBuffer(cmd, vkBuf, idx, mesh.geo.indexType);
            bkpCmdDrawIndexed(cmd, mesh.geo.count, 0, 0);
        } else {
            bkpCmdDraw(cmd, mesh.geo.count, 0);
        }
    }

    for(uint32_t c = 0; c < node.childCount; c++)
        _drawNodeShadow(cmd, model, node.children[c], frame);
}

/* ---- main draw -------------------------------------------------------- */

void Renderer::draw(BkpGpuAdapter adp, VkCommandBuffer cmd,
                    uint32_t frame, float aspect,
                    BkpModel* model, Camera& cam,
                    const BkpMat4& rootTransform, Animator* anim) {
    /* compute lightVP (reuse from shadow pass if available) */
    if(model && showShadow) {
        /* already computed in drawShadow; _lightVP is set */
    } else {
        _lightVP = model ? _computeLightVP() : bkpIdentityMat4();
    }

    /* update FrameUBO */
    FrameUBO ubo = {};
    ubo.view = cam.view();
    ubo.proj = cam.proj(aspect);
    BkpVec3 pos = cam.position();
    ubo.camPos  = bkpVec4(pos.x, pos.y, pos.z, 1.0f);

    float ce = cosf(light.elevation), se = sinf(light.elevation);
    float ca = cosf(light.azimuth),   sa = sinf(light.azimuth);
    BkpVec3 ld = bkpVec3(ce*sa, se, ce*ca);
    ld = bkpNormalize3D(&ld);
    ubo.lightDir   = bkpVec4(ld.x, ld.y, ld.z, light.intensity);
    ubo.lightColor = bkpVec4(light.color[0], light.color[1], light.color[2], -0.001f);
    ubo.lightVP    = _lightVP;

    /* extra.x: 0=no shadow, 1=plan shadow only, 2=plan+model shadow */
    float shadowVal = 0.0f;
    if(showShadow) shadowVal = shadowOnModel ? 2.0f : 1.0f;
    ubo.extra    = bkpVec4(shadowVal, planChecker ? 1.0f : 0.0f, checkerScale, 0.0f);
    ubo.planColor = bkpVec4(planColor[0], planColor[1], planColor[2], 0.0f);

    bkpUploadBufferData(adp, frameUbo[frame].buffer, &ubo, 0, sizeof(FrameUBO));

    /* world transforms + joint matrices — always rebuild so rootTransform is applied */
    if(model) {
        if(anim && !anim->worldTransforms.empty()) {
            _worldTransforms = anim->worldTransforms;
        } else {
            _worldTransforms.resize(model->nodeCount);
            for(uint32_t r = 0; r < model->rootNodeCount; r++)
                _buildWorldTransforms(model, model->rootNodes[r], rootTransform);
        }
        _uploadJointMatrices(adp, frame, model, nullptr);
    }

    /* draw meshes */
    if(model && model->nodeCount > 0 && !matGPU.empty()) {
        /* opaque pass */
        bkpCmdBindDescriptorSets(cmd, pbrPipeline.pipelineLayout.layout, 0, 1, &frameDSet[frame]);
        for(uint32_t r = 0; r < model->rootNodeCount; r++)
            _drawNode(cmd, model, model->rootNodes[r], frame);

        /* transparent pass — collect, sort back-to-front, draw */
        struct TransDraw { int nodeIdx; uint32_t subIdx; float distSq; };
        std::vector<TransDraw> trans;
        BkpVec3 cp = cam.position();

        std::function<void(int)> collect = [&](int ni) {
            if(ni < 0 || ni >= (int)model->nodeCount) return;
            BkpNode& node = model->nodes[ni];
            for(uint32_t p = 0; p < node.meshCount; p++) {
                uint32_t mi = (uint32_t)node.meshIdx + p;
                if(mi >= model->meshCount) break;
                BkpModelMesh& mesh = model->meshes[mi];
                BkpMaterial* mat = (mesh.materialIdx >= 0 && (uint32_t)mesh.materialIdx < model->materialCount)
                                   ? &model->materials[mesh.materialIdx] : nullptr;
                if(mat && mat->alphaMode == BKP_ALPHA_BLEND) {
                    const BkpMat4& w = (ni < (int)_worldTransforms.size())
                                       ? _worldTransforms[(uint32_t)ni] : bkpIdentityMat4();
                    float dx = w.mLine3.x - cp.x, dy = w.mLine3.y - cp.y, dz = w.mLine3.z - cp.z;
                    trans.push_back({ni, p, dx*dx + dy*dy + dz*dz});
                }
            }
            for(uint32_t ci = 0; ci < node.childCount; ci++)
                collect(node.children[ci]);
        };
        for(uint32_t r = 0; r < model->rootNodeCount; r++)
            collect(model->rootNodes[r]);

        if(!trans.empty()) {
            std::sort(trans.begin(), trans.end(),
                [](const TransDraw& a, const TransDraw& b){ return a.distSq > b.distSq; });
            for(auto& td : trans)
                _drawTransMesh(cmd, model, td.nodeIdx, td.subIdx, frame);
        }
    }

    /* plan */
    if(showPlan) {
        bkpCmdBindPipeline(cmd, planPipeline.pipeline);
        bkpCmdBindDescriptorSets(cmd, planPipeline.pipelineLayout.layout, 0, 1, &frameDSet[frame]);
        bkpCmdDraw(cmd, 6, 0);
    }

    /* grid */
    if(showGrid) {
        bkpCmdBindPipeline(cmd, gridPipeline.pipeline);
        bkpCmdBindDescriptorSets(cmd, gridPipeline.pipelineLayout.layout, 0, 1, &frameDSet[frame]);
        bkpCmdDraw(cmd, 6, 0);
    }

    /* AABB wireframes */
    if((showSceneAabb || showMeshAabb) && model) {
        bkpCmdBindPipeline(cmd, aabbPipeline.pipeline);
        bkpCmdBindDescriptorSets(cmd, aabbPipeline.pipelineLayout.layout, 0, 1, &shadowDSet[frame]);
        VkPipelineLayout lay = aabbPipeline.pipelineLayout.layout;

        auto drawBox = [&](const BkpMat4& modelMat,
                           const float mn[3], const float mx[3],
                           float r, float g, float b) {
            AabbPC pc;
            memcpy(pc.model, &modelMat, sizeof(pc.model));
            pc.boxMin[0]=mn[0]; pc.boxMin[1]=mn[1]; pc.boxMin[2]=mn[2]; pc.boxMin[3]=0.f;
            pc.boxMax[0]=mx[0]; pc.boxMax[1]=mx[1]; pc.boxMax[2]=mx[2]; pc.boxMax[3]=0.f;
            pc.color[0]=r; pc.color[1]=g; pc.color[2]=b; pc.color[3]=1.f;
            bkpCmdPushConstants(cmd, lay, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(AabbPC), &pc);
            bkpCmdDraw(cmd, 24, 0);
        };

        if(showSceneAabb) {
            BkpMat4 identity = bkpIdentityMat4();
            float mn[3]={aabbMin.x,aabbMin.y,aabbMin.z};
            float mx[3]={aabbMax.x,aabbMax.y,aabbMax.z};
            drawBox(identity, mn, mx, 1.f, 1.f, 0.f); /* yellow */
        }

        if(showMeshAabb && model->nodeCount > 0)
        {
          std::function<void(int)> drawNodeAabb = [&](int nodeIdx) {
            if(nodeIdx < 0 || nodeIdx >= (int)model->nodeCount) return;

            BkpNode& node = model->nodes[nodeIdx];

            if(node.meshIdx >= 0 && node.meshIdx < (int)model->meshCount)
            {
              const BkpModelMesh& mesh = model->meshes[(uint32_t)node.meshIdx];
              const BkpMat4& world = (nodeIdx < (int)_worldTransforms.size())
                ? _worldTransforms[(uint32_t)nodeIdx]
                : bkpIdentityMat4();
              drawBox(world, mesh.meshAabbMin, mesh.meshAabbMax, 0.f, 1.f, 1.f); /* cyan */
            }
            for(int c = 0; c < (int)node.childCount; ++c)
            {
              drawNodeAabb(node.children[c]);
            }
          };
          for(uint32_t r = 0; r < model->rootNodeCount; r++)
          {
            drawNodeAabb(model->rootNodes[r]);
          }
        }
    }
}

void Renderer::_drawNode(VkCommandBuffer cmd, BkpModel* model,
                         int nodeIdx, uint32_t frame) {
    if(nodeIdx < 0 || nodeIdx >= (int)model->nodeCount) return;
    BkpNode& node = model->nodes[nodeIdx];

    const BkpMat4& world = (nodeIdx < (int)_worldTransforms.size())
                           ? _worldTransforms[(uint32_t)nodeIdx]
                           : bkpIdentityMat4();

    for(uint32_t p = 0; p < node.meshCount; p++)
    {
        uint32_t mi = (uint32_t)node.meshIdx + p;
        if(mi >= model->meshCount) break;
        BkpModelMesh& mesh = model->meshes[mi];

        /* skip transparent meshes — drawn in a sorted second pass */
        {
            BkpMaterial* m = (mesh.materialIdx >= 0 && (uint32_t)mesh.materialIdx < model->materialCount)
                             ? &model->materials[mesh.materialIdx] : nullptr;
            if(m && m->alphaMode == BKP_ALPHA_BLEND) continue;
        }

        bool skinned = (mesh.isSkinned != 0) && (node.skinIdx >= 0);

        VkPipelineLayout layout;
        if(skinned)
        {
            bkpCmdBindPipeline(cmd, pbrPipelineLBS.pipeline);
            layout = pbrPipelineLBS.pipelineLayout.layout;
            bkpCmdBindDescriptorSets(cmd, layout, 0, 1, &frameDSet[frame]);
            bkpCmdBindDescriptorSets(cmd, layout, 2, 1, &jointDSet[frame]);
        }
        else
        {
            bkpCmdBindPipeline(cmd, pbrPipeline.pipeline);
            layout = pbrPipeline.pipelineLayout.layout;
            bkpCmdBindDescriptorSets(cmd, layout, 0, 1, &frameDSet[frame]);
        }

        int matIdx = (mesh.materialIdx >= 0 && (uint32_t)mesh.materialIdx < (uint32_t)matGPU.size())
                     ? mesh.materialIdx : 0;
        bkpCmdBindDescriptorSets(cmd, layout, 1, 1, &matGPU[matIdx].ds);

        MeshPC pc = {};
        if(skinned)
        {
            BkpMat4 ident = bkpIdentityMat4();
            memcpy(pc.model, &ident, sizeof(BkpMat4));
        }
        else
        {
            memcpy(pc.model, &world, sizeof(BkpMat4));
        }

        BkpMaterial* mat = (mesh.materialIdx >= 0 && (uint32_t)mesh.materialIdx < model->materialCount)
                           ? &model->materials[mesh.materialIdx] : nullptr;
        if(mat)
        {
          memcpy(pc.baseColor, mat->baseColor, 16);
          pc.metallic  = mat->metallic;
          pc.roughness = mat->roughness;
          pc.flags = (mat->albedoIdx            >= 0 ? 1 : 0)
            | (mat->normalIdx            >= 0 ? 2 : 0)
            | (mat->metallicRoughnessIdx >= 0 ? 4 : 0)
            | (mat->emissiveIdx          >= 0 ? 8 : 0);
        }
        else
        {
          pc.baseColor[0] = pc.baseColor[1] = pc.baseColor[2] = 0.72f;
          pc.baseColor[3] = 1.0f;
          pc.roughness    = 0.5f;
        }
        pc.renderMode = (int32_t)renderMode;
        if(renderMode == RenderMode::Flat)
        {
            pc.baseColor[0] = flatColor[0];
            pc.baseColor[1] = flatColor[1];
            pc.baseColor[2] = flatColor[2];
            pc.baseColor[3] = 1.0f;
        }

        bkpCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(MeshPC), &pc);

        VkCullModeFlags cull = (mat && mat->doubleSided) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        vkCmdSetCullMode(cmd, cull);

        VkBuffer vkBuf; VkDeviceSize off;
        bkpGetBuffer(mesh.geo.buffer, &vkBuf);
        bkpGetBufferOffset(mesh.geo.buffer, &off);
        bkpCmdBindVertexBuffer(cmd, 0, vkBuf, off);

        if(mesh.geo.hasIndices)
        {
            VkDeviceSize idx = off + (VkDeviceSize)mesh.geo.indicesOffset;
            bkpCmdBindIndexBuffer(cmd, vkBuf, idx, mesh.geo.indexType);
            bkpCmdDrawIndexed(cmd, mesh.geo.count, 0, 0);
        }
        else
        {
            bkpCmdDraw(cmd, mesh.geo.count, 0);
        }
    }

    for(uint32_t c = 0; c < node.childCount; c++)
    {
        _drawNode(cmd, model, node.children[c], frame);
    }
}

void Renderer::_drawTransMesh(VkCommandBuffer cmd, BkpModel* model,
                               int nodeIdx, uint32_t subIdx, uint32_t frame)
{
    if(nodeIdx < 0 || nodeIdx >= (int)model->nodeCount) return;
    BkpNode& node = model->nodes[nodeIdx];
    uint32_t mi = (uint32_t)node.meshIdx + subIdx;
    if(mi >= model->meshCount) return;
    BkpModelMesh& mesh = model->meshes[mi];

    const BkpMat4& world = (nodeIdx < (int)_worldTransforms.size())
                           ? _worldTransforms[(uint32_t)nodeIdx] : bkpIdentityMat4();

    bool skinned = (mesh.isSkinned != 0) && (node.skinIdx >= 0);
    VkPipelineLayout layout;
    if(skinned)
    {
        bkpCmdBindPipeline(cmd, pbrPipelineLBSBlend.pipeline);
        layout = pbrPipelineLBSBlend.pipelineLayout.layout;
        bkpCmdBindDescriptorSets(cmd, layout, 0, 1, &frameDSet[frame]);
        bkpCmdBindDescriptorSets(cmd, layout, 2, 1, &jointDSet[frame]);
    }
    else
    {
        bkpCmdBindPipeline(cmd, pbrPipelineBlend.pipeline);
        layout = pbrPipelineBlend.pipelineLayout.layout;
        bkpCmdBindDescriptorSets(cmd, layout, 0, 1, &frameDSet[frame]);
    }

    int matIdx = (mesh.materialIdx >= 0 && (uint32_t)mesh.materialIdx < (uint32_t)matGPU.size())
                 ? mesh.materialIdx : 0;
    bkpCmdBindDescriptorSets(cmd, layout, 1, 1, &matGPU[matIdx].ds);

    MeshPC pc = {};
    if(skinned)
    {
        BkpMat4 ident = bkpIdentityMat4();
        memcpy(pc.model, &ident, sizeof(BkpMat4));
    }
    else
    {
        memcpy(pc.model, &world, sizeof(BkpMat4));
    }

    BkpMaterial* mat = (mesh.materialIdx >= 0 && (uint32_t)mesh.materialIdx < model->materialCount)
                       ? &model->materials[mesh.materialIdx] : nullptr;
    if(mat)
    {
        memcpy(pc.baseColor, mat->baseColor, 16);
        pc.metallic  = mat->metallic;
        pc.roughness = mat->roughness;
        pc.flags = (mat->albedoIdx            >= 0 ? 1 : 0)
                 | (mat->normalIdx            >= 0 ? 2 : 0)
                 | (mat->metallicRoughnessIdx >= 0 ? 4 : 0)
                 | (mat->emissiveIdx          >= 0 ? 8 : 0);
    }
    else
    {
        pc.baseColor[0] = pc.baseColor[1] = pc.baseColor[2] = 0.72f;
        pc.baseColor[3] = 1.0f;
        pc.roughness    = 0.5f;
    }
    pc.renderMode = (int32_t)renderMode;
    if(renderMode == RenderMode::Flat)
    {
        pc.baseColor[0] = flatColor[0];
        pc.baseColor[1] = flatColor[1];
        pc.baseColor[2] = flatColor[2];
        pc.baseColor[3] = 1.0f;
    }

    bkpCmdPushConstants(cmd, layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(MeshPC), &pc);

    VkCullModeFlags cull = (mat && mat->doubleSided) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    vkCmdSetCullMode(cmd, cull);

    VkBuffer vkBuf; VkDeviceSize off;
    bkpGetBuffer(mesh.geo.buffer, &vkBuf);
    bkpGetBufferOffset(mesh.geo.buffer, &off);
    bkpCmdBindVertexBuffer(cmd, 0, vkBuf, off);

    if(mesh.geo.hasIndices)
    {
        VkDeviceSize idx = off + (VkDeviceSize)mesh.geo.indicesOffset;
        bkpCmdBindIndexBuffer(cmd, vkBuf, idx, mesh.geo.indexType);
        bkpCmdDrawIndexed(cmd, mesh.geo.count, 0, 0);
    }
    else
    {
        bkpCmdDraw(cmd, mesh.geo.count, 0);
    }
}

/* ---- attachment info helpers ------------------------------------------ */

VkRenderingAttachmentInfo Renderer::depthAttachment() const
{
    VkRenderingAttachmentInfo a = {};
    a.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageView   = depth.imageViews[0];
    a.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    a.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.clearValue  = {.depthStencil = {1.0f, 0}};
    return a;
}

VkRenderingAttachmentInfo Renderer::buildColorAttachment(VkImageView swapView) const
{
    VkRenderingAttachmentInfo a = {};
    a.sType    = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.loadOp   = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.clearValue = {.color = {{0.12f, 0.12f, 0.14f, 1.0f}}};

    if((msaaSamples != VK_SAMPLE_COUNT_1_BIT))
    {
        a.imageView          = _msaa.imageViews[0];
        a.imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.storeOp            = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
        a.resolveImageView   = swapView;
        a.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else
    {
        a.imageView   = swapView;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    }
    return a;
}

/* ---- MSAA buffer ------------------------------------------------------ */


void Renderer::_createMsaaBuffer(BkpGpuAdapter adp, uint32_t w, uint32_t h)
{
    _msaa = {};
    _msaa.imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    _msaa.imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    _msaa.imageInfo.format        = _colorFmt;
    _msaa.imageInfo.extent        = {w, h, 1};
    _msaa.imageInfo.mipLevels     = 1;
    _msaa.imageInfo.arrayLayers   = 1;
    _msaa.imageInfo.samples       = msaaSamples;
    _msaa.imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    _msaa.imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    _msaa.imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    _msaa.imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    _msaa.viewInfo.sType          = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    _msaa.viewInfo.viewType       = VK_IMAGE_VIEW_TYPE_2D;
    _msaa.viewInfo.format         = _colorFmt;
    _msaa.viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    _msaa.bufferType              = eBUFFER_GPU;
    _msaa.mipType                 = eMIPMAP_NONE;
    bkpCreateImageResources(adp, &_msaa);
}

void Renderer::_destroyMsaaBuffer(BkpGpuAdapter adp)
{
    bkpDestroyImageResource(adp, &_msaa);
    _msaa = {};
}

void Renderer::addMsaaBarrier(VkCommandBuffer cmd) const
{
    if(!(msaaSamples != VK_SAMPLE_COUNT_1_BIT)) return;
    BkpImageBarrierInfo b = {
        .image     = _msaa.images[0],
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcStage  = VK_PIPELINE_STAGE_2_NONE, .srcAccess = VK_ACCESS_2_NONE,
        .dstStage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1
    };
    bkpCmdBarrierImages(cmd, &b, 1);
}

/* ---- pipeline teardown / MSAA rebuild --------------------------------- */

void Renderer::_destroyPipelines(BkpGpuAdapter adp)
{
    auto destroyPL = [&](BkpPipelineGraphic& pl) {
        bkpDestroyGraphicPipeline(adp, &pl);
        bkpDestroyPipelineCache(adp, &pl.pipelineCache);
        bkpDestroyPipelineLayout(adp, &pl.pipelineLayout);
    };
    destroyPL(pbrPipeline);
    /* blend variants share the layout with their opaque counterpart — only destroy pipeline+cache */
    bkpDestroyGraphicPipeline(adp, &pbrPipelineBlend);
    bkpDestroyPipelineCache(adp, &pbrPipelineBlend.pipelineCache);
    destroyPL(pbrPipelineLBS);
    bkpDestroyGraphicPipeline(adp, &pbrPipelineLBSBlend);
    bkpDestroyPipelineCache(adp, &pbrPipelineLBSBlend.pipelineCache);
    destroyPL(gridPipeline);
    destroyPL(planPipeline);
    destroyPL(shadowPipeline);
    destroyPL(shadowPipelineSkin);
    destroyPL(aabbPipeline);

    bkpDestroyShader(adp, &pbrProg);
    bkpDestroyShader(adp, &skinProg);
    bkpDestroyShader(adp, &gridProg);
    bkpDestroyShader(adp, &planProg);
    bkpDestroyShader(adp, &shadowProg);
    bkpDestroyShader(adp, &shadowSkinProg);
    bkpDestroyShader(adp, &aabbProg);

    bkpDestroyShaderModule(adp, &pbrVert);   bkpDestroyShaderModule(adp, &pbrFrag);
    bkpDestroyShaderModule(adp, &skinVert);
    bkpDestroyShaderModule(adp, &gridVert);  bkpDestroyShaderModule(adp, &gridFrag);
    bkpDestroyShaderModule(adp, &planFrag);
    bkpDestroyShaderModule(adp, &shadowVert);
    bkpDestroyShaderModule(adp, &shadowSkinVert);
    bkpDestroyShaderModule(adp, &shadowFrag);
    bkpDestroyShaderModule(adp, &aabbVert);  bkpDestroyShaderModule(adp, &aabbFrag);
}

void Renderer::setMSAA(BkpGpuAdapter adp, VkSampleCountFlagBits samples)
{
    /* clamp to hardware max */
    while(samples > maxMsaaSamples && samples > VK_SAMPLE_COUNT_1_BIT)
        samples = (VkSampleCountFlagBits)(samples >> 1);

    _destroyMsaaBuffer(adp);
    bkpDestroyImageResource(adp, &depth);
    _destroyPipelines(adp);

    msaaSamples = samples;
    _createPipelines(adp, _colorFmt);

    uint32_t w = (uint32_t)adp->frameInfo.winWidth;
    uint32_t h = (uint32_t)adp->frameInfo.winHeight;
    depth = bkpDefaultDepthBuffer();
    depth.imageInfo.extent  = {w, h, 1};
    depth.imageInfo.samples = msaaSamples;
    bkpCreateDepthResources(adp, &depth);
    if(msaaSamples != VK_SAMPLE_COUNT_1_BIT) _createMsaaBuffer(adp, w, h);
}

/* ---- cleanup ---------------------------------------------------------- */

void Renderer::cleanup(BkpGpuAdapter adp)
{
    for(auto& m : matGPU)
    {
        if(m.ds != VK_NULL_HANDLE)
        {
            bkpFreeDescriptorSet(adp, &descPool, m.ds);
        }
    }
    matGPU.clear();

    for(uint32_t i = 0; i < MAX_FRAMES; i++)
    {
        if(frameDSet[i]  != VK_NULL_HANDLE) bkpFreeDescriptorSet(adp, &descPool, frameDSet[i]);
        if(shadowDSet[i] != VK_NULL_HANDLE) bkpFreeDescriptorSet(adp, &descPool, shadowDSet[i]);
        if(jointDSet[i]  != VK_NULL_HANDLE) bkpFreeDescriptorSet(adp, &descPool, jointDSet[i]);
        bkpDestroyBuffersGPU(adp, &frameUbo[i]);
        bkpDestroyBuffersGPU(adp, &jointUbo[i]);
    }

    bkpDestroyDescriptorPool(adp, &descPool);
    _destroyMsaaBuffer(adp);
    _destroyPipelines(adp);

    bkpDestroyImageResource(adp, &defAlbedo);
    bkpDestroyImageResource(adp, &defNormal);
    bkpDestroyImageResource(adp, &defORM);
    bkpDestroyImageResource(adp, &defEmissive);
    bkpDestroyImageResource(adp, &depth);
    bkpDestroyImageResource(adp, &shadowMapImg);
    bkpDestroySampler(adp, &sampler);
    bkpDestroySampler(adp, &shadowSmp);
}
