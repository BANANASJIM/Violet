#pragma once

#include "Camera.hpp"

namespace violet {

class PerspectiveCamera : public Camera {
public:
    PerspectiveCamera(float fov = 45.0f, float aspectRatio = 16.0f/9.0f,
                     float nearPlane = 0.1f, float farPlane = 10.0f);

    glm::mat4 getViewMatrix() const override;
    glm::mat4 getProjectionMatrix() const override;

    void setFOV(float fov) { this->fov = fov; projectionDirty = true; markFrustumDirty(); }
    void setAspectRatio(float aspectRatio) { this->aspectRatio = aspectRatio; projectionDirty = true; markFrustumDirty(); }
    void setNearPlane(float nearPlane) { this->nearPlane = nearPlane; projectionDirty = true; markFrustumDirty(); }
    void setFarPlane(float farPlane) { this->farPlane = farPlane; projectionDirty = true; markFrustumDirty(); }

    float getFOV() const { return fov; }
    float getAspectRatio() const { return aspectRatio; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }

protected:
    void updateViewMatrix() override;
    void updateProjectionMatrix() override;

private:
    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;
};

}