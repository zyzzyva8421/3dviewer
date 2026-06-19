// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#pragma once

#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <cmath>

namespace viewer3d {
namespace render {
namespace backend_osg {

// Axis-aligned bounding box used by culling and spatial queries.
struct AABB {
    std::array<float, 3> min;
    std::array<float, 3> max;

    AABB() {
        min = {0, 0, 0};
        max = {0, 0, 0};
    }

    AABB(const std::array<float, 3>& minVal, const std::array<float, 3>& maxVal)
        : min(minVal), max(maxVal) {}

    bool contains(const AABB& other) const {
        return min[0] <= other.min[0] && min[1] <= other.min[1] && min[2] <= other.min[2] &&
               max[0] >= other.max[0] && max[1] >= other.max[1] && max[2] >= other.max[2];
    }

    bool intersects(const AABB& other) const {
        return !(other.min[0] > max[0] || other.max[0] < min[0] ||
                other.min[1] > max[1] || other.max[1] < min[1] ||
                other.min[2] > max[2] || other.max[2] < min[2]);
    }

    std::array<float, 3> center() const {
        return {(min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f};
    }

    std::array<float, 3> halfExtent() const {
        return {(max[0] - min[0]) * 0.5f, (max[1] - min[1]) * 0.5f, (max[2] - min[2]) * 0.5f};
    }

    void expand(const std::array<float, 3>& point) {
        min[0] = std::min(min[0], point[0]);
        min[1] = std::min(min[1], point[1]);
        min[2] = std::min(min[2], point[2]);
        max[0] = std::max(max[0], point[0]);
        max[1] = std::max(max[1], point[1]);
        max[2] = std::max(max[2], point[2]);
    }

    void expand(const AABB& other) {
        min[0] = std::min(min[0], other.min[0]);
        min[1] = std::min(min[1], other.min[1]);
        min[2] = std::min(min[2], other.min[2]);
        max[0] = std::max(max[0], other.max[0]);
        max[1] = std::max(max[1], other.max[1]);
        max[2] = std::max(max[2], other.max[2]);
    }
};

// Result of frustum vs. box classification.
enum class FrustumTest {
    Outside = 0,    // Completely outside frustum
    Inside = 1,     // Completely inside frustum
    Intersect = 2   // Partially inside frustum
};

// View frustum represented as six plane equations.
class Frustum {
public:
    Frustum() {}

    // Build frustum planes from a column-major OpenGL-style view-projection matrix.
    // Plane normals point outward, so inside tests use dot(p, n) + d >= 0.
    void setFromMatrix(const float* matrix) {
        // Extract 6 planes from VP matrix
        // Planes are extracted such that normal points OUTWARD from frustum
        // For a point to be inside, dot(point, plane) + plane.w >= 0

        // Left plane (negative X)
        planes_[0] = matrix[3] + matrix[0];
        planes_[1] = matrix[7] + matrix[4];
        planes_[2] = matrix[11] + matrix[8];
        planes_[3] = matrix[15] + matrix[12];

        // Right plane (positive X)
        planes_[4] = matrix[3] - matrix[0];
        planes_[5] = matrix[7] - matrix[4];
        planes_[6] = matrix[11] - matrix[8];
        planes_[7] = matrix[15] - matrix[12];

        // Bottom plane (negative Y)
        planes_[8] = matrix[3] + matrix[1];
        planes_[9] = matrix[7] + matrix[5];
        planes_[10] = matrix[11] + matrix[9];
        planes_[11] = matrix[15] + matrix[13];

        // Top plane (positive Y)
        planes_[12] = matrix[3] - matrix[1];
        planes_[13] = matrix[7] - matrix[5];
        planes_[14] = matrix[11] - matrix[9];
        planes_[15] = matrix[15] - matrix[13];

        // Near plane (negative Z, towards viewer)
        planes_[16] = matrix[3] + matrix[2];
        planes_[17] = matrix[7] + matrix[6];
        planes_[18] = matrix[11] + matrix[10];
        planes_[19] = matrix[15] + matrix[14];

        // Far plane (positive Z, away from viewer)
        planes_[20] = matrix[3] - matrix[2];
        planes_[21] = matrix[7] - matrix[6];
        planes_[22] = matrix[11] - matrix[10];
        planes_[23] = matrix[15] - matrix[14];

        // Normalize planes
        for (int i = 0; i < 6; i++) {
            float len = std::sqrt(planes_[i*4] * planes_[i*4] +
                                   planes_[i*4+1] * planes_[i*4+1] +
                                   planes_[i*4+2] * planes_[i*4+2]);
            if (len > 1e-6f) {
                planes_[i*4] /= len;
                planes_[i*4+1] /= len;
                planes_[i*4+2] /= len;
                planes_[i*4+3] /= len;
            }
        }
    }

