#pragma once

extern "C" {
#include <include/blukpast.h>
}

#include <math.h>

/* Orbital (arcball) camera locked to a target point.
 * Controls:
 *   Left-drag  → orbit (azimuth / elevation)
 *   Right-drag → pan target
 *   Scroll     → zoom */
struct Camera {
    float   azimuth   =  0.5f;   /* radians, horizontal */
    float   elevation =  0.4f;   /* radians, clamped away from poles */
    float   distance  =  5.0f;
    float   fovDeg    = 45.0f;
    BkpVec3 target;              /* orbit centre, zero-initialised */

    bool  leftDown  = false;
    bool  rightDown = false;
    float lastX     = 0.0f;
    float lastY     = 0.0f;

    Camera() { target = bkpVec3(0.0f, 0.0f, 0.0f); }

    BkpVec3 position() const;
    BkpMat4 view()     const;
    BkpMat4 proj(float aspect) const; /* Y-flipped for Vulkan */

    void onMouseButton(int btn, int action, float x, float y);
    void onMouseMove(float x, float y);
    void onScroll(float dy);

    /* auto-fit so the AABB fills the view */
    void focusAABB(BkpVec3 mn, BkpVec3 mx);
};
