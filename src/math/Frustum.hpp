#pragma once

#include <glm/glm.hpp>
#include <array>
#include "math/AABB.hpp"
#include "core/Log.hpp"

namespace violet {

struct Frustum {
    std::array<glm::vec4, 6> planes; // left, right, bottom, top, near, far

    void extract(const glm::mat4& viewProj) {
        // Extract frustum planes from view-projection matrix
        // Planes are in world space, pointing inward

        // Left
        planes[0] = glm::vec4(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
        );

        // Right
        planes[1] = glm::vec4(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0],
            viewProj[3][3] - viewProj[3][0]
        );

        // Bottom
        planes[2] = glm::vec4(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1],
            viewProj[3][3] + viewProj[3][1]
        );

        // Top
        planes[3] = glm::vec4(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1],
            viewProj[3][3] - viewProj[3][1]
        );

        // Near
        planes[4] = glm::vec4(
            viewProj[0][3] + viewProj[0][2],
            viewProj[1][3] + viewProj[1][2],
            viewProj[2][3] + viewProj[2][2],
            viewProj[3][3] + viewProj[3][2]
        );

        // Far
        planes[5] = glm::vec4(
            viewProj[0][3] - viewProj[0][2],
            viewProj[1][3] - viewProj[1][2],
            viewProj[2][3] - viewProj[2][2],
            viewProj[3][3] - viewProj[3][2]
        );

        // Normalize planes
        for (auto& plane : planes) {
            float length = glm::length(glm::vec3(plane));
            plane /= length;
        }
    }

    bool testAABB(const AABB& box) const {
        // Only test the first 5 planes (left, right, bottom, top, near)
        // Skip far plane (index 5) for culling
        for (int i = 0; i < 5; i++) {
            const auto& plane = planes[i];
            // Find the vertex furthest in the direction of the plane normal
            // This is the "positive vertex" - if it's outside, the whole AABB is outside
            glm::vec3 p(
                plane.x > 0 ? box.max.x : box.min.x,
                plane.y > 0 ? box.max.y : box.min.y,
                plane.z > 0 ? box.max.z : box.min.z
            );

            // For plane equation n·p + d = 0, if n·p + d < 0, point is outside
            float distance = glm::dot(glm::vec3(plane), p) + plane.w;
            if (distance < 0) {
                return false;
            }
        }
        return true;
    }

    // Debug version with detailed logging
    bool testAABBDebug(const AABB& box, int objectIndex = -1) const {
        const char* planeNames[] = {"left", "right", "bottom", "top", "near", "far"};

        for (int i = 0; i < 5; i++) {
            const auto& plane = planes[i];
            glm::vec3 p(
                plane.x > 0 ? box.max.x : box.min.x,
                plane.y > 0 ? box.max.y : box.min.y,
                plane.z > 0 ? box.max.z : box.min.z
            );

            float distance = glm::dot(glm::vec3(plane), p) + plane.w;
            bool passes = distance >= 0;

            if (objectIndex >= 0 && objectIndex < 3) {  // Only log first 3 objects
                violet::Log::info("Frustum", "Obj {} {} plane: vertex({:.1f},{:.1f},{:.1f}) dist={:.3f} -> {}",
                    objectIndex, planeNames[i], p.x, p.y, p.z, distance, passes ? "PASS" : "FAIL");
            }

            if (!passes) {
                return false;
            }
        }
        return true;
    }
};

} // namespace violet