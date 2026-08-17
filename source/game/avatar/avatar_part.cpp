#include "shared.h"
#include "avatar_part.h"

#include "database/gs_database.h"

const char *AvatarClothePart::getMeshName() const {
    // FUN_008edfd0
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numRows =
        static_cast<int32_t>( gameDB.pNumItems[kClotheDatabaseIndex] );
    if ( numRows == 0 || gameDB.DBClothe == nullptr || Index < 0 ||
         Index >= numRows ) {
        return nullptr;
    }

    return gameDB.DBClothe[Index].File.pAsString;
}

const char *AvatarClothePart::getBankName() const {
    // FUN_008ee030 (byte-identical to FUN_008edfd0)
    return AvatarClothePart::getMeshName();
}

const char *AvatarClothePart::getBrandFolder() const {
    // FUN_008ee000
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numRows =
        static_cast<int32_t>( gameDB.pNumItems[kClotheDatabaseIndex] );
    if ( numRows == 0 || gameDB.DBClothe == nullptr || Index < 0 ||
         Index >= numRows ) {
        return nullptr;
    }

    const DBEntryBrand *pBrand = static_cast<const DBEntryBrand *>(
        gameDB.DBClothe[Index].Brand.pAsRawPointer );
    return ( pBrand != nullptr ) ? pBrand->BrandID.pAsString : nullptr;
}

AvatarPartDB::AvatarPartDB() {}

AvatarPartDB::~AvatarPartDB() { delete[] static_cast<uint8_t *>( pElements ); }

void AvatarPartDB::reserve( int32_t capacity,
                            int32_t elementSize,
                            void ( *pConstructor )( void * ) ) {
    // FUN_00410140 (one variant per descriptor type; they differ only by the
    // block size and the constructor handed to edMemMaster::BaseAllocate)
    delete[] static_cast<uint8_t *>( pElements );
    Capacity = capacity;
    ElementSize = elementSize;
    NumElements = 0;
    pElementConstructor = pConstructor;
    pElements = ( capacity > 0 && elementSize > 0 )
                    ? new uint8_t[capacity * elementSize]()
                    : nullptr;
}

void AvatarPartDB::clear() { NumElements = 0; }

void *AvatarPartDB::addSlot() {
    if ( NumElements >= Capacity ) {
        return nullptr;
    }

    void *pSlot = getElement( NumElements++ );
    if ( pElementConstructor != nullptr ) {
        pElementConstructor( pSlot );
    }

    return pSlot;
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
