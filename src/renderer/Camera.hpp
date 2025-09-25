#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "math/Frustum.hpp"
#include "core/Log.hpp"

namespace violet {

class Camera {
public:
    virtual ~Camera() = default;

    virtual glm::mat4 getViewMatrix() const = 0;
    virtual glm::mat4 getProjectionMatrix() const = 0;

    void setPosition(const glm::vec3& position) { this->position = position; updateViewMatrix(); }
    void setTarget(const glm::vec3& target) { this->target = target; updateViewMatrix(); }
    void setUp(const glm::vec3& up) { this->up = up; updateViewMatrix(); }

    const glm::vec3& getPosition() const { return position; }
    const glm::vec3& getTarget() const { return target; }
    const glm::vec3& getUp() const { return up; }

    glm::vec3 getForward() const { return glm::normalize(target - position); }
    glm::vec3 getRight() const { return glm::normalize(glm::cross(getForward(), up)); }

    const Frustum& getFrustum() const {
        // FORCE frustum to be dirty every frame for debugging
        frustumDirty = true;

        if (frustumDirty) {
            // Correct order: projection * view (view-projection matrix)
            glm::mat4 viewProjMatrix = getProjectionMatrix() * getViewMatrix();
            frustum.extract(viewProjMatrix);
            frustumDirty = false;

            // Debug: Log frustum updates occasionally
            static int frustumUpdateCount = 0;
            frustumUpdateCount++;
            if (frustumUpdateCount % 60 == 0) {  // Log every ~1 second at 60fps
                violet::Log::info("Camera", "Frustum FORCED update #{} for camera at ({:.1f}, {:.1f}, {:.1f}) target({:.1f}, {:.1f}, {:.1f})",
                                frustumUpdateCount, position.x, position.y, position.z, target.x, target.y, target.z);
            }
        }
        return frustum;
    }

protected:
    virtual void updateViewMatrix() = 0;
    virtual void updateProjectionMatrix() = 0;

    void markFrustumDirty() const { frustumDirty = true; }

protected:
    glm::vec3 position{3.0f, 3.0f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    mutable glm::mat4 viewMatrix{1.0f};
    mutable glm::mat4 projectionMatrix{1.0f};
    mutable bool viewDirty = true;
    mutable bool projectionDirty = true;

    mutable Frustum frustum;
    mutable bool frustumDirty = true;
};

}