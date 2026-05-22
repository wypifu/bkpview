#include "app.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "dialog.h"
#include "splash.h"
#include "icon.h"
#include "fa_solid.h"
#include "inter_regular.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <algorithm>
#ifdef _WIN32
#  include <direct.h>
#  include <sys/stat.h>
#  define bkp_mkdir(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define bkp_mkdir(p) mkdir(p, 0755)
#endif

/* Windows icon via Win32 API (RGBA → BGRA) */

/* ---- helpers ---------------------------------------------------------- */

static VkSampleCountFlagBits defaultMsaa(VkSampleCountFlagBits maxSamples)
{
    if(maxSamples >= VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if(maxSamples >= VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

/* ---- GLFW callbacks --------------------------------------------------- */

static App* s_app = nullptr;

static void cb_mouseButton(GLFWwindow*, int btn, int action, int)
{
    if(ImGui::GetIO().WantCaptureMouse) return;
    double x, y;
    glfwGetCursorPos(bkpGetWindow(), &x, &y);
    s_app->camera.onMouseButton(btn, action, (float)x, (float)y);
}

/*______________________________________________________________*/
static void cb_mouseMove(GLFWwindow*, double x, double y) {
    if(ImGui::GetIO().WantCaptureMouse) return;
    s_app->camera.onMouseMove((float)x, (float)y);
}

/*______________________________________________________________*/
static void cb_scroll(GLFWwindow*, double, double dy) {
    if(ImGui::GetIO().WantCaptureMouse) return;
    s_app->camera.onScroll((float)dy);
}

/*______________________________________________________________*/
static void cb_drop(GLFWwindow*, int count, const char** paths) {
    if(count > 0)
        snprintf(s_app->pendingPath, sizeof(s_app->pendingPath), "%s", paths[0]);
}


/*______________________________________________________________*/
static void cb_key(GLFWwindow*, int key, int, int action, int) {
    if(action != GLFW_PRESS) return;
    if(ImGui::GetIO().WantCaptureKeyboard) return;
    switch(key) {
    case GLFW_KEY_P: s_app->renderer.renderMode = RenderMode::PBR;     break;
    case GLFW_KEY_F: s_app->renderer.renderMode = RenderMode::Flat;    break;
    case GLFW_KEY_N: s_app->renderer.renderMode = RenderMode::Normals; break;
    case GLFW_KEY_G: s_app->renderer.showGrid   = !s_app->renderer.showGrid; break;
    case GLFW_KEY_SPACE:
        if(s_app->hasModel && s_app->animState.clipIdx >= 0)
            s_app->animState.playing = !s_app->animState.playing;
        break;
    case GLFW_KEY_R:
        if(s_app->hasModel) {
            s_app->camera.azimuth   = 0.5f;
            s_app->camera.elevation = 0.4f;
            s_app->camera.focusAABB(s_app->renderer.aabbMin, s_app->renderer.aabbMax);
        }
        break;
    }
}

/* ---- init ------------------------------------------------------------- */
void App::_applyIngaStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // --- Géométrie (Le "Rounding") ---
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 0.0f; // On enlève les bordures pour un look plus "flat"

    // --- Palette de couleurs (Gris profond & Bleu InGa) ---
    const ImVec4 bgColor    = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    const ImVec4 accentColor = ImVec4(0.05f, 0.42f, 0.68f, 1.00f); // Ton bleu actuel
    const ImVec4 panelColor  = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);

    colors[ImGuiCol_WindowBg]             = bgColor;
    colors[ImGuiCol_ChildBg]              = panelColor;
    colors[ImGuiCol_PopupBg]              = panelColor;
    colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.20f, 0.23f, 0.50f);
    
    // Éléments de saisie (Sliders, Checkboxes)
    colors[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.23f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = accentColor;

    // Onglets et Headers
    colors[ImGuiCol_Header]               = ImVec4(0.03f, 0.15f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.05f, 0.22f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.02f, 0.10f, 0.22f, 1.00f);

    // Boutons
    colors[ImGuiCol_Button]               = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = accentColor;
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.04f, 0.38f, 0.62f, 1.00f);
}

