#include "PerspectiveCamera.hpp"

namespace violet {

PerspectiveCamera::PerspectiveCamera(float fov, float aspectRatio,
                                   float nearPlane, float farPlane)
    : fov(fov), aspectRatio(aspectRatio), nearPlane(nearPlane), farPlane(farPlane) {
    updateViewMatrix();
    updateProjectionMatrix();
}

glm::mat4 PerspectiveCamera::getViewMatrix() const {
    if (viewDirty) {
        const_cast<PerspectiveCamera*>(this)->updateViewMatrix();
    }
    return viewMatrix;
}

glm::mat4 PerspectiveCamera::getProjectionMatrix() const {
    if (projectionDirty) {
        const_cast<PerspectiveCamera*>(this)->updateProjectionMatrix();
    }
    return projectionMatrix;
}

void PerspectiveCamera::updateViewMatrix() {
    viewMatrix = glm::lookAt(position, target, up);
    viewDirty = false;
    markFrustumDirty();
}

void PerspectiveCamera::updateProjectionMatrix() {
    projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    projectionMatrix[1][1] *= -1;
    projectionDirty = false;
    markFrustumDirty();
}

}