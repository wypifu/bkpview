#include "animator.h"
#include <cmath>
#include <cstring>
#include <algorithm>

extern "C" {
#include <math/include/bkp_mat.h>
}

/* =========================================================================
   setModel / setClip / update
   ========================================================================= */

void Animator::setModel(BkpModel* model) {
    if(!model) return;
    _rest.resize(model->nodeCount);
    _animated.assign(model->nodeCount, false);
    worldTransforms.resize(model->nodeCount);

    for(uint32_t i = 0; i < model->nodeCount; i++) {
        BkpNode& n = model->nodes[i];
        memcpy(_rest[i].T, n.translation,  sizeof(float) * 3);
        memcpy(_rest[i].R, n.rotation,     sizeof(float) * 4);
        memcpy(_rest[i].S, n.scale,        sizeof(float) * 3);
        memcpy(_rest[i].M, n.localMatrix,  sizeof(float) * 16);
    }
}

void Animator::setClip(BkpModel* /*model*/, int idx) {
    clipIdx = idx;
    time    = 0.0f;
    playing = true;
}

void Animator::update(float dt) {
    if(playing) time += dt * speed;
}

/* =========================================================================
   TRS → column-major BkpMat4
   cgltf convention (same as glTF spec):
     mLineN = row N of the matrix stored in memory, but the library names
     them "lines" = rows.  localMatrix[16] is stored column-major (matches
     the renderer's memcpy usage), so:
       col 0 = [lm[0], lm[1], lm[2], lm[3]]
       col 1 = [lm[4], lm[5], lm[6], lm[7]]   (= mLine1)
     etc.
   ========================================================================= */

void Animator::_trsToMatrix(const float T[3], const float R[4], const float S[3], BkpMat4& out) {
    float qx = R[0], qy = R[1], qz = R[2], qw = R[3];
    float sx = S[0], sy = S[1], sz = S[2];
    float tx = T[0], ty = T[1], tz = T[2];

    /* column-major layout: lm[col*4 + row] */
    float lm[16];
    lm[0]  = (1.0f - 2.0f*qy*qy - 2.0f*qz*qz) * sx;
    lm[1]  = (       2.0f*qx*qy + 2.0f*qz*qw) * sx;
    lm[2]  = (       2.0f*qx*qz - 2.0f*qy*qw) * sx;
    lm[3]  = 0.0f;

    lm[4]  = (       2.0f*qx*qy - 2.0f*qz*qw) * sy;
    lm[5]  = (1.0f - 2.0f*qx*qx - 2.0f*qz*qz) * sy;
    lm[6]  = (       2.0f*qy*qz + 2.0f*qx*qw) * sy;
    lm[7]  = 0.0f;

    lm[8]  = (       2.0f*qx*qz + 2.0f*qy*qw) * sz;
    lm[9]  = (       2.0f*qy*qz - 2.0f*qx*qw) * sz;
    lm[10] = (1.0f - 2.0f*qx*qx - 2.0f*qy*qy) * sz;
    lm[11] = 0.0f;

    lm[12] = tx;
    lm[13] = ty;
    lm[14] = tz;
    lm[15] = 1.0f;

    /* BkpMat4: mLine0 = first 4 floats in memory = column 0 */
    out.mLine0 = bkpVec4(lm[0],  lm[1],  lm[2],  lm[3]);
    out.mLine1 = bkpVec4(lm[4],  lm[5],  lm[6],  lm[7]);
    out.mLine2 = bkpVec4(lm[8],  lm[9],  lm[10], lm[11]);
    out.mLine3 = bkpVec4(lm[12], lm[13], lm[14], lm[15]);
}

/* =========================================================================
   Interpolation helpers
   ========================================================================= */

void Animator::_lerpVec3(const float* a, const float* b, float t, float* out) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

void Animator::_slerpQuat(const float* a, const float* b, float t, float* out) {
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    float dot = a[0]*bx + a[1]*by + a[2]*bz + a[3]*bw;

    /* flip b if dot is negative to take shortest arc */
    if(dot < 0.0f) {
        bx = -bx; by = -by; bz = -bz; bw = -bw;
        dot = -dot;
    }

    /* near-parallel: fall back to normalised lerp */
    if(dot > 0.9995f) {
        out[0] = a[0] + (bx - a[0]) * t;
        out[1] = a[1] + (by - a[1]) * t;
        out[2] = a[2] + (bz - a[2]) * t;
        out[3] = a[3] + (bw - a[3]) * t;
        float inv = 1.0f / sqrtf(out[0]*out[0] + out[1]*out[1] +
                                  out[2]*out[2] + out[3]*out[3]);
        out[0] *= inv; out[1] *= inv; out[2] *= inv; out[3] *= inv;
        return;
    }

    float theta0 = acosf(dot);
    float theta  = theta0 * t;
    float sinT0  = sinf(theta0);
    float sinT   = sinf(theta);
    float s0     = cosf(theta) - dot * sinT / sinT0;
    float s1     = sinT / sinT0;

    out[0] = s0 * a[0] + s1 * bx;
    out[1] = s0 * a[1] + s1 * by;
    out[2] = s0 * a[2] + s1 * bz;
    out[3] = s0 * a[3] + s1 * bw;
}

/* =========================================================================
   Sampler evaluation
   ========================================================================= */

/* Returns bracket index i such that times[i] <= t <= times[i+1].
   Returns count-1 if t is beyond the last keyframe. */
static uint32_t findBracket(BkpAnimSampler* s, float t) {
    if(s->count == 0) return 0;
    if(t <= s->times[0]) return 0;
    if(t >= s->times[s->count - 1]) return s->count - 1;

    /* binary search */
    uint32_t lo = 0, hi = s->count - 1;
    while(lo + 1 < hi) {
        uint32_t mid = (lo + hi) / 2;
        if(s->times[mid] <= t) lo = mid; else hi = mid;
    }
    return lo;
}

void Animator::_sampleVec3(BkpAnimSampler* s, float t, float out[3]) {
    if(s->count == 0) { out[0] = out[1] = out[2] = 0.0f; return; }

    uint32_t i = findBracket(s, t);

    if(s->interp == BKP_INTERP_STEP || i + 1 >= s->count) {
        /* step: return value at i */
        const float* base;
        if(s->interp == BKP_INTERP_CUBICSPLINE)
            base = &s->values[i * 9 + 3]; /* skip in-tangent (3 floats) */
        else
            base = &s->values[i * 3];
        out[0] = base[0]; out[1] = base[1]; out[2] = base[2];
        return;
    }

    float dt = s->times[i+1] - s->times[i];
    float alpha = (dt > 1e-7f) ? (t - s->times[i]) / dt : 0.0f;

    if(s->interp == BKP_INTERP_CUBICSPLINE) {
        /* layout per keyframe: [in_tan(3), val(3), out_tan(3)] */
        const float* vi  = &s->values[i   * 9 + 3];
        const float* vi1 = &s->values[(i+1) * 9 + 3];
        _lerpVec3(vi, vi1, alpha, out);
    } else {
        /* LINEAR */
        const float* vi  = &s->values[i   * 3];
        const float* vi1 = &s->values[(i+1) * 3];
        _lerpVec3(vi, vi1, alpha, out);
    }
}

void Animator::_sampleQuat(BkpAnimSampler* s, float t, float out[4]) {
    if(s->count == 0) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }

    uint32_t i = findBracket(s, t);

    if(s->interp == BKP_INTERP_STEP || i + 1 >= s->count) {
        const float* base;
        if(s->interp == BKP_INTERP_CUBICSPLINE)
            base = &s->values[i * 12 + 4]; /* skip in-tangent (4 floats) */
        else
            base = &s->values[i * 4];
        out[0] = base[0]; out[1] = base[1]; out[2] = base[2]; out[3] = base[3];
        return;
    }

    float dt = s->times[i+1] - s->times[i];
    float alpha = (dt > 1e-7f) ? (t - s->times[i]) / dt : 0.0f;

    if(s->interp == BKP_INTERP_CUBICSPLINE) {
        /* layout per keyframe: [in_tan(4), val(4), out_tan(4)] */
        const float* vi  = &s->values[i   * 12 + 4];
        const float* vi1 = &s->values[(i+1) * 12 + 4];
        _slerpQuat(vi, vi1, alpha, out);
    } else {
        /* LINEAR */
        const float* vi  = &s->values[i   * 4];
        const float* vi1 = &s->values[(i+1) * 4];
        _slerpQuat(vi, vi1, alpha, out);
    }
}

/* =========================================================================
   evaluate
   ========================================================================= */

void Animator::_buildWorldTransforms(BkpModel* model, int nodeIdx, const BkpMat4& parent) {
    if(nodeIdx < 0 || nodeIdx >= (int)model->nodeCount) return;
    BkpNode& node = model->nodes[nodeIdx];

    BkpMat4 local;
    memcpy(&local, node.localMatrix, sizeof(BkpMat4));
    worldTransforms[(uint32_t)nodeIdx] = bkpMat4DotMat4(&parent, &local);

    for(uint32_t c = 0; c < node.childCount; c++)
        _buildWorldTransforms(model, node.children[c], worldTransforms[(uint32_t)nodeIdx]);
}

