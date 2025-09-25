#pragma once

#include <glm/glm.hpp>
#include <limits>
#include "math/AABB.hpp"

namespace violet {

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    glm::vec3 invDirection;
    float tMin;
    float tMax;

    Ray(const glm::vec3& o, const glm::vec3& d, float tMin_ = 0.0f, float tMax_ = std::numeric_limits<float>::max())
        : origin(o), direction(d), tMin(tMin_), tMax(tMax_) {
        invDirection = glm::vec3(1.0f) / direction;
    }

    bool intersectAABB(const AABB& aabb) const {
        float tNear, tFar;
        return intersectAABB(aabb, tNear, tFar);
    }

    bool intersectAABB(const AABB& aabb, float& tNear, float& tFar) const {
        glm::vec3 t1 = (aabb.min - origin) * invDirection;
        glm::vec3 t2 = (aabb.max - origin) * invDirection;

        glm::vec3 tmin = glm::min(t1, t2);
        glm::vec3 tmax = glm::max(t1, t2);

        tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
        tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

        return tNear <= tFar && tFar >= tMin && tNear <= tMax;
    }
};

} // namespace violet