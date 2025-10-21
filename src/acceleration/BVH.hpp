#pragma once

#include <EASTL/vector.h>
#include <EASTL/sort.h>
#include <glm/glm.hpp>
#include "math/AABB.hpp"
#include "math/Ray.hpp"

namespace violet {

struct BVHNode {
    AABB bounds;
    uint32_t firstChild; // For internal: left child index, for leaf: first primitive index
    uint32_t count;      // For internal: 0, for leaf: primitive count
    uint32_t rightChild; // For internal: right child index, for leaf: unused

    bool isLeaf() const { return count > 0; }
};

struct MortonPrimitive {
    uint64_t mortonCode;
    uint32_t primitiveIndex;
    AABB bounds;
};

class BVH {
public:
    void build(const eastl::vector<AABB>& bounds) {
        nodes.clear();
        leafIndices.clear();

        if (bounds.empty()) {
            sceneBounds = AABB();  // Reset to empty
            return;
        }

        // Calculate scene bounding box
        sceneBounds = AABB();
        for (const auto& bound : bounds) {
            sceneBounds.expand(bound);
        }

        // Create Morton primitives
        eastl::vector<MortonPrimitive> mortonPrims;
        mortonPrims.reserve(bounds.size());

        for (uint32_t i = 0; i < bounds.size(); ++i) {
            MortonPrimitive prim;
            prim.primitiveIndex = i;
            prim.bounds = bounds[i];
            prim.mortonCode = mortonCode3D(bounds[i].center(), sceneBounds);
            mortonPrims.push_back(prim);
        }

        // Sort by Morton code
        eastl::sort(mortonPrims.begin(), mortonPrims.end(),
                   [](const MortonPrimitive& a, const MortonPrimitive& b) {
                       return a.mortonCode < b.mortonCode;
                   });

        // Build BVH using Linear BVH algorithm
        buildLinearBVH(mortonPrims);
    }

    // Get scene bounding box (computed during build)
    const AABB& getSceneBounds() const { return sceneBounds; }

    template<typename IntersectionTest, typename LeafHandler>
    void traverse(IntersectionTest&& intersectionTest, LeafHandler&& leafHandler) const {
        if (nodes.empty()) {
            return;
        }

        eastl::vector<uint32_t> stack;
        stack.reserve(64); // Reasonable depth assumption
        stack.push_back(0);

        while (!stack.empty()) {
            uint32_t nodeIndex = stack.back();
            stack.pop_back();

            // Boundary check
            if (nodeIndex >= nodes.size()) {
                // This should help us catch the out of bounds error
                continue;
            }

            const BVHNode& node = nodes[nodeIndex];

            if (!intersectionTest(node.bounds)) {
                continue;
            }

            if (node.isLeaf()) {
                for (uint32_t i = 0; i < node.count; ++i) {
                    uint32_t leafIndex = node.firstChild + i;
                    if (leafIndex < leafIndices.size()) {
                        leafHandler(leafIndices[leafIndex]);
                    }
                }
            } else {
                // Add children to stack (right first, so left is processed first)
                if (node.rightChild < nodes.size()) {
                    stack.push_back(node.rightChild);
                }
                if (node.firstChild < nodes.size()) {
                    stack.push_back(node.firstChild);
                }
            }
        }
    }

private:
    eastl::vector<BVHNode> nodes;
    eastl::vector<uint32_t> leafIndices;
    AABB sceneBounds;  // Overall scene bounding box

    // Morton code utility functions
    inline uint32_t expandBits(uint32_t v) const {
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
    }

    inline uint64_t mortonCode3D(const glm::vec3& pos, const AABB& sceneBounds) const {
        // Normalize position to [0, 1024) range
        glm::vec3 normalized = (pos - sceneBounds.min) / sceneBounds.size();
        normalized = glm::clamp(normalized, glm::vec3(0.0f), glm::vec3(1.0f));

        uint32_t x = static_cast<uint32_t>(normalized.x * 1023.0f);
        uint32_t y = static_cast<uint32_t>(normalized.y * 1023.0f);
        uint32_t z = static_cast<uint32_t>(normalized.z * 1023.0f);

        uint64_t xx = expandBits(x);
        uint64_t yy = expandBits(y);
        uint64_t zz = expandBits(z);

        return (zz << 2) | (yy << 1) | xx;
    }

    inline int longestCommonPrefix(uint64_t a, uint64_t b) const {
        if (a == b) return 64; // All bits match
        return __builtin_clzll(a ^ b); // Count leading zeros of XOR
    }

    void buildLinearBVH(const eastl::vector<MortonPrimitive>& sortedPrims) {
        nodes.clear();
        leafIndices.clear();

        if (sortedPrims.empty()) {
            return;
        }

        // Reserve space for nodes (worst case: 2N-1 for N primitives)
        nodes.reserve(sortedPrims.size() * 2);

        // Build recursively using Morton-sorted primitives
        buildRecursive(sortedPrims, 0, static_cast<uint32_t>(sortedPrims.size()));
    }

    uint32_t buildRecursive(const eastl::vector<MortonPrimitive>& sortedPrims, uint32_t start, uint32_t end) {
        uint32_t nodeIndex = static_cast<uint32_t>(nodes.size());
        nodes.push_back(BVHNode());
        BVHNode& node = nodes[nodeIndex];

        // Compute bounding box for this range
        node.bounds = AABB();
        for (uint32_t i = start; i < end; ++i) {
            node.bounds.expand(sortedPrims[i].bounds);
        }

        uint32_t count = end - start;

        // Create leaf if small enough
        if (count <= 1) {
            node.firstChild = static_cast<uint32_t>(leafIndices.size());
            node.count = count;

            for (uint32_t i = start; i < end; ++i) {
                leafIndices.push_back(sortedPrims[i].primitiveIndex);
            }

            return nodeIndex;
        }

        // Find split point - simple midpoint for now
        uint32_t mid = start + count / 2;

        // Build children
        uint32_t leftChild = buildRecursive(sortedPrims, start, mid);
        uint32_t rightChild = buildRecursive(sortedPrims, mid, end);

        // Set up internal node
        node.firstChild = leftChild;
        node.rightChild = rightChild;
        node.count = 0; // Internal node

        return nodeIndex;
    }


};

} // namespace violet