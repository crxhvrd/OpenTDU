#pragma once

// NOTE: The allocator logic is the same as the original game; only difference
// is that we use std containers instead of the custom ones the game uses (TDU1
// uses double linked list most of the time)
template <class T> class edStdAllocator {
public:
    struct Node {
        T *pContent;
        Node *pPreviousNode;
        Node *pNextNode;
    };

public:
    using value_type = T;

    edStdAllocator() noexcept
        : pPooledMemory( nullptr ),
          pHead( nullptr ),
          pContentBase( nullptr ),
          pFirstFreeNode( nullptr ),
          Capacity( 0 ),
          Usage( 0 ) {}

    template <class U>
    edStdAllocator( const edStdAllocator<U> & ) noexcept : edStdAllocator() {}

    static constexpr size_t GetRequiredMemory( size_t param_1 ) {
        return param_1 * sizeof( Node ) + alignof( T ) - 1 +
               param_1 * sizeof( T );
    }

    void initialize( void *param_1, size_t param_2 ) {
        pPooledMemory = param_1;
        Capacity = param_2;
        Usage = 0;
        pHead = (Node *)pPooledMemory;
        pFirstFreeNode = nullptr;
        pContentBase = nullptr;

        if ( Capacity == 0 ) {
            return;
        }
        uintptr_t contentPtr =
            (uintptr_t)( (uint8_t *)pHead + sizeof( Node ) * Capacity );
        pContentBase = (T *)Align( contentPtr, alignof( T ) );

        for ( size_t i = 0; i < Capacity; i++ ) {
            Node *pNode = pHead + i;
            pNode->pContent = new ( pContentBase + i ) T();
            pNode->pNextNode =
                ( i + 1 < Capacity ) ? ( pHead + i + 1 ) : nullptr;
            MarkNodeFree( pNode );
        }

        pFirstFreeNode = pHead;
    }

    T *allocate( std::size_t n ) {
        OTDU_ASSERT( n == 1 );

        Node *peVar1 = pFirstFreeNode;
        if ( peVar1 == nullptr ) {
            OTDU_FATAL_ERROR( "Pool is exhausted (%llu/%llu in use)!",
                              (unsigned long long)Usage,
                              (unsigned long long)Capacity );
            return nullptr;
        }

        Usage++;
        pFirstFreeNode = peVar1->pNextNode;
        peVar1->pPreviousNode = nullptr;
        peVar1->pNextNode = nullptr;

        return peVar1->pContent;
    }

    void deallocate( T *p, std::size_t n ) {
        Node *pNode = findNode( p );
        if ( pNode == nullptr ) {
            OTDU_LOG_ERROR( "Tried to free %p, which does not belong to this "
                            "pool!\n",
                            (void *)p );
            OTDU_TRIGGER_BREAKPOINT;
            return;
        }

        if ( IsNodeFree( pNode ) ) {
            OTDU_LOG_ERROR( "Double free of pooled block %p!\n", (void *)p );
            OTDU_TRIGGER_BREAKPOINT;
            return;
        }

        Usage--;
        pNode->pNextNode = pFirstFreeNode;
        MarkNodeFree( pNode );
        pFirstFreeNode = pNode;
    }

    void *pPooledMemory;
    Node *pHead;
    T *pContentBase;
    Node *pFirstFreeNode;
    uint64_t Capacity;
    uint64_t Usage;

private:
    static inline void MarkNodeFree( Node *param_1 ) {
        param_1->pPreviousNode = param_1;
    }

    static inline bool IsNodeFree( const Node *param_1 ) {
        return param_1->pPreviousNode == param_1;
    }

    Node *findNode( const T *param_1 ) const {
        if ( param_1 == nullptr || pContentBase == nullptr ) {
            return nullptr;
        }

        const uintptr_t offset = (uintptr_t)param_1 - (uintptr_t)pContentBase;
        if ( ( offset % sizeof( T ) ) != 0 ) {
            return nullptr;
        }

        const uint64_t index = offset / sizeof( T );
        if ( index >= Capacity ) {
            return nullptr;
        }

        return pHead + index;
    }
};
