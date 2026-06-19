// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, The OpenROAD Authors

#include "SpatialIndex.h"

namespace viewer3d {
namespace render {
namespace backend_osg {

void SpatialIndex::build(const std::vector<std::pair<std::string, AABB>>& objects, int maxDepth) {
    clear();
    maxDepth_ = maxDepth;

    if (objects.empty()) {
        return;
    }

    // Compute bounding box of all objects
    AABB worldBounds;
    for (const auto& [id, box] : objects) {
        worldBounds.expand(box);
    }

    // Add small margin to bounds
    for (int i = 0; i < 3; i++) {
        float margin = (worldBounds.max[i] - worldBounds.min[i]) * 0.01f;
        if (margin < 1e-4f) margin = 1.0f;
        worldBounds.min[i] -= margin;
        worldBounds.max[i] += margin;
    }

    root_ = std::make_unique<OctreeNode>(worldBounds);
    buildRecursive(*root_, objects, 0);
}

void SpatialIndex::buildRecursive(OctreeNode& node, const std::vector<std::pair<std::string, AABB>>& objects, int depth) {
    if (objects.size() <= 1 || depth >= maxDepth_) {
        node.isLeaf = true;
        node.objectIds.reserve(objects.size());
        for (const auto& [id, box] : objects) {
            node.objectIds.push_back(id);
            (void)box;  // suppress unused warning
        }
        nodeCount_++;
        return;
    }

    node.isLeaf = false;
    node.objectIds.clear();

    // Split objects into 8 children
    std::array<std::vector<std::pair<std::string, AABB>>, 8> childObjects;
    std::array<AABB, 8> childBounds;

    std::array<float, 3> center = node.box.center();

    // Initialize child bounds
    for (int i = 0; i < 8; i++) {
        bool xMax = i & 1;
        bool yMax = i & 2;
        bool zMax = i & 4;

        childBounds[i].min = {
            xMax ? center[0] : node.box.min[0],
            yMax ? center[1] : node.box.min[1],
            zMax ? center[2] : node.box.min[2]
        };
        childBounds[i].max = {
            xMax ? node.box.max[0] : center[0],
            yMax ? node.box.max[1] : center[1],
            zMax ? node.box.max[2] : center[2]
        };
    }

    // Assign objects to children based on AABB center
    for (const auto& [id, box] : objects) {
        auto boxCenter = box.center();
        int childIdx = 0;
        if (boxCenter[0] >= center[0]) childIdx |= 1;
        if (boxCenter[1] >= center[1]) childIdx |= 2;
        if (boxCenter[2] >= center[2]) childIdx |= 4;

        childObjects[childIdx].emplace_back(id, box);
    }

    // Create children and recurse
    for (int i = 0; i < 8; i++) {
        if (!childObjects[i].empty()) {
            node.children[i] = std::make_unique<OctreeNode>(childBounds[i]);
            buildRecursive(*node.children[i], childObjects[i], depth + 1);
        }
    }

    nodeCount_++;
}

void SpatialIndex::getObjectsInFrustum(const Frustum& frustum, std::vector<std::string>& result) {
    result.clear();
    if (!root_) {
        return;
    }
    queryFrustumRecursive(*root_, frustum, result, 0);
}

void SpatialIndex::queryFrustumRecursive(const OctreeNode& node, const Frustum& frustum, std::vector<std::string>& result, int depth) {
    FrustumTest test = frustum.testBox(node.box);

    if (test == FrustumTest::Outside) {
        return;  // Prune entire subtree
    }

    if (node.isLeaf) {
        // Leaf node: add all objects
        result.insert(result.end(), node.objectIds.begin(), node.objectIds.end());
        return;
    }

    if (test == FrustumTest::Inside) {
        // Non-leaf node marked as Inside: recursively collect from children
        for (int i = 0; i < 8; i++) {
            if (node.children[i]) {
                queryFrustumRecursive(*node.children[i], frustum, result, depth + 1);
            }
        }
        return;
    }

    // Partial intersection - check children
    for (int i = 0; i < 8; i++) {
        if (node.children[i]) {
            queryFrustumRecursive(*node.children[i], frustum, result, depth + 1);
        }
    }
}

// Debug helper
void SpatialIndex::debugPrint() const {
    if (!root_) {
        fprintf(stderr, "DEBUG SpatialIndex: root is null\n");
        return;
    }
    fprintf(stderr, "DEBUG SpatialIndex: root.box min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)\n",
            root_->box.min[0], root_->box.min[1], root_->box.min[2],
            root_->box.max[0], root_->box.max[1], root_->box.max[2]);
    fprintf(stderr, "DEBUG SpatialIndex: root.objectIds.size=%zu, isLeaf=%d\n",
            root_->objectIds.size(), root_->isLeaf);
    int childCount = 0;
    for (int i = 0; i < 8; i++) {
        if (root_->children[i]) childCount++;
    }
    fprintf(stderr, "DEBUG SpatialIndex: root has %d children\n", childCount);
}

void SpatialIndex::getObjectsInBox(const AABB& box, std::vector<std::string>& result) {
    result.clear();
    if (!root_) {
        return;
    }
    queryBoxRecursive(*root_, box, result);
}

void SpatialIndex::queryBoxRecursive(const OctreeNode& node, const AABB& box, std::vector<std::string>& result) {
    if (!node.box.intersects(box)) {
        return;
    }

    if (node.isLeaf) {
        result.insert(result.end(), node.objectIds.begin(), node.objectIds.end());
        return;
    }

    for (int i = 0; i < 8; i++) {
        if (node.children[i]) {
            queryBoxRecursive(*node.children[i], box, result);
        }
    }
}

void SpatialIndex::clear() {
    root_.reset();
    nodeCount_ = 0;
}

}  // namespace backend_osg
}  // namespace render
}  // namespace viewer3d