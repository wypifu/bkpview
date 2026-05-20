#pragma once

#include <string>
#include <vector>

extern "C" {
#include <include/blukpast.h>
#include <system/include/bkp_allocator.h>
#include <loaders/include/bkp_loaders.h>
}

#include "camera.h"
#include "renderer.h"
#include "animator.h"

/* ---- builtin animation state ------------------------------------------ */
enum class BuiltinAnimMode { Off = 0, Rotation = 1, Hover = 2, Swing = 3 };

struct BuiltinAnimState {
    BuiltinAnimMode mode  = BuiltinAnimMode::Off;
    float           speed = 1.0f;
    float           phase = 0.0f;
};

/* ---- glTF animation state --------------------------------------------- */
struct AnimState {
    int   clipIdx = -1;
    bool  playing = false;
    bool  loop    = true;
    float speed   = 1.0f;
    /* time is stored in Animator::time */
};

/* ---- App -------------------------------------------------------------- */
struct App {
    BkpContext    ctx    = {};
    BkpGpuAdapter adp   = nullptr;
    BkpSwapChain  swc   = {};
    BkpCommandPool  cmdPool = {};
    BkpCommandBuffer cmds   = {};

    Camera   camera;
    Renderer renderer;
    Animator animator;
    BkpModel model    = {};
    bool     hasModel    = false;
    bool     showOverlay = true;
    bool     panelVisible = true;
    int      activeTab    = 0;   /* 0=File  1=Scene  2=Light */

    /* builtin and glTF animation */
    BuiltinAnimState builtinAnim;
    AnimState        animState;

    /* timing */
    double _prevTime = 0.0;

    /* open-file dialog path */
    char pendingPath[512] = {};

    /* current loaded model path (for screenshot location) */
    char currentModelPath[512] = {};

    /* dedicated scratch group for large files */
    size_t _generalGroupPageSize = 0;   /* read from bkp at init */
    size_t _loaderGroupId        = 0;
    bool   _loaderGroupSet       = false;

    /* recent files */
    static constexpr int MAX_RECENT = 10;
    std::vector<std::string> recentFiles;

    /* MSAA change deferred to next frame */
    bool                  pendingMsaaChange = false;
    VkSampleCountFlagBits pendingMsaaValue  = VK_SAMPLE_COUNT_1_BIT;

    /* screenshot deferred to end of frame */
    bool pendingScreenshot = false;

    /* load error — set in _loadFile, consumed in _buildUI */
    char _loadErrorMsg[256] = {};

    /* model orientation / position correction */
    BkpMat4 modelBaseTransform = {};
    float   trsTrans[3]        = {0.f, 0.f, 0.f};
    float   trsRot[3]          = {0.f, 0.f, 0.f};
    float   trsScale[3]        = {1.f, 1.f, 1.f};
    bool    uniformScale       = true;

    /* splash screen */
    BkpImageResource splashTex = {};
    VkDescriptorSet  splashDS  = VK_NULL_HANDLE;

    static constexpr uint32_t MAX_FRAMES = Renderer::MAX_FRAMES;

    bool init(int w, int h);
    void run();
    void cleanup();

private:
    void _loadFile(const char* path);
    void _handleResize();
    void _drawFrame();
    void _buildUI();
    void _applyIngaStyle();
    void _initImGuiVulkan();
    void _applyMsaaChange();
    void _rebuildTransform();

    BkpMat4 _computeRootTransform() const;

    static std::string _configDir();
    void _loadRecentFiles();
    void _saveRecentFiles();
    void _addRecentFile(const char* path);
};
