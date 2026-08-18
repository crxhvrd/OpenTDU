#pragma once

// A keyframe list evaluated by linear interpolation between its points.
template <int32_t TPoolSize> class ResponseCurve {
public:
    struct Element {
        float X = 0.0f;
        float Y = 0.0f;
        Element *pNext = nullptr;
        Element *pPrevious = nullptr;
    };

public:
    inline int32_t getNumPoints() const { return NumElements; }
    inline const Element *getFirstPoint() const { return pFirst; }

public:
    void addPoint( float x, float y );
    float evaluate( float x ) const;
    void clear();

private:
    void pushBack( Element *pElement );

private:
    Element *pFirst = nullptr;
    Element *pLast = nullptr;
    int32_t NumElements = 0;

    Element ElementPool[TPoolSize] = {};
    uint32_t NumUsedElemInPool = 0;
};

template <int32_t TPoolSize>
void ResponseCurve<TPoolSize>::addPoint( float x, float y ) {
    // FUN_0040c000
    if ( NumUsedElemInPool >= static_cast<uint32_t>( TPoolSize ) ) {
        return;
    }

    Element *pElement = &ElementPool[NumUsedElemInPool];
    pElement->X = x;
    pElement->Y = y;

    for ( Element *pIt = pFirst; pIt != nullptr; pIt = pIt->pNext ) {
        // Two points may not share an abscissa
        if ( pIt->X == x ) {
            return;
        }

        if ( x < pIt->X ) {
            NumUsedElemInPool++;
            pElement->pNext = pIt;
            pElement->pPrevious = pIt->pPrevious;
            if ( pIt->pPrevious != nullptr ) {
                pIt->pPrevious->pNext = pElement;
            }
            pIt->pPrevious = pElement;
            if ( pFirst == pIt ) {
                pFirst = pElement;
            }
            NumElements++;
            return;
        }
    }

    NumUsedElemInPool++;
    pushBack( pElement );
}

template <int32_t TPoolSize>
void ResponseCurve<TPoolSize>::pushBack( Element *pElement ) {
    // FUN_00425670
    pElement->pNext = nullptr;
    pElement->pPrevious = pLast;
    if ( pLast != nullptr ) {
        pLast->pNext = pElement;
    } else {
        pFirst = pElement;
    }
    pLast = pElement;
    NumElements++;
}

template <int32_t TPoolSize>
float ResponseCurve<TPoolSize>::evaluate( float x ) const {
    // FUN_009044e0 (inlined)
    const Element *pIt = pFirst;
    if ( pIt == nullptr ) {
        return 0.0f;
    }
    if ( x <= pIt->X ) {
        return pIt->Y;
    }

    const Element *pPrevious = nullptr;
    while ( pIt != nullptr ) {
        if ( x <= pIt->X ) {
            const float t = ( x - pPrevious->X ) / ( pIt->X - pPrevious->X );
            return ( 1.0f - t ) * pPrevious->Y + pIt->Y * t;
        }
        pPrevious = pIt;
        pIt = pIt->pNext;
    }

    // Past the last point: the curve holds its final value.
    return pPrevious->Y;
}

template <int32_t TPoolSize> void ResponseCurve<TPoolSize>::clear() {
    pFirst = nullptr;
    pLast = nullptr;
    NumElements = 0;
    NumUsedElemInPool = 0;
}
