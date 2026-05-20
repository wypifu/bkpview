#pragma once
#include <vector>
#include <array>
extern "C" {
#include <loaders/include/bkp_loaders.h>
}

class Animator {
public:
    /* public state */
    int   clipIdx = -1;
    float time    = 0.0f;
    bool  playing = false;
    bool  loop    = true;
    float speed   = 1.0f;

    /* world transforms in scene space (set by evaluate, indexed by nodeIdx) */
    std::vector<BkpMat4> worldTransforms;

    /* call once after loading a model to save rest pose */
    void setModel(BkpModel* model);

    /* select animation clip by index (resets time) */
    void setClip(BkpModel* model, int idx);

    /* advance time by dt seconds */
    void update(float dt);

    /* sample channels → update node localMatrix → build worldTransforms
       rootTransform is applied as parent of all root nodes */
    void evaluate(BkpModel* model, const BkpMat4& rootTransform);

    /* compute skinning joint matrices: out[i] = worldTransforms[joint_i] * IBM_i
       max = max joints to fill (typically 128), skinIdx = which skin to use (0 for first) */
    void computeJointMatrices(BkpModel* model, BkpMat4* out, uint32_t max, uint32_t skinIdx = 0) const;

private:
    struct RestTRS { float T[3], R[4], S[3]; float M[16]; };
    std::vector<RestTRS>  _rest;
    std::vector<bool>     _animated; /* per-node channel-touched flag */

    static void _sampleVec3(BkpAnimSampler* s, float t, float out[3]);
    static void _sampleQuat(BkpAnimSampler* s, float t, float out[4]);
    static void _lerpVec3(const float* a, const float* b, float t, float* out);
    static void _slerpQuat(const float* a, const float* b, float t, float* out);
    static void _trsToMatrix(const float T[3], const float R[4], const float S[3], BkpMat4& out);
    void _buildWorldTransforms(BkpModel* model, int nodeIdx, const BkpMat4& parent);
};
