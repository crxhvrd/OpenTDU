#pragma once

struct HiearchyNode;

struct HierarchyTriangleSoup {
    std::vector<Eigen::Vector3f> Vertices;
    std::vector<uint32_t> Indices; // 3 per triangle
};

bool GatherHierarchyTriangles( HiearchyNode *pRoot,
                               HierarchyTriangleSoup &outSoup );