    // Debug: print all planes
    void print() const {
        const char* names[] = {"Left", "Right", "Bottom", "Top", "Near", "Far"};
        for (int i = 0; i < 6; i++) {
            fprintf(stderr, "  %s: (%.4f, %.4f, %.4f, %.4f)\n",
                    names[i], planes_[i*4], planes_[i*4+1], planes_[i*4+2], planes_[i*4+3]);
        }
    }

    // Classify an AABB against the frustum.
    // SAT-style plane test returns Outside/Inside/Intersect.
    FrustumTest testBox(const AABB& box) const {
        std::array<float, 3> boxCenter = box.center();
        std::array<float, 3> boxHalf = box.halfExtent();

        bool allInside = true;

        for (int i = 0; i < 6; i++) {
            float nx = planes_[i * 4];
            float ny = planes_[i * 4 + 1];
            float nz = planes_[i * 4 + 2];
            float offset = planes_[i * 4 + 3];

            // Compute distance from box center to plane
            float dist = nx * boxCenter[0] + ny * boxCenter[1] + nz * boxCenter[2] + offset;

            // Compute the projection interval radius
            float radius = std::abs(nx * boxHalf[0]) + std::abs(ny * boxHalf[1]) + std::abs(nz * boxHalf[2]);

            if (dist < -radius) {
                // Box is completely outside this plane
                return FrustumTest::Outside;
            }
            if (dist < radius) {
                // Box intersects this plane
                allInside = false;
            }
        }

        if (allInside) {
            return FrustumTest::Inside;
        }
        return FrustumTest::Intersect;
    }

private:
    // 6 planes, each with normal(xyz) + offset(w)
    float planes_[24];  // 6 * 4
};

// Octree node for hierarchical spatial partitioning.
struct OctreeNode {
    bool isLeaf = true;
    AABB box;

    // Child nodes ordered by bit mask: x(1), y(2), z(4).
    std::array<std::unique_ptr<OctreeNode>, 8> children;

    // IDs of objects stored in this leaf node.
    std::vector<std::string> objectIds;

    OctreeNode() {}

    explicit OctreeNode(const AABB& bounds) : box(bounds) {}
};

// Octree-based spatial index used by the OSG backend.
// The index stores object IDs and AABBs to accelerate view-frustum and box queries.
class SpatialIndex {
public:
    SpatialIndex() : root_(nullptr), maxDepth_(6) {}

    // Build the index from (objectId, bounds) pairs.
    // maxDepth controls recursion limit; lower values reduce node count.
    void build(const std::vector<std::pair<std::string, AABB>>& objects, int maxDepth = 6);

    // Collect IDs of objects that are at least partially inside the frustum.
    void getObjectsInFrustum(const Frustum& frustum, std::vector<std::string>& result);

    // Collect IDs of objects whose node boxes intersect the query box.
    void getObjectsInBox(const AABB& box, std::vector<std::string>& result);

    // Remove all nodes and reset counters.
    void clear();

    size_t getNodeCount() const { return nodeCount_; }

    // Print root-level diagnostics for debugging index construction.
    void debugPrint() const;

private:
    std::unique_ptr<OctreeNode> root_;
    int maxDepth_;
    size_t nodeCount_ = 0;

    // Internal recursive helpers.
    void buildRecursive(OctreeNode& node, const std::vector<std::pair<std::string, AABB>>& objects, int depth);
    void queryFrustumRecursive(const OctreeNode& node, const Frustum& frustum, std::vector<std::string>& result, int depth);
    void queryBoxRecursive(const OctreeNode& node, const AABB& box, std::vector<std::string>& result);
};

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d