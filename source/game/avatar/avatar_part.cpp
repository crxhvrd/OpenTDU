#include "shared.h"
#include "avatar_part.h"

#include "avatar_bnk.h"
#include "database/gs_database.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_avatar.h"

static const DBEntryClothe *GetClotheRow( const int32_t index ) {
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numRows =
        static_cast<int32_t>( gameDB.pNumItems[kClotheDatabaseIndex] );
    if ( numRows == 0 || gameDB.DBClothe == nullptr || index < 0 ||
         index >= numRows ) {
        return nullptr;
    }

    return &gameDB.DBClothe[index];
}

static const uint16_t *GetOwnedColorMask( const uint16_t id ) {
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr || pLoadedSave->getAvatar() == nullptr ) {
        return nullptr;
    }

    const uint16_t *pMasks = reinterpret_cast<const uint16_t *>(
        pLoadedSave->getAvatar()->AvatarData );
    return &pMasks[id];
}

uint64_t AvatarDBEntry::getMeshHashcode() const {
    // FUN_008edf20 / FUN_008edf80
    const char *pMeshName = getMeshName();
    return ( pMeshName != nullptr ) ? AvatarCalcHashcode( pMeshName ) : 0ull;
}

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

const char *AvatarClothePart::getDisplayName() const {
    // FUN_008ee0b0
    const DBEntryClothe *pRow = GetClotheRow( Index );
    return ( pRow != nullptr ) ? pRow->ClotheName.pAsString : nullptr;
}

const char *AvatarClothePart::getBrandName() const {
    // FUN_008ee060
    if ( Category == AC_Hair || Category == AC_FacePilosity ||
         Category == AC_Eyebrows ) {
        return "COMMON";
    }

    const DBEntryClothe *pRow = GetClotheRow( Index );
    if ( pRow == nullptr ) {
        return nullptr;
    }

    const DBEntryBrand *pBrand =
        static_cast<const DBEntryBrand *>( pRow->Brand.pAsRawPointer );
    return ( pBrand != nullptr ) ? pBrand->BrandID.pAsString : nullptr;
}

bool AvatarClothePart::isOwned() const {
    // FUN_008ede20
    const uint16_t *pMask = GetOwnedColorMask( IDHashcode );
    return pMask != nullptr && *pMask != 0;
}

bool AvatarClothePart::isColorOwned( const uint8_t colorIndex ) const {
    // FUN_008ede50
    const uint16_t *pMask = GetOwnedColorMask( IDHashcode );
    return pMask != nullptr &&
           ( *pMask & ( 1u << ( colorIndex & 0x1f ) ) ) != 0;
}

void AvatarClothePart::setColorOwned( const uint8_t colorIndex ) {
    // FUN_008eddf0
    uint16_t *pMask = const_cast<uint16_t *>( GetOwnedColorMask( IDHashcode ) );
    if ( pMask != nullptr ) {
        *pMask |= static_cast<uint16_t>( 1u << ( colorIndex & 0x1f ) );
    }
}

int32_t AvatarClothePart::getNumColors() const {
    // FUN_008edf50
    const DBEntryClothe *pRow = GetClotheRow( Index );
    return ( pRow != nullptr ) ? pRow->NumColors : 0;
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

void AvatarPartCursor::seekNextMatch() {
    const int32_t nextIndex = CurIndex + 1;
    if ( LastMatchIndex <= nextIndex ) {
        CurIndex = nextIndex;
        return;
    }

    for ( int32_t i = nextIndex; i <= LastMatchIndex; i++ ) {
        if ( i < pDB->getCount() &&
             matchesCategory(
                 static_cast<AvatarDBEntry *>( pDB->getElement( i ) ) ) ) {
            CurIndex = i;
            return;
        }
    }
}

void AvatarPartCursor::seekPreviousMatch() {
    for ( int32_t i = CurIndex - 1; i >= FirstMatchIndex; i-- ) {
        if ( i < pDB->getCount() &&
             matchesCategory(
                 static_cast<AvatarDBEntry *>( pDB->getElement( i ) ) ) ) {
            CurIndex = i;
            return;
        }
    }
}

AvatarClotheCursor::AvatarClotheCursor( AvatarPartDB *pTargetDB,
                                        const int32_t gender,
                                        const eAvatarCustomizable category,
                                        const int32_t clotheSet )
    : AvatarPartCursor( pTargetDB ),
      Gender( gender ),
      Category( category ),
      ClotheSet( clotheSet ) {}

bool AvatarClotheCursor::matchesCategory( const AvatarDBEntry *pEntry ) {
    // FUN_008ed9d0
    const AvatarClothePart *pPart =
        static_cast<const AvatarClothePart *>( pEntry );
    const bool bMatchesSet = ClotheSet == kAnyClotheSet ||
                             pPart->DrivingType == ClotheSet ||
                             pPart->DrivingType == ADT_Mixte;
    return pPart->Gender == Gender && pPart->Category == Category &&
           bMatchesSet;
}

bool AvatarClotheCursor::isAtEnd() {
    // FUN_0040ed70
    return LastMatchIndex < CurIndex;
}

void AvatarClotheCursor::next() {
    // FUN_0040ed90
    seekNextMatch();
}

void AvatarClotheCursor::previous() {
    // FUN_0040ede0
    seekPreviousMatch();
}

bool AvatarClotheCursor::seekTo( const uint16_t id ) {
    // FUN_0040f280
    if ( pDB->getCount() <= 0 || !hasRange() ) {
        return false;
    }

    CurIndex = FirstMatchIndex;
    while ( !isAtEnd() ) {
        const AvatarDBEntry *pPart = getResult();
        if ( pPart->IDHashcode == id ) {
            return true;
        }
        next();
    }
    return false;
}

bool AvatarOwnedClotheCursor::matchesCategory( const AvatarDBEntry *pEntry ) {
    // FUN_008eda80
    return AvatarClotheCursor::matchesCategory( pEntry ) &&
           static_cast<const AvatarClothePart *>( pEntry )->isOwned();
}

AvatarBrandClotheCursor::AvatarBrandClotheCursor(
    AvatarPartDB *pTargetDB,
    const int32_t gender,
    const eAvatarCustomizable category,
    const int32_t clotheSet,
    const char *pBrandName )
    : AvatarClotheCursor( pTargetDB, gender, category, clotheSet ),
      pBrand( pBrandName ) {
    // FUN_008edbe0
}

bool AvatarBrandClotheCursor::matchesCategory( const AvatarDBEntry *pEntry ) {
    // FUN_008edb30
    return AvatarClotheCursor::matchesCategory( pEntry ) &&
           strcmp(
               static_cast<const AvatarClothePart *>( pEntry )->getBrandName(),
               pBrand ) == 0;
}

void AvatarWrappingClotheCursor::next() {
    // FUN_008ed9b0
    if ( CurIndex == LastMatchIndex ) {
        CurIndex = FirstMatchIndex;
        return;
    }
    AvatarClotheCursor::next();
}

void AvatarWrappingClotheCursor::previous() {
    // FUN_008ed990
    if ( CurIndex == FirstMatchIndex ) {
        CurIndex = LastMatchIndex;
        return;
    }
    AvatarClotheCursor::previous();
}

AvatarGenderCursor::AvatarGenderCursor( AvatarPartDB *pTargetDB,
                                        const int32_t gender )
    : AvatarPartCursor( pTargetDB ), Gender( gender ) {}

bool AvatarGenderCursor::matchesCategory( const AvatarDBEntry *pEntry ) {
    // FUN_0040f180
    return pEntry->Gender == Gender;
}

bool AvatarGenderCursor::isAtEnd() {
    // FUN_0040ed70
    return LastMatchIndex < CurIndex;
}

void AvatarGenderCursor::next() {
    // FUN_0040f340
    seekNextMatch();
}

void AvatarGenderCursor::previous() {
    // FUN_0040f390
    seekPreviousMatch();
}

bool AvatarGenderCursor::seekTo( const uint16_t id ) {
    // FUN_0040f210
    if ( pDB->getCount() <= 0 || !hasRange() ) {
        return false;
    }

    CurIndex = FirstMatchIndex;
    while ( !isAtEnd() ) {
        const AvatarDBEntry *pPart = getResult();
        if ( pPart->IDHashcode == id ) {
            return true;
        }
        next();
    }
    return false;
}

void AvatarWrappingGenderCursor::next() {
    // FUN_0040f420
    if ( CurIndex == LastMatchIndex ) {
        CurIndex = FirstMatchIndex;
        return;
    }
    AvatarGenderCursor::next();
}

void AvatarWrappingGenderCursor::previous() {
    // FUN_0040f440
    if ( CurIndex == FirstMatchIndex ) {
        CurIndex = LastMatchIndex;
        return;
    }
    AvatarGenderCursor::previous();
}
