#pragma once

#include "render/ed_render.h"

template <typename T> class RenderPool {
public:
    struct Node {
        Node *pPrevious = nullptr;
        Node *pNext = nullptr;
        T *pContent = nullptr;
    };

    using TAllocFun_t = void ( * )( T * );

public:
    RenderPool() : pMemoryPool( nullptr ) {}

    ~RenderPool() {}

    void allocate( int32_t param_2 ) {
        // FUN_00511f90
        if ( param_2 == 0 ) {
            return;
        }

        uint32_t uVar2 = param_2 * sizeof( Node ) + 0xfU & 0xfffffff0;
        pMemoryPool = AllocateFromRenderPool( uVar2 );

        Node *puVar1 = (Node *)pMemoryPool;
        for ( int32_t iVar3 = param_2 - 1; iVar3 != 0; iVar3-- ) {
            puVar1->pNext = puVar1 + 1;
            puVar1 = puVar1 + 1;
        }

        puVar1->pNext = nullptr;
        for ( Node *&pBucket : nodes ) {
            pBucket = nullptr;
        }
    }

    Node *addToPool( T *param_2 ) {
        // FUN_00512040
        Node *pNode = (Node *)pMemoryPool;
        if ( pNode == nullptr || param_2 == nullptr ) {
            return nullptr;
        }

        pMemoryPool = pNode->pNext;

        Node *&pBucket = nodes[GetBucketIndex( param_2 )];
        pNode->pPrevious = nullptr;
        pNode->pNext = pBucket;
        pNode->pContent = param_2;
        if ( pBucket != nullptr ) {
            pBucket->pPrevious = pNode;
        }
        pBucket = pNode;
        return pNode;
    }

    void removeFromPool( T *param_2 ) {
        // FUN_005120a0
        if ( param_2 == nullptr ) {
            return;
        }

        Node *&pBucket = nodes[GetBucketIndex( param_2 )];
        Node *pNode = pBucket;
        while ( pNode != nullptr && pNode->pContent != param_2 ) {
            pNode = pNode->pNext;
        }
        if ( pNode == nullptr ) {
            return;
        }

        if ( pNode->pPrevious == nullptr ) {
            pBucket = pNode->pNext;
        } else {
            pNode->pPrevious->pNext = pNode->pNext;
        }
        if ( pNode->pNext != nullptr ) {
            pNode->pNext->pPrevious = pNode->pPrevious;
        }

        pNode->pNext = (Node *)pMemoryPool;
        pMemoryPool = pNode;
    }

    void flushPendingAllocs( TAllocFun_t allocFun ) {
        // FUN_00512110
        Node *pCVar1 = nullptr;

        uint32_t uVar2 = 0;
        do {
            for ( pCVar1 = nodes[uVar2]; pCVar1 != nullptr;
                  pCVar1 = pCVar1->pNext ) {
                allocFun( pCVar1->pContent );
            }
            uVar2++;
        } while ( uVar2 < kMaxCapacity );
    }

    inline void invalidatePoolPointer() { pMemoryPool = nullptr; }

private:
    static constexpr uint32_t kMaxCapacity = 0x100;

    static inline uint32_t GetBucketIndex( const T *pContent ) {
        return ( reinterpret_cast<uintptr_t>( pContent ) >> 4 ) & 0xff;
    }

private:
    Node *nodes[kMaxCapacity];
    void *pMemoryPool;
};
