#pragma once

#include <glm/glm.hpp>

#include <limits>

namespace violet {

struct Frustum; // Forward declaration

struct AABB {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    AABB() = default;
    AABB(const glm::vec3& min_, const glm::vec3& max_) : min(min_), max(max_) {}

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    void expand(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    [[nodiscard]] AABB transform(const glm::mat4& matrix) const {
        AABB result;

        // Transform all 8 corners
        for (int i = 0; i < 8; ++i) {
            glm::vec3 corner(i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z);

            glm::vec4 transformed = matrix * glm::vec4(corner, 1.0f);
            result.expand(glm::vec3(transformed) / transformed.w);
        }

        return result;
    }

    [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }

    [[nodiscard]] glm::vec3 size() const { return max - min; }

    [[nodiscard]] float surfaceArea() const {
        glm::vec3 extents = size();
        return 2.0f * (extents.x * extents.y + extents.y * extents.z + extents.z * extents.x);
    }

    [[nodiscard]] AABB unionOf(const AABB& other) const {
        return AABB(glm::min(min, other.min), glm::max(max, other.max));
    }

    bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }


    void reset() {
        min = glm::vec3(std::numeric_limits<float>::max());
        max = glm::vec3(std::numeric_limits<float>::lowest());
    }
};

} // namespace violet
