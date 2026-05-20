#include "camera.h"
#include <algorithm>

static constexpr float kElMin   = -0.785f;  /* -45 degrees */
static constexpr float kElMax   =  1.55f;
static constexpr float kDistMin =  0.1f;
static constexpr float kDistMax =  2000.0f;
static constexpr float kPI      =  3.14159265f;

BkpVec3 Camera::position() const {
    float cosEl = cosf(elevation);
    BkpVec3 p;
    p.x = target.x + distance * cosEl * sinf(azimuth);
    p.y = target.y + distance * sinf(elevation);
    p.z = target.z + distance * cosEl * cosf(azimuth);
    return p;
}

BkpMat4 Camera::view() const {
    BkpVec3 up = bkpVec3(0.0f, 1.0f, 0.0f);
    return bkpLookAt(position(), target, up);
}

BkpMat4 Camera::proj(float aspect) const {
    float fovRad = fovDeg * (kPI / 180.0f);
    BkpMat4 p = bkpPerspective(fovRad, aspect, 0.01f, 5000.0f);
    p.mLine1.y = -p.mLine1.y;   /* Vulkan Y-flip */
    return p;
}

void Camera::onMouseButton(int btn, int action, float x, float y) {
    bool pressed = (action != 0);
    if(btn == 0) leftDown  = pressed;
    if(btn == 1) rightDown = pressed;
    lastX = x;
    lastY = y;
}

void Camera::onMouseMove(float x, float y) {
    float dx = x - lastX;
    float dy = y - lastY;
    lastX = x;
    lastY = y;

    if(leftDown) {
        azimuth   -= dx * 0.005f;
        elevation += dy * 0.005f;
        elevation  = std::clamp(elevation, kElMin, kElMax);
    }

    if(rightDown) {
        BkpVec3 pos  = position();
        BkpVec3 fwd;
        fwd.x = target.x - pos.x;
        fwd.y = target.y - pos.y;
        fwd.z = target.z - pos.z;
        fwd = bkpNormalize3D(&fwd);

        BkpVec3 up    = bkpVec3(0.0f, 1.0f, 0.0f);
        BkpVec3 right = bkpCross3D(&fwd, &up);
        right = bkpNormalize3D(&right);
        BkpVec3 camUp = bkpCross3D(&right, &fwd);

        float speed = distance * 0.001f;
        target.x -= right.x * dx * speed;
        target.y -= right.y * dx * speed;
        target.z -= right.z * dx * speed;
        target.x += camUp.x * dy * speed;
        target.y += camUp.y * dy * speed;
        target.z += camUp.z * dy * speed;
    }
}

void Camera::onScroll(float dy) {
    distance *= (1.0f - dy * 0.1f);
    distance  = std::clamp(distance, kDistMin, kDistMax);
}

void Camera::focusAABB(BkpVec3 mn, BkpVec3 mx) {
    target.x = (mn.x + mx.x) * 0.5f;
    target.y = (mn.y + mx.y) * 0.5f;
    target.z = (mn.z + mx.z) * 0.5f;

    float dx = mx.x - mn.x;
    float dy = mx.y - mn.y;
    float dz = mx.z - mn.z;
    float fov  = fovDeg * (kPI / 180.0f);
    float tanV = tanf(fov * 0.5f);

    /* fit XZ extent against horizontal FOV (~1.6× vertical for a typical 3D viewport) */
    float xz_r = sqrtf(dx * dx + dz * dz) * 0.5f;
    float d_h  = xz_r / (tanV * 1.6f);
    /* fit Y half-extent against vertical FOV */
    float d_v  = (dy * 0.5f) / tanV;

    distance  = std::max(d_h, d_v) * 1.2f;
    distance  = std::clamp(distance, kDistMin, kDistMax);

    /* gentle elevation for flat models, steeper for tall ones */
    float ratio = (dy * 0.5f) / std::max(xz_r, 0.001f);
    elevation   = std::clamp(0.1f + 0.25f * ratio, 0.05f, 0.4f);
}