bool App::init(int w, int h) {
    s_app = this;

    bkpWindowEnableResizable(1);

    BkpConfig cfg = bkpDefaultConfig();
    cfg.vulkanContextInfo.winWidth         = w;
    cfg.vulkanContextInfo.winHeight        = h;
    cfg.vulkanContextInfo.maxFrameInFlight = MAX_FRAMES;
    cfg.appName = "BkpView 0.6";
    cfg.contextMemoryInfo.gfxPageMemorySize = 1024 * 1024 * 8; /* 8 MiB — default 4 MiB too small for 9 pipelines */

    if(!bkpInit(&ctx, &cfg)) return false;
    bkpActivateGpuAdapter(&ctx.vulkanContext, &adp, nullptr);
    _generalGroupPageSize = bkpGetAllocGroupPageSize(0);

    swc.info = bkpDefaultSwapChain(1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_TRUE, VK_PRESENT_MODE_MAILBOX_KHR);
    {
        static constexpr VkPresentModeKHR kModes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
        bkpCreateSwapChain(adp, &swc, kModes, 2);
    }

    cmdPool.flags  = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPool.pQueue = &adp->graphicQueue;
    bkpCreateCmdPool(adp, &cmdPool);

    cmds.commandPool = &cmdPool;
    cmds.level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmds.count       = MAX_FRAMES;
    bkpAllocateCmdBuffer(adp, &cmds);

    renderer.init(adp, swc.info.imageFormat);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    _applyIngaStyle();
    //ImGui::StyleColorsDark();

    {
        ImGuiIO& fio = ImGui::GetIO();
        ImFontConfig mainCfg;
        mainCfg.FontDataOwnedByAtlas = false;
        fio.Fonts->AddFontFromMemoryTTF(
            (void*)inter_regular_ttf, (int)inter_regular_ttf_size,
            15.0f, &mainCfg);
        ImFontConfig cfg;
        cfg.MergeMode             = true;
        cfg.FontDataOwnedByAtlas  = false;
        static const ImWchar fa_ranges[] = { 0xE000, 0xF8FF, 0 };
        fio.Fonts->AddFontFromMemoryTTF(
            (void*)fa_solid_ttf, (int)fa_solid_ttf_size,
            13.0f, &cfg, fa_ranges);
    }

    GLFWwindow* win = bkpGetWindow();

    float dpiScale = 1.0f;
    { float sx, sy; glfwGetWindowContentScale(win, &sx, &sy); dpiScale = sx; }
    if(dpiScale > 1.01f) {
        ImGui::GetIO().FontGlobalScale = dpiScale;
        ImGui::GetStyle().ScaleAllSizes(dpiScale);
    }

    glfwSetMouseButtonCallback(win, cb_mouseButton);
    glfwSetCursorPosCallback  (win, cb_mouseMove);
    glfwSetScrollCallback     (win, cb_scroll);
    glfwSetDropCallback       (win, cb_drop);
    glfwSetKeyCallback        (win, cb_key);

    /* window icon — glfwSetWindowIcon handles RGBA correctly on all platforms */
    {
        GLFWimage glfwIcon = { icon_width, icon_height, (unsigned char*)icon_rgba };
        glfwSetWindowIcon(win, 1, &glfwIcon);
    }

    ImGui_ImplGlfw_InitForVulkan(win, true);
    _initImGuiVulkan();

    bkpSetDefaultTextureInfo(&splashTex);
    BkpMemInfo mi = { (void*)splash_png, 0, splash_png_size };
    bkpCreateTextureFromMemory(adp, &mi, &splashTex);
    splashDS = ImGui_ImplVulkan_AddTexture(
        renderer.sampler.sampler,
        splashTex.imageViews[0],
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    modelBaseTransform = bkpIdentityMat4();
    _loadRecentFiles();
    _prevTime = glfwGetTime();
    return true;
}

/* ---- TRS rebuild ------------------------------------------------------ */

void App::_rebuildTransform() {
    float rx = trsRot[0] * (3.14159265f / 180.f);
    float ry = trsRot[1] * (3.14159265f / 180.f);
    float rz = trsRot[2] * (3.14159265f / 180.f);
    BkpMat4 T = bkpIdentityMat4();
    BkpVec3 tv = bkpVec3(trsTrans[0], trsTrans[1], trsTrans[2]);
    bkpMat4SetTranslate(&T, &tv);
    BkpMat4 Rx = bkpMat4GetRotationX(rx);
    BkpMat4 Ry = bkpMat4GetRotationY(ry);
    BkpMat4 Rz = bkpMat4GetRotationZ(rz);
    BkpMat4 S = bkpIdentityMat4();
    BkpVec3 sv = bkpVec3(trsScale[0], trsScale[1], trsScale[2]);
    bkpMat4SetScale(&S, &sv);
    BkpMat4 RyRx = bkpMat4DotMat4(&Ry, &Rx);
    BkpMat4 R    = bkpMat4DotMat4(&Rz, &RyRx);
    BkpMat4 RS   = bkpMat4DotMat4(&R, &S);
    modelBaseTransform = bkpMat4DotMat4(&T, &RS);
}

/* ---- root transform (builtin animation) ------------------------------- */

BkpMat4 App::_computeRootTransform() const {
    if(builtinAnim.mode == BuiltinAnimMode::Off)
        return modelBaseTransform;

    /* pivot = AABB center XZ */
    float cx = (renderer.aabbMin.x + renderer.aabbMax.x) * 0.5f;
    float cz = (renderer.aabbMin.z + renderer.aabbMax.z) * 0.5f;
    float modelH = renderer.aabbMax.y - renderer.aabbMin.y;

    BkpMat4 result = bkpIdentityMat4();

    switch(builtinAnim.mode) {
    case BuiltinAnimMode::Rotation:
    case BuiltinAnimMode::Swing: {
        float angle = (builtinAnim.mode == BuiltinAnimMode::Rotation)
                      ? builtinAnim.phase
                      : sinf(builtinAnim.phase) * (60.0f * 3.14159265f / 180.0f);

        float c = cosf(angle), s = sinf(angle);

        /* T(pivot) * Ry(angle) * T(-pivot) */
        BkpMat4 ry;
        ry.mLine0 = bkpVec4( c,    0.0f, -s,    0.0f);
        ry.mLine1 = bkpVec4( 0.0f, 1.0f,  0.0f, 0.0f);
        ry.mLine2 = bkpVec4( s,    0.0f,  c,    0.0f);
        ry.mLine3 = bkpVec4( 0.0f, 0.0f,  0.0f, 1.0f);

        /* translate pivot to origin, rotate, translate back */
        BkpMat4 tNeg = bkpIdentityMat4();
        tNeg.mLine3  = bkpVec4(-cx, 0.0f, -cz, 1.0f);
        BkpMat4 tPos = bkpIdentityMat4();
        tPos.mLine3  = bkpVec4( cx, 0.0f,  cz, 1.0f);

        BkpMat4 tmp  = bkpMat4DotMat4(&ry,   &tNeg);
        result       = bkpMat4DotMat4(&tPos, &tmp);
        break;
    }
    case BuiltinAnimMode::Hover: {
        float amp = modelH * 0.25f;
        float dy  = (sinf(builtinAnim.phase) + 1.0f) * 0.5f * amp;
        result.mLine3 = bkpVec4(0.0f, dy, 0.0f, 1.0f);
        break;
    }
    default: break;
    }
    return bkpMat4DotMat4(&result, &modelBaseTransform);
}

/* ---- main loop -------------------------------------------------------- */

void App::run() {
    while(!bkpWindowIsClosed(ctx.vulkanContext.win)) {
        bkpUpdateInputs(ctx.vulkanContext.win);

        double now = glfwGetTime();
        float  dt  = (float)(now - _prevTime);
        _prevTime  = now;
        if(dt > 0.1f) dt = 0.1f;  /* clamp on first frame / lag spike */

        if(pendingMsaaChange) {
            _applyMsaaChange();
            pendingMsaaChange = false;
        }

        if(pendingPath[0]) {
            _loadFile(pendingPath);
            pendingPath[0] = '\0';
        }

        if(bkpWindowGetFrameBufferResized())
            _handleResize();

        /* skip draw when minimized */
        int fw, fh;
        glfwGetFramebufferSize(bkpGetWindow(), &fw, &fh);
        if(fw == 0 || fh == 0) continue;

        /* builtin animation */
        if(hasModel && builtinAnim.mode != BuiltinAnimMode::Off)
            builtinAnim.phase += (3.14159265f * 0.5f) * builtinAnim.speed * dt;

        /* glTF animation */
        if(hasModel && animState.playing && animState.clipIdx >= 0) {
            animator.speed   = animState.speed;
            animator.loop    = animState.loop;
            animator.playing = animState.playing;
            animator.update(dt);
        }

        _drawFrame();
    }
    bkpWaitDevice(adp);
}

void App::_handleResize() {
    bkpWindowResetFramebufferResized();
    bkpWaitDevice(adp);

    int w, h;
    bkpWindowWaitEventFrameBufferSize(&w, &h);
    if(w == 0 || h == 0) return;

    bkpCleanupSwapChain(adp, &swc);
    swc.info = bkpDefaultSwapChain(1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_TRUE, VK_PRESENT_MODE_MAILBOX_KHR);
    {
        static constexpr VkPresentModeKHR kModes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
        bkpCreateSwapChain(adp, &swc, kModes, 2);
    }
    renderer.handleResize(adp);
    ImGui_ImplVulkan_SetMinImageCount(MAX_FRAMES);
}

void App::_loadFile(const char* path) {
    bkpWaitDevice(adp);

    if(hasModel) {
        renderer.unloadModel(adp, &model);
        hasModel = false;
        animState = AnimState{};
    }

    /* pick scratch group — create/resize a dedicated one for large files.
       OBJ/PLY/STL expand heavily on GPU: compact file bytes → 64-byte BkpVertex,
       worst case ~8× the raw file size.  Trigger the scratch group as soon as
       the estimated expanded size would exceed the general page, not the file size. */
    size_t scratchGroup = 0;
    {
        struct stat st;
        if(stat(path, &st) == 0)
        {
            size_t fileSize = (size_t)st.st_size;
            size_t needed   = fileSize * 8;
            if(needed > _generalGroupPageSize)
            {
                if(!_loaderGroupSet) {
                    BkpAllocationGroupInfo gi = {"LargeFile", needed};
                    _loaderGroupId  = bkpAddAllocatorGroup(&gi);
                    _loaderGroupSet = true;
                } else if(bkpGetAllocGroupPageSize(_loaderGroupId) < needed) {
                    bkpResizeAllocGroup(_loaderGroupId, needed);
                }
                scratchGroup = _loaderGroupId;
            }
        }
    }

    if(bkpLoadModel(adp, path, &model, scratchGroup)) {
        hasModel = true;
        modelBaseTransform = bkpIdentityMat4();
        renderer.loadModel(adp, &model);
        snprintf(currentModelPath, sizeof(currentModelPath), "%s", path);
        _addRecentFile(path);

        /* window title: "BkpView 1.0 - filename.ext" */
        const char* slash = path;
        for(const char* p = path; *p; p++)
            if(*p == '/' || *p == '\\') slash = p + 1;
        char title[256];
        snprintf(title, sizeof(title), "BkpView 1.0 - %s", slash);
        glfwSetWindowTitle(bkpGetWindow(), title);
        animator.setModel(&model);

        renderer.aabbMin = bkpVec3(model.aabbMin[0], model.aabbMin[1], model.aabbMin[2]);
        renderer.aabbMax = bkpVec3(model.aabbMax[0], model.aabbMax[1], model.aabbMax[2]);

        /* auto-ground: translate so AABB bottom sits at y=0 */
        float localMinY = model.aabbMin[1];
        trsTrans[0] = 0.f; trsTrans[1] = -localMinY; trsTrans[2] = 0.f;
        trsRot[0]   = 0.f; trsRot[1]   = 0.f;        trsRot[2]   = 0.f;
        trsScale[0] = 1.f; trsScale[1] = 1.f;         trsScale[2] = 1.f;
        _rebuildTransform();
        if(localMinY != 0.0f) {
            renderer.aabbMin.y -= localMinY;
            renderer.aabbMax.y -= localMinY;
        }
        camera.focusAABB(renderer.aabbMin, renderer.aabbMax);

        /* auto-play first animation if available */
        if(model.animationCount > 0) {
            animState.clipIdx = 0;
            animState.playing = true;
            animator.setClip(&model, 0);
        }
    } else {
        fprintf(stderr, "Failed to load: %s\n", path);
        const char* slash = path;
        for(const char* p = path; *p; p++)
            if(*p == '/' || *p == '\\') slash = p + 1;
        snprintf(_loadErrorMsg, sizeof(_loadErrorMsg), "Could not load:\n%s", slash);
    }

    if(_loaderGroupSet)
        bkpClearAllocGroup(_loaderGroupId);
}

/* ---- draw frame ------------------------------------------------------- */

void App::_drawFrame() {
    bkpPrepareFrame(adp);

    uint32_t    frame    = adp->frameInfo.currentFrame;
    uint32_t    imgIdx   = adp->frameInfo.imageAcquired;
    VkImage     swapImg  = adp->swapChain->images[imgIdx];
    VkImageView swapView = adp->swapChain->imageViews[imgIdx];
    VkExtent2D  extent   = adp->swapChain->info.imageExtent;
    VkCommandBuffer cmd  = cmds.cmds[frame];

    float aspect = (float)extent.width / (float)extent.height;

    BkpMat4 rootTransform = _computeRootTransform();

    /* evaluate glTF animation (always called — also applies rootTransform for static models) */
    if(hasModel)
        animator.evaluate(&model, rootTransform);

    bkpBeginCommandBuffer(&cmds, frame, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    /* swapchain + main depth: UNDEFINED → attachment (batched) */
    BkpImageBarrierInfo frameBeginBarriers[2] = {
        { .image = swapImg,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .srcStage  = VK_PIPELINE_STAGE_2_NONE, .srcAccess = VK_ACCESS_2_NONE,
          .dstStage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1 },
        { .image = renderer.depth.images[0],
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
          .srcStage  = VK_PIPELINE_STAGE_2_NONE, .srcAccess = VK_ACCESS_2_NONE,
          .dstStage  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
          .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1 }
    };
    bkpCmdBarrierImages(cmd, frameBeginBarriers, 2);

    renderer.addMsaaBarrier(cmd);

    /* shadow pass (before color rendering, manages own shadow map layout) */
    Animator* animPtr = hasModel ? &animator : nullptr;
    renderer.drawShadow(adp, cmd, frame,
                        hasModel ? &model : nullptr, rootTransform, animPtr);

    /* ---- pass 1: scene + plan + grid ---- */
    VkRenderingAttachmentInfo colorAtt = renderer.buildColorAttachment(swapView);
    VkRenderingAttachmentInfo depthAtt = renderer.depthAttachment();

    VkRenderingInfo ri = {};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea           = {{0,0}, extent};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &colorAtt;
    ri.pDepthAttachment     = &depthAtt;

    bkpCmdBeginRendering(cmd, &ri);
    bkpUpdateWindowViewportScissor(cmd, (float)extent.width, (float)extent.height);

    renderer.draw(adp, cmd, frame, aspect,
                  hasModel ? &model : nullptr, camera,
                  rootTransform, animPtr);

    bkpCmdEndRendering(cmd);

    /* barrier between passes */
    bkpCmdMemoryBarrier(cmd,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    /* ---- pass 2: ImGui ---- */
    VkRenderingAttachmentInfo imguiColorAtt = {};
    imguiColorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    imguiColorAtt.imageView   = swapView;
    imguiColorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imguiColorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
    imguiColorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo riImGui = {};
    riImGui.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    riImGui.renderArea           = {{0,0}, extent};
    riImGui.layerCount           = 1;
    riImGui.colorAttachmentCount = 1;
    riImGui.pColorAttachments    = &imguiColorAtt;

    bkpCmdBeginRendering(cmd, &riImGui);
    bkpUpdateWindowViewportScissor(cmd, (float)extent.width, (float)extent.height);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if(!pendingScreenshot) _buildUI();
    ImGui::Render();
    if(!pendingScreenshot) ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    bkpCmdEndRendering(cmd);

    /* COLOR_ATTACHMENT → PRESENT */
    BkpImageBarrierInfo presentBarrier = {
        .image     = swapImg,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcStage  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStage  = VK_PIPELINE_STAGE_2_NONE, .dstAccess = VK_ACCESS_2_NONE,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1
    };
    bkpCmdBarrierImages(cmd, &presentBarrier, 1);

    bkpEndCommandBuffer(&cmds, frame);
    bkpSubmitFrameBasic(adp, &cmds);
    bkpPresentFrame(adp);

    if(pendingScreenshot) {
        pendingScreenshot = false;
        bkpWaitDevice(adp);

        /* build output path: next to model if loaded, else ~/Pictures */
        char dir[512];
        char base[128] = "screenshot";
        {
#ifdef _WIN32
            const char* home = getenv("USERPROFILE");
#else
            const char* home = getenv("HOME");
#endif
            snprintf(dir, sizeof(dir), "%s/Pictures", home ? home : ".");
            bkp_mkdir(dir);
        }
        if(currentModelPath[0]) {
            snprintf(dir, sizeof(dir), "%s", currentModelPath);
            char* last = dir;
            for(char* p = dir; *p; p++)
                if(*p == '/' || *p == '\\') last = p;
            if(last != dir) {
                snprintf(base, sizeof(base), "%s", last + 1);
                /* strip extension from base */
                char* dot = strrchr(base, '.');
                if(dot) *dot = '\0';
                *last = '\0';  /* dir now ends before the filename */
            }
        }

        char path[640];
        time_t t = time(nullptr);
        struct tm* lt = localtime(&t);
        snprintf(path, sizeof(path), "%s/%s_%04d%02d%02d_%02d%02d%02d.png",
                 dir, base,
                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                 lt->tm_hour, lt->tm_min, lt->tm_sec);
        bkpSaveSwapChainImage(adp, &swc, imgIdx, path, eBKP_IMAGE_TYPE_PNG);
    }
}

/* ---- UI --------------------------------------------------------------- */

static const char* kSpeedLabels[] = {"x0.25","x0.5","x0.75","x1","x1.25","x1.5","x1.75","x2"};
static const float kSpeedValues[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

static int speedIndex(float v) {
    int best = 3;
    float diff = 1e9f;
    for(int i = 0; i < 8; i++) {
        float d = fabsf(kSpeedValues[i] - v);
        if(d < diff) { diff = d; best = i; }
    }
    return best;
}

void App::_buildUI() {
    ImGuiIO& io  = ImGui::GetIO();
    float scale  = io.FontGlobalScale;

    /* layout constants */
    const float tabW    = 28.f * scale;
    const float contW   = 258.f * scale;
    const float panelW  = tabW + contW;
    const float togW    = 16.f * scale;
    const float togH    = 40.f * scale;
    const float dispH   = io.DisplaySize.y;

    /* ---- splash ---- */
    if(!hasModel && splashDS != VK_NULL_HANDLE) {
        constexpr float IMG_W = 1408.f, IMG_H = 768.f, ASPECT = IMG_W / IMG_H;
        float vpLeft = panelVisible ? panelW + togW : togW;
        float vpW = io.DisplaySize.x - vpLeft, vpH = dispH;
        float w = vpW * 0.80f, h = w / ASPECT;
        if(h > vpH * 0.80f) { h = vpH * 0.80f; w = h * ASPECT; }
        float cx = vpLeft + vpW * 0.5f, cy = vpH * 0.5f;
        ImGui::GetBackgroundDrawList()->AddImage(
            ImTextureRef((ImTextureID)(uint64_t)splashDS),
            {cx - w*0.5f, cy - h*0.5f}, {cx + w*0.5f, cy + h*0.5f});
    }

    /* ---- overlay ---- */
    if(showOverlay) {
        ImGui::SetNextWindowPos({io.DisplaySize.x - 10.f, 10.f}, ImGuiCond_Always, {1.f, 0.f});
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("##overlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs);
        ImGui::Text("%.0f fps", io.Framerate);
        if(hasModel) {
            uint32_t tris = 0;
            for(uint32_t i = 0; i < model.meshCount; i++)
                tris += model.meshes[i].geo.count / 3;
            ImGui::Text("%u meshes  %u tris", model.meshCount, tris);
            ImGui::Text("%u mats  %u tex  %u nodes",
                        model.materialCount, model.textureCount, model.nodeCount);
            if(model.animationCount) ImGui::Text("%u anims", model.animationCount);
            if(model.skinCount)      ImGui::Text("%u skins", model.skinCount);

            bool hasTRS = false;
            for(uint32_t i = 0; i < model.nodeCount && !hasTRS; i++) {
                const BkpNode& n = model.nodes[i];
                hasTRS = n.translation[0] || n.translation[1] || n.translation[2]
                      || n.rotation[0]    || n.rotation[1]    || n.rotation[2]
                      || n.scale[0] != 1.f || n.scale[1] != 1.f || n.scale[2] != 1.f;
            }
            ImGui::Text("TRS: %s", hasTRS ? "yes" : "no");

            bool hasAlbedo = false, hasNormal = false, hasRoughMetal = false;
            bool hasAO = false, hasEmissive = false;
            for(uint32_t i = 0; i < model.materialCount; i++) {
                const BkpMaterial& m = model.materials[i];
                if(m.albedoIdx            >= 0) hasAlbedo     = true;
                if(m.normalIdx            >= 0) hasNormal     = true;
                if(m.metallicRoughnessIdx >= 0) hasRoughMetal = true;
                if(m.occlusionIdx         >= 0) hasAO         = true;
                if(m.emissiveIdx          >= 0) hasEmissive   = true;
            }
            ImGui::Separator();
            ImGui::Text("albedo:   %s", hasAlbedo     ? "yes" : "no");
            ImGui::Text("normal:   %s", hasNormal     ? "yes" : "no");
            ImGui::Text("rough:    %s", hasRoughMetal ? "yes" : "no");
            ImGui::Text("ao:       %s", hasAO         ? "yes" : "no");
            ImGui::Text("emissive: %s", hasEmissive   ? "yes" : "no");
        }
        ImGui::End();
    }

    /* ---- sidebar panel ---- */
    /* Tab definitions: 0=File  1=Scene  2=Light */
    static const struct { const char* icon; const char* tip; } kTabs[] = {
        { "\xEF\x81\xBC", "File"        },   /* fa-folder-open  U+F07C */
        { "\xEF\x81\xAE", "Scene"       },   /* fa-eye          U+F06E */
        { "\xEF\x86\x85", "Light"       },   /* fa-sun          U+F185 */
    };
    static constexpr int NUM_TABS = 3;

    static float sPanelH = 300.f; /* updated each frame for toggle centering */

    if(panelVisible) {
        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar       | ImGuiWindowFlags_NoResize          |
            ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_NoScrollbar       |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;

        const float margin = 10.f * scale;
        ImGui::SetNextWindowPos({margin, dispH * 0.5f}, ImGuiCond_Always, {0.f, 0.5f});
        ImGui::SetNextWindowSizeConstraints({panelW, 60.f}, {panelW, dispH - 2.f * margin});
        ImGui::Begin("##panel", nullptr, kFlags);

        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.06f, 0.22f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.08f, 0.35f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.05f, 0.42f, 0.68f, 1.0f));

        /* --- icon tab column (group) --- */
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(2.f * scale, 4.f * scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f * scale, 4.f * scale));
        ImGui::BeginGroup();
        for(int i = 0; i < NUM_TABS; i++) {
            bool sel = (activeTab == i);
            ImGui::PushStyleColor(ImGuiCol_Button,
                sel ? ImVec4(0.05f, 0.42f, 0.68f, 1.0f)
                    : ImVec4(0.09f, 0.11f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.35f, 0.58f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.04f, 0.48f, 0.76f, 1.0f));
            char lbl[32]; snprintf(lbl, sizeof(lbl), "%s##t%d", kTabs[i].icon, i);
            if(ImGui::Button(lbl, {tabW - 4.f * scale, tabW})) activeTab = i;
            ImGui::PopStyleColor(3);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", kTabs[i].tip);
        }
        ImGui::EndGroup();
        ImGui::PopStyleVar(2);

        ImGui::SameLine(0.f, 0.f);

        /* --- content (group) --- */
        ImGui::BeginGroup();

        switch(activeTab) {

        /* ========== FILE ========== */
        case 0:
            ImGui::Spacing();
            if(ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) {
                if(ImGui::Button("Open...", {-1.f, 0.f})) {
                    const char* path = openFileDialog("Open 3D Model");
                    if(path) snprintf(pendingPath, sizeof(pendingPath), "%s", path);
                }
                ImGui::TextDisabled("or drop file here");
                if(!recentFiles.empty()) {
                    ImGui::SetNextItemWidth(-1.f);
                    if(ImGui::BeginCombo("##recent", "Recent...")) {
                        for(auto& p : recentFiles) {
                            const char* name = p.c_str();
                            for(const char* q = p.c_str(); *q; ++q)
                                if(*q == '/' || *q == '\\') name = q + 1;
                            if(ImGui::Selectable(name))
                                snprintf(pendingPath, sizeof(pendingPath), "%s", p.c_str());
                            if(ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", p.c_str());
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            break;

        /* ========== SCENE ========== */
        case 1:
            ImGui::Spacing();
            if(ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
                int mode = (int)renderer.renderMode;
                ImGui::RadioButton("PBR",     &mode, 0); ImGui::SameLine();
                ImGui::RadioButton("Flat",    &mode, 1); ImGui::SameLine();
                ImGui::RadioButton("Normals", &mode, 2);
                renderer.renderMode = (RenderMode)mode;
                if(renderer.renderMode == RenderMode::Flat)
                    ImGui::ColorEdit3("Color##flat", renderer.flatColor);
                ImGui::Checkbox("Grid",  &renderer.showGrid);
                ImGui::SameLine();
                ImGui::Checkbox("Stats", &showOverlay);
                ImGui::Checkbox("Scene AABB", &renderer.showSceneAabb);
                ImGui::SameLine();
                ImGui::Checkbox("Nodes AABB", &renderer.showMeshAabb);
                {
                    static const VkSampleCountFlagBits kCounts[] = {
                        VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT,
                        VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT
                    };
                    static const char* kMsaaLabels[] = {"Off", "2x", "4x", "8x", "16x"};
                    int maxIdx = 0;
                    for(int i = 1; i < 5; i++)
                        if(kCounts[i] <= renderer.maxMsaaSamples) maxIdx = i;
                    int curIdx = 0;
                    for(int i = 0; i <= maxIdx; i++)
                        if(renderer.msaaSamples == kCounts[i]) { curIdx = i; break; }
                    ImGui::SetNextItemWidth(80.f * scale);
                    if(ImGui::BeginCombo("MSAA", kMsaaLabels[curIdx])) {
                        for(int i = 0; i <= maxIdx; i++) {
                            bool sel = (curIdx == i);
                            if(ImGui::Selectable(kMsaaLabels[i], sel)) {
                                VkSampleCountFlagBits v = kCounts[i];
                                if(i == 0) v = VK_SAMPLE_COUNT_1_BIT;
                                else if(v == VK_SAMPLE_COUNT_1_BIT)
                                    v = defaultMsaa(renderer.maxMsaaSamples);
                                pendingMsaaValue  = v;
                                pendingMsaaChange = true;
                            }
                            if(sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            ImGui::Spacing();
            if(ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("FOV", &camera.fovDeg, 10.f, 120.f);
                if(ImGui::Button("Reset view") && hasModel) {
                    camera.azimuth   = 0.5f;
                    camera.elevation = 0.4f;
                    camera.focusAABB(renderer.aabbMin, renderer.aabbMax);
                }
            }
            ImGui::Spacing();
            if(hasModel && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                float fw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.f) / 3.f;

                auto refreshWorldAABB = [&]() {
                    float mnX=model.aabbMin[0], mnY=model.aabbMin[1], mnZ=model.aabbMin[2];
                    float mxX=model.aabbMax[0], mxY=model.aabbMax[1], mxZ=model.aabbMax[2];
                    float cx8[8]={mnX,mxX,mnX,mxX,mnX,mxX,mnX,mxX};
                    float cy8[8]={mnY,mnY,mxY,mxY,mnY,mnY,mxY,mxY};
                    float cz8[8]={mnZ,mnZ,mnZ,mnZ,mxZ,mxZ,mxZ,mxZ};
                    float wmn[3]={1e30f,1e30f,1e30f}, wmx[3]={-1e30f,-1e30f,-1e30f};
                    for(int k=0;k<8;++k){
                        BkpVec4 v=bkpVec4(cx8[k],cy8[k],cz8[k],1.0f);
                        BkpVec4 w=bkpMat4DotVec4(&modelBaseTransform,&v);
                        if(w.x<wmn[0])wmn[0]=w.x; if(w.x>wmx[0])wmx[0]=w.x;
                        if(w.y<wmn[1])wmn[1]=w.y; if(w.y>wmx[1])wmx[1]=w.y;
                        if(w.z<wmn[2])wmn[2]=w.z; if(w.z>wmx[2])wmx[2]=w.z;
                    }
                    renderer.aabbMin=bkpVec3(wmn[0],wmn[1],wmn[2]);
                    renderer.aabbMax=bkpVec3(wmx[0],wmx[1],wmx[2]);
                };

                auto axisLabel = [&](const char* label, ImVec4 color) {
                    ImGui::TextColored(color, "%s", label);
                    ImGui::SameLine(0.f, 2.f * scale);
                };

                ImGui::TextDisabled("Position");
                axisLabel("X", {0.9f,0.2f,0.2f,1.f});
                ImGui::SetNextItemWidth(fw);
                bool tx = ImGui::DragFloat("##px", &trsTrans[0], 0.01f, 0.f, 0.f, "%.3f");
                ImGui::SameLine(0.f, 4.f * scale);
                axisLabel("Y", {0.3f,0.8f,0.3f,1.f});
                ImGui::SetNextItemWidth(fw);
                bool ty = ImGui::DragFloat("##py", &trsTrans[1], 0.01f, 0.f, 0.f, "%.3f");
                ImGui::SameLine(0.f, 4.f * scale);
                axisLabel("Z", {0.3f,0.5f,1.f,1.f});
                ImGui::SetNextItemWidth(fw);
                bool tz = ImGui::DragFloat("##pz", &trsTrans[2], 0.01f, 0.f, 0.f, "%.3f");
                if(tx || ty || tz) _rebuildTransform();

                ImGui::TextDisabled("Scale");
                ImGui::SameLine();
                ImGui::Checkbox("Uniform##scale", &uniformScale);
                {
                    axisLabel("X", {0.9f,0.2f,0.2f,1.f});
                    ImGui::SetNextItemWidth(fw);
                    bool sx = ImGui::DragFloat("##sx", &trsScale[0], 0.005f, 0.001f, 1000.f, "%.3f");
                    ImGui::SameLine(0.f, 4.f * scale);
                    axisLabel("Y", {0.3f,0.8f,0.3f,1.f});
                    ImGui::SetNextItemWidth(fw);
                    bool sy = ImGui::DragFloat("##sy", &trsScale[1], 0.005f, 0.001f, 1000.f, "%.3f");
                    ImGui::SameLine(0.f, 4.f * scale);
                    axisLabel("Z", {0.3f,0.5f,1.f,1.f});
                    ImGui::SetNextItemWidth(fw);
                    bool sz = ImGui::DragFloat("##sz", &trsScale[2], 0.005f, 0.001f, 1000.f, "%.3f");
                    if(sx || sy || sz) {
                        if(uniformScale) {
                            if(sx)      { trsScale[1]=trsScale[2]=trsScale[0]; }
                            else if(sy) { trsScale[0]=trsScale[2]=trsScale[1]; }
                            else        { trsScale[0]=trsScale[1]=trsScale[2]; }
                        }
                        _rebuildTransform();
                    }
                }

                ImGui::TextDisabled("Rotation");
                axisLabel("X", {0.9f,0.2f,0.2f,1.f});
                ImGui::SetNextItemWidth(fw);
                bool rx = ImGui::DragFloat("##rx", &trsRot[0], 1.f, 0.f, 0.f, "%.1f\xc2\xb0");
                ImGui::SameLine(0.f, 4.f * scale);
                axisLabel("Y", {0.3f,0.8f,0.3f,1.f});
                ImGui::SetNextItemWidth(fw);
                bool ry = ImGui::DragFloat("##ry", &trsRot[1], 1.f, 0.f, 0.f, "%.1f\xc2\xb0");
                ImGui::SameLine(0.f, 4.f * scale);
                axisLabel("Z", {0.3f,0.5f,1.f,1.f});
                ImGui::SetNextItemWidth(fw);
                bool rz = ImGui::DragFloat("##rz", &trsRot[2], 1.f, 0.f, 0.f, "%.1f\xc2\xb0");
                if(rx || ry || rz) _rebuildTransform();

                if(ImGui::Button("Ground")) {
                    float mnX=model.aabbMin[0], mnY=model.aabbMin[1], mnZ=model.aabbMin[2];
                    float mxX=model.aabbMax[0], mxY=model.aabbMax[1], mxZ=model.aabbMax[2];
                    float cx[8]={mnX,mxX,mnX,mxX,mnX,mxX,mnX,mxX};
                    float cy[8]={mnY,mnY,mxY,mxY,mnY,mnY,mxY,mxY};
                    float cz[8]={mnZ,mnZ,mnZ,mnZ,mxZ,mxZ,mxZ,mxZ};
                    float worldMinY=1e30f;
                    for(int k=0;k<8;++k){
                        BkpVec4 v=bkpVec4(cx[k],cy[k],cz[k],1.0f);
                        BkpVec4 w=bkpMat4DotVec4(&modelBaseTransform,&v);
                        if(w.y<worldMinY) worldMinY=w.y;
                    }
                    trsTrans[1] -= worldMinY;
                    _rebuildTransform();
                    refreshWorldAABB();
                    camera.focusAABB(renderer.aabbMin, renderer.aabbMax);
                }
                ImGui::SameLine();
                if(ImGui::Button("Reset")) {
                    trsTrans[0]=trsTrans[1]=trsTrans[2]=0.f;
                    trsRot[0]=trsRot[1]=trsRot[2]=0.f;
                    trsScale[0]=trsScale[1]=trsScale[2]=1.f;
                    _rebuildTransform();
                    refreshWorldAABB();
                    camera.focusAABB(renderer.aabbMin, renderer.aabbMax);
                }
            }
            ImGui::Spacing();
            if(ImGui::CollapsingHeader("Builtin Anim", ImGuiTreeNodeFlags_DefaultOpen)) {
                int bm = (int)builtinAnim.mode;
                ImGui::RadioButton("Off##ba",  &bm, 0); ImGui::SameLine();
                ImGui::RadioButton("Rot##ba",  &bm, 1); ImGui::SameLine();
                ImGui::RadioButton("Hover##ba",&bm, 2); ImGui::SameLine();
                ImGui::RadioButton("Swing##ba",&bm, 3);
                builtinAnim.mode = (BuiltinAnimMode)bm;
                if(bm != 0) {
                    int si = speedIndex(builtinAnim.speed);
                    if(ImGui::SliderInt("Speed##ba", &si, 0, 7, kSpeedLabels[si]))
                        builtinAnim.speed = kSpeedValues[si];
                }
            }
            ImGui::Spacing();
            if(hasModel && model.animationCount > 0 &&
               ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* curName = (animState.clipIdx >= 0 && (uint32_t)animState.clipIdx < model.animationCount)
                                      ? model.animations[animState.clipIdx].name : "None";
                if(ImGui::BeginCombo("Clip##ac", curName)) {
                    for(uint32_t i = 0; i < model.animationCount; i++) {
                        bool sel = ((uint32_t)animState.clipIdx == i);
                        char dispName[128];
                        if(model.animations[i].name[0])
                            snprintf(dispName, sizeof(dispName), "%s", model.animations[i].name);
                        else
                            snprintf(dispName, sizeof(dispName), "Anim %u", i);
                        char lbl2[160];
                        snprintf(lbl2, sizeof(lbl2), "%s###clip%u", dispName, i);
                        if(ImGui::Selectable(lbl2, sel)) {
                            animState.clipIdx = (int)i;
                            animator.setClip(&model, (int)i);
                            animState.playing = true;
                        }
                        if(sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if(animState.playing) {
                    if(ImGui::Button("Pause##ac")) animState.playing = false;
                } else {
                    if(ImGui::Button("Play##ac"))  animState.playing = true;
                }
                ImGui::SameLine();
                if(ImGui::Button("Restart##ac")) {
                    animator.time = 0.0f;
                    if(animState.clipIdx >= 0) animator.setClip(&model, animState.clipIdx);
                    animState.playing = true;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop##ac", &animState.loop);
                if(animState.clipIdx >= 0 && (uint32_t)animState.clipIdx < model.animationCount) {
                    float dur = model.animations[animState.clipIdx].duration;
                    if(dur > 0.0f) {
                        float t = animator.time;
                        if(ImGui::SliderFloat("Time##ac", &t, 0.0f, dur, "%.2fs")) {
                            animator.time = t;
                            animState.playing = false;
                        }
                    }
                }
                {
                    int si = speedIndex(animState.speed);
                    if(ImGui::SliderInt("Speed##ac", &si, 0, 7, kSpeedLabels[si]))
                        animState.speed = kSpeedValues[si];
                }
            }
            break;

        /* ========== LIGHT ========== */
        case 2:
            ImGui::Spacing();
            if(ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderAngle("Azimuth##L",   &renderer.light.azimuth,   0.f, 360.f);
                ImGui::SliderAngle("Elevation##L", &renderer.light.elevation, 0.f,  90.f);
                ImGui::SliderFloat("Intensity",    &renderer.light.intensity, 0.f,  20.f);
                ImGui::ColorEdit3 ("Color",         renderer.light.color);
            }
            ImGui::Spacing();
            if(ImGui::CollapsingHeader("Plan & Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Plan##ps", &renderer.showPlan);
                if(renderer.showPlan) {
                    ImGui::Indent();
                    int pm = renderer.planChecker ? 1 : 0;
                    ImGui::RadioButton("Solid##p",   &pm, 0); ImGui::SameLine();
                    ImGui::RadioButton("Checker##p", &pm, 1);
                    renderer.planChecker = (pm == 1);
                    if(!renderer.planChecker)
                        ImGui::ColorEdit3("Color##plan", renderer.planColor);
                    else
                        ImGui::SliderFloat("Scale##chk", &renderer.checkerScale, 0.1f, 5.0f);
                    ImGui::Unindent();
                }
                ImGui::Checkbox("Shadow##ps", &renderer.showShadow);
                if(renderer.showShadow)
                    ImGui::Checkbox("  Shadow on model", &renderer.shadowOnModel);
            }
            break;

        } /* switch(activeTab) */

        /* ---- persistent footer ---- */
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if(ImGui::Button("Screenshot", {-1.f, 0.f}))
            pendingScreenshot = true;
        if(ImGui::Button("? Shortcuts", {-1.f, 0.f}))
            ImGui::OpenPopup("shortcuts_popup");
        if(ImGui::BeginPopup("shortcuts_popup")) {
            ImGui::SeparatorText("Keyboard shortcuts");
            ImGui::TextUnformatted("P         PBR mode");
            ImGui::TextUnformatted("F         Flat mode");
            ImGui::TextUnformatted("N         Normals mode");
            ImGui::TextUnformatted("G         Grid toggle");
            ImGui::TextUnformatted("Space     Play / Pause animation");
            ImGui::TextUnformatted("R         Reset camera");
            ImGui::EndPopup();
        }
        if(ImGui::Button("Buy me a coffee", {-1.f, 0.f}))
            openUrl("https://paypal.me/sabashka80");
        ImGui::Spacing();

        ImGui::EndGroup(); /* content group */

        sPanelH = ImGui::GetWindowSize().y;
        ImGui::PopStyleColor(3);
        ImGui::End(); /* ##panel */
    }

    /* ---- toggle tab ---- */
    {
        const float margin = 10.f * scale;
        float tx = panelVisible ? margin + panelW : 0.f;
        float ty = dispH * 0.5f - togH * 0.5f;
        ImGui::SetNextWindowPos({tx, ty}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({togW, togH}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.80f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.f * scale, 1.f * scale));
        ImGui::Begin("##tog", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize    |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 1.f));
        /* fa-chevron-left U+F053 / fa-chevron-right U+F054 */
        const char* arrow = panelVisible ? "\xEF\x81\x93" : "\xEF\x81\x94";
        if(ImGui::Button(arrow, {-1.f, -1.f}))
            panelVisible = !panelVisible;
        ImGui::PopStyleVar();
        ImGui::End();
    }

    /* ---- load error modal ---- */
    static char sErrorMsg[256] = {};
    if(_loadErrorMsg[0]) {
        memcpy(sErrorMsg, _loadErrorMsg, sizeof(sErrorMsg));
        _loadErrorMsg[0] = '\0';
        ImGui::OpenPopup("##load_error");
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    if(ImGui::BeginPopupModal("##load_error", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Spacing();
        ImGui::Text("%s", sErrorMsg);
        ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 80.f * scale) * 0.5f);
        if(ImGui::Button("OK", {80.f * scale, 0}))
            ImGui::CloseCurrentPopup();
        ImGui::Spacing();
        ImGui::EndPopup();
    }
}

/* ---- recent files ----------------------------------------------------- */

std::string App::_configDir() {
#ifdef _WIN32
    const char* base = getenv("APPDATA");
#else
    const char* base = getenv("XDG_CONFIG_HOME");
    if(!base) base = getenv("HOME");
    std::string dir = base ? std::string(base) + (getenv("XDG_CONFIG_HOME") ? "" : "/.config") + "/bkpview" : ".";
    return dir;
#endif
    return base ? std::string(base) + "/bkpview" : ".";
}

void App::_loadRecentFiles() {
    std::string path = _configDir() + "/recent.txt";
    FILE* f = fopen(path.c_str(), "r");
    if(!f) return;
    char line[512];
    while(fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if(len > 0) recentFiles.push_back(line);
    }
    fclose(f);
}

void App::_saveRecentFiles() {
    std::string dir  = _configDir();
    bkp_mkdir(dir.c_str());
    std::string path = dir + "/recent.txt";
    FILE* f = fopen(path.c_str(), "w");
    if(!f) return;
    for(auto& p : recentFiles) fprintf(f, "%s\n", p.c_str());
    fclose(f);
}

void App::_addRecentFile(const char* path) {
    std::string s(path);
    recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), s), recentFiles.end());
    recentFiles.insert(recentFiles.begin(), s);
    if((int)recentFiles.size() > MAX_RECENT)
        recentFiles.resize(MAX_RECENT);
    _saveRecentFiles();
}

/* ---- ImGui Vulkan init / MSAA ----------------------------------------- */

void App::_initImGuiVulkan() {
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion          = VK_API_VERSION_1_3;
    ii.Instance            = ctx.vulkanContext.instance;
    ii.PhysicalDevice      = adp->gpu;
    ii.Device              = adp->device;
    ii.QueueFamily         = adp->graphicQueue.index;
    ii.Queue               = adp->graphicQueue.queue[0];
    ii.DescriptorPoolSize  = 256;
    ii.MinImageCount       = MAX_FRAMES;
    ii.ImageCount          = swc.imageCount;
    ii.UseDynamicRendering = true;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        nullptr, 0, 1, &swc.info.imageFormat,
        VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED
    };
    ImGui_ImplVulkan_Init(&ii);
}

void App::_applyMsaaChange() {
    bkpWaitDevice(adp);
    renderer.setMSAA(adp, pendingMsaaValue);
}

/* ---- cleanup ---------------------------------------------------------- */

void App::cleanup() {
    bkpWaitDevice(adp);

    if(splashDS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(splashDS);
    bkpDestroyImageResource(adp, &splashTex);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if(hasModel) {
        renderer.unloadModel(adp, &model);
        hasModel = false;
    }
    renderer.cleanup(adp);

    bkpDestroyCommandBuffers(adp, &cmds);
    bkpDestroyCommandPool(adp, &cmdPool);
    bkpCleanupSwapChain(adp, &swc);
    bkpClearGpuAdapter(adp);
    bkpQuit(&ctx);

}
