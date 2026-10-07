#pragma once

#include "render/collection_link.h"

class Instance;
struct HiearchyNodeEntry;

struct HiearchyNode {
    uint32_t Flags;
    Instance *pInstance;
    uint16_t NumChildren;
    HiearchyNode *pParent;
    HiearchyNode *pNext;
    HiearchyNode *pPrevious;
    Matrix4x4f_t TransformMatrix;
    CollectionLink<HiearchyNode> collectionLink;

    void bindInstance( HiearchyNodeEntry *param_2, Instance *param_3 );
    HiearchyNode *findChild( uint64_t param_2 );
    HiearchyNode *findChild( const char *param_2 );
    Instance *findInstance( uint64_t param_2 );
    void propagateTransformMatrices();
    void updateAndPropagateTransformMatrices( const bool param_1_00 );
    void propagateTransformMatricesWithFlag();
    void propagateTransformMatricesNoScale();
    void setAndPropagateFlags( uint32_t param_2 );
    void unsetAndPropagateFlags( uint32_t param_2 );
    void clear();
};

inline int32_t AccumulateNodeWorldMatrix( const HiearchyNode *pNode,
                                          Matrix4x4f_t &inOutMatrix ) {
    // FUN_008046a0
    if ( pNode == nullptr ) {
        return 0;
    }

    int32_t depth = 1;
    if ( pNode->pParent == nullptr ) {
        return depth;
    }

    inOutMatrix = pNode->TransformMatrix;

    for ( const HiearchyNode *pParent = pNode->pParent;
          pParent->pParent != nullptr; pParent = pParent->pParent ) {
        depth++;
        inOutMatrix = inOutMatrix * pParent->TransformMatrix;
    }
    return depth;
}

struct HiearchyNodeInstance {
    Matrix4x4f_t InstanceMatrix;
    HiearchyNode *pNode;
    Instance *pInstance;
};