void Animator::evaluate(BkpModel* model, const BkpMat4& rootTransform) {
    if(!model || model->nodeCount == 0) return;

    /* ensure vectors are sized */
    if(worldTransforms.size() != model->nodeCount)
        worldTransforms.resize(model->nodeCount);

    /* reset to rest pose — also restores localMatrix for has_matrix nodes */
    uint32_t nRest = std::min((uint32_t)_rest.size(), model->nodeCount);
    for(uint32_t i = 0; i < nRest; i++) {
        BkpNode& n = model->nodes[i];
        memcpy(n.translation, _rest[i].T, sizeof(float) * 3);
        memcpy(n.rotation,    _rest[i].R, sizeof(float) * 4);
        memcpy(n.scale,       _rest[i].S, sizeof(float) * 3);
        memcpy(n.localMatrix, _rest[i].M, sizeof(float) * 16);
    }

    /* clear per-node animated flag */
    if(_animated.size() < model->nodeCount)
        _animated.assign(model->nodeCount, false);
    else
        std::fill(_animated.begin(), _animated.end(), false);

    if(clipIdx >= 0 && (uint32_t)clipIdx < model->animationCount) {
        BkpAnimation& clip = model->animations[clipIdx];

        if(clip.duration > 1e-7f) {
            if(loop)
                time = fmodf(time, clip.duration);
            else
                time = std::min(time, clip.duration);
        }

        for(uint32_t c = 0; c < clip.channelCount; c++) {
            BkpAnimChannel& ch = clip.channels[c];
            int ni = ch.nodeIdx;
            if(ni < 0 || ni >= (int)model->nodeCount) continue;
            BkpNode& node = model->nodes[ni];
            _animated[(uint32_t)ni] = true;

            switch(ch.path) {
            case BKP_ANIM_T: _sampleVec3(&ch.sampler, time, node.translation); break;
            case BKP_ANIM_R: _sampleQuat(&ch.sampler, time, node.rotation);    break;
            case BKP_ANIM_S: _sampleVec3(&ch.sampler, time, node.scale);       break;
            }
        }
    }

    /* rebuild localMatrix from TRS only for nodes touched by animation channels
       (has_matrix nodes keep their saved localMatrix from the reset above) */
    for(uint32_t i = 0; i < model->nodeCount; i++) {
        if(!_animated[i]) continue;
        BkpNode& node = model->nodes[i];
        BkpMat4 lm;
        _trsToMatrix(node.translation, node.rotation, node.scale, lm);
        memcpy(node.localMatrix, &lm, sizeof(BkpMat4));
    }

    /* build world transforms from roots */
    for(uint32_t r = 0; r < model->rootNodeCount; r++)
        _buildWorldTransforms(model, model->rootNodes[r], rootTransform);
}

/* =========================================================================
   computeJointMatrices
   ========================================================================= */

void Animator::computeJointMatrices(BkpModel* model, BkpMat4* out,
                                    uint32_t max, uint32_t skinIdx) const {
    if(!model || !out || model->skinCount == 0) return;
    if(skinIdx >= model->skinCount) return;

    BkpSkin& skin = model->skins[skinIdx];
    uint32_t count = std::min(skin.jointCount, max);

    for(uint32_t i = 0; i < count; i++) {
        int ji = skin.joints[i];
        if(ji < 0 || ji >= (int)worldTransforms.size()) {
            out[i] = bkpIdentityMat4();
            continue;
        }

        /* extract inverse bind matrix from flat column-major array */
        const float* ibm = &skin.inverseBindMatrices[i * 16];
        BkpMat4 ibmMat;
        ibmMat.mLine0 = bkpVec4(ibm[0],  ibm[1],  ibm[2],  ibm[3]);
        ibmMat.mLine1 = bkpVec4(ibm[4],  ibm[5],  ibm[6],  ibm[7]);
        ibmMat.mLine2 = bkpVec4(ibm[8],  ibm[9],  ibm[10], ibm[11]);
        ibmMat.mLine3 = bkpVec4(ibm[12], ibm[13], ibm[14], ibm[15]);

        const BkpMat4& wt = worldTransforms[(uint32_t)ji];
        out[i] = bkpMat4DotMat4(&wt, &ibmMat);
    }
}
