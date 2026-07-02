#include "shared.h"
#include "avatar_part.h"

AvatarPartDB::AvatarPartDB() {}

AvatarPartDB::~AvatarPartDB() { delete[] static_cast<uint8_t *>( pElements ); }

void AvatarPartDB::reserve( int32_t capacity, int32_t elementSize ) {
    // FUN_00410140
    delete[] static_cast<uint8_t *>( pElements );
    Capacity = capacity;
    ElementSize = elementSize;
    NumElements = 0;
    pElements = ( capacity > 0 && elementSize > 0 )
                    ? new uint8_t[capacity * elementSize]()
                    : nullptr;
}

void AvatarPartDB::clear() { NumElements = 0; }

void *AvatarPartDB::addSlot() {
    if ( NumElements >= Capacity ) {
        return nullptr;
    }
    return getElement( NumElements++ );
}

AvatarPartCursor::AvatarPartCursor( AvatarPartDB *pTargetDB )
    : pDB( pTargetDB ) {}

void AvatarPartCursor::findRange() {
    // FUN_0040f610
    FirstMatchIndex = -1;
    LastMatchIndex = -1;

    const int32_t count = pDB->getCount();
    for ( int32_t i = 0; i < count; ++i ) {
        const AvatarDBEntry *pPart =
            static_cast<const AvatarDBEntry *>( pDB->getElement( i ) );
        if ( pPart != nullptr && matchesCategory( pPart ) ) {
            FirstMatchIndex = i;
            break;
        }
    }
    for ( int32_t i = count - 1; i >= 0; --i ) {
        const AvatarDBEntry *pPart =
            static_cast<const AvatarDBEntry *>( pDB->getElement( i ) );
        if ( pPart != nullptr && matchesCategory( pPart ) ) {
            LastMatchIndex = i;
            break;
        }
    }

    CurIndex = FirstMatchIndex;
}

bool AvatarPartCursor::findById( uint16_t id ) {
    // FUN_0040ef70
    if ( pDB->getCount() <= 0 || !hasRange() ) {
        return false;
    }
    for ( CurIndex = FirstMatchIndex; !isAtEnd(); advance() ) {
        const AvatarDBEntry *pPart =
            static_cast<const AvatarDBEntry *>( pDB->getElement( CurIndex ) );
        if ( pPart != nullptr && pPart->IDHashcode == id ) {
            return true;
        }
    }
    return false;
}
