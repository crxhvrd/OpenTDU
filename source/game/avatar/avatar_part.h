#pragma once

#include <cstdint>

#include "avatar_configuration.h"

// Index of TDU_Clothes in GameDatabase::pNumItems (kDatabasesToLoad).
static constexpr int32_t kClotheDatabaseIndex = 9;

enum eAvatarClotheLength : int32_t {
    ACL_Long = 0,    // LONG_CLOTHE
    ACL_Short = 1,   // SHORT_CLOTHE
    ACL_None = 2,    // NO_LENGHT (sic.)
    ACL_Invalid = 3, // anything else
};

enum eAvatarClotheSize : int32_t {
    ACS_Small = 0,
    ACS_Large = 1,
    ACS_Any = 2, // anything else
};

enum eAvatarBeltSeat : int32_t {
    ABS_Driver = 0,
    ABS_Passenger = 1,
    ABS_Any = 2,
};
enum eAvatarBeltType : int32_t {
    ABT_TwoPoint = 0,
    ABT_ThreePoint = 1,
    ABT_FourPoint = 2,
};

enum eAvatarDrivingType : int32_t {
    ADT_Car = 0,
    ADT_Bike = 1,
    ADT_Mixte = 2,   // valid for both sets
    ADT_Invalid = 3, // anything else
};

struct AvatarDBEntry {
    uint16_t IDHashcode = 0;
    uint16_t Unknown2 = 0;
    int32_t Gender = 0;

    virtual ~AvatarDBEntry() {}

    virtual const char *getMeshName() const { return nullptr; }
    virtual const char *getBankName() const { return nullptr; }
    virtual const char *getCategoryFolder() const { return nullptr; }
    virtual const char *getBrandFolder() const { return nullptr; }
    virtual bool isCommonPart() const { return false; }
    virtual uint64_t getMeshHashcode() const;
};

struct AvatarClothePart : public AvatarDBEntry {
    eAvatarCustomizable Category = AC_Count;
    eAvatarClotheLength Length = ACL_Invalid;
    eAvatarClotheSize Size = ACS_Any;
    eAvatarDrivingType DrivingType = ADT_Invalid;
    int32_t Price = 0;
    uint8_t bIsBodyPart = 0;
    uint8_t bIsPartBoth = 0;
    // index of the source row in TDU_Clothes; -1 when hardcoded
    int32_t Index = -1;

    const char *getMeshName() const override;
    const char *getBankName() const override;
    const char *getCategoryFolder() const override { /* FUN_008edef0 */
        return "CLOTHES";
    }
    const char *getBrandFolder() const override;
    bool isCommonPart() const override { /* FUN_008edd80 */
        return bIsBodyPart != 0;
    }

    const char *getDisplayName() const;
    const char *getBrandName() const;
    bool isOwned() const;
    bool isColorOwned( uint8_t colorIndex ) const;
    int32_t getNumColors() const;
};

// The hardcoded default-parts entries carry their asset names inline
struct AvatarDefaultPart : public AvatarClothePart {
    const char *pMeshName = nullptr;
    const char *pDisplayName = nullptr;
    const char *pBrandName = nullptr;
    int32_t BrandID = 0;

    const char *getMeshName() const override { /* FUN_008edd40 */
        return pMeshName;
    }
    const char *getBankName() const override { /* FUN_008edd20 */
        return pMeshName;
    }
    const char *getBrandFolder() const override { /* FUN_008edd00 */
        return pBrandName;
    }
};

struct AvatarBeltPart : public AvatarClothePart {
    const char *pPartTypeName = nullptr; // always "BELT"
    const char *pBankName = nullptr;     // always "DEFAULT_BELTS"
    const char *pMeshName = nullptr;
    uint64_t MeshHashcode = 0; // AvatarCalcHashcode( pMeshName )
    eAvatarBeltSeat Seat = ABS_Any;
    eAvatarBeltType BeltType = ABT_TwoPoint;

    const char *getMeshName() const override { /* FUN_008ee410 */
        return pMeshName;
    }
    const char *getBankName() const override { /* FUN_008ee420 */
        return pBankName;
    }
    const char *getBrandFolder() const override { /* FUN_008ee450 */
        return "BELTS";
    }
    bool isCommonPart() const override { /* FUN_008ee440 */ return true; }
    uint64_t getMeshHashcode() const override { /* FUN_008ee400 */
        return MeshHashcode;
    }
};

struct AvatarStylePart : public AvatarDBEntry {
    uint32_t UnknownChild = 0;
    uint64_t NameHashcode = 0;
};

struct AvatarSkinPart : public AvatarDBEntry {
    uint32_t UnknownChild = 0;
    float Tint[4] = {};
};

struct AvatarEyePart : public AvatarDBEntry {
    uint32_t UnknownChild = 0;
    uint64_t NameHC = 0;
    uint64_t UnknownChild2 = 0;
    float UVOffset[4] = {};
};

struct AvatarNamedPartPart : public AvatarDBEntry {
    const char *pName = nullptr;

    const char *getMeshName() const override { /* FUN_008ee1d0 */
        return pName;
    }
};

class AvatarPartDB {
public:
    inline int32_t getCount() const { return NumElements; }
    inline int32_t getCapacity() const { return Capacity; }
    inline int32_t getElementSize() const { return ElementSize; }
    inline void *getElement( int32_t index ) const {
        return static_cast<uint8_t *>( pElements ) + index * ElementSize;
    }

    template <typename T> inline void reserveAs( int32_t capacity ) {
        reserve( capacity, sizeof( T ),
                 []( void *pSlot ) { new ( pSlot ) T(); } );
    }

public:
    AvatarPartDB();
    ~AvatarPartDB();

    void reserve( int32_t capacity,
                  int32_t elementSize,
                  void ( *pConstructor )( void * ) );
    void clear();
    void *addSlot();

private:
    void *pElements = nullptr;
    int32_t NumElements = 0;
    int32_t Capacity = 0;
    int32_t ElementSize = 0;
    void ( *pElementConstructor )( void * ) = nullptr;
};

class AvatarPartCursor {
public:
    virtual bool isAtEnd() { return CurIndex < 0 || CurIndex > LastMatchIndex; }
    virtual void advance() { ++CurIndex; }

    inline void rewind() { CurIndex = FirstMatchIndex; }
    inline bool hasRange() const {
        return FirstMatchIndex != -1 && LastMatchIndex != -1;
    }
    inline AvatarDBEntry *getResult() const {
        if ( CurIndex < 0 || CurIndex >= pDB->getCount() ) {
            return nullptr;
        }
        return static_cast<AvatarDBEntry *>( pDB->getElement( CurIndex ) );
    }

public:
    explicit AvatarPartCursor( AvatarPartDB *pTargetDB );
    virtual ~AvatarPartCursor() {}

    virtual bool matchesCategory( const AvatarDBEntry *pPart ) = 0;

    void findRange();
    bool findById( uint16_t id );

protected:
    void seekNextMatch();
    void seekPreviousMatch();

protected:
    int32_t CurIndex = 0;
    AvatarPartDB *pDB = nullptr;
    int32_t FirstMatchIndex = -1;
    int32_t LastMatchIndex = -1;
};

class AvatarGenericCursor : public AvatarPartCursor {
public:
    explicit AvatarGenericCursor( AvatarPartDB *pTargetDB )
        : AvatarPartCursor( pTargetDB ) {}

    bool matchesCategory( const AvatarDBEntry * ) override { return true; }
};

class AvatarClotheCursor : public AvatarPartCursor {
public:
    static constexpr int32_t kAnyClotheSet = 2;

public:
    AvatarClotheCursor( AvatarPartDB *pTargetDB,
                        int32_t gender,
                        eAvatarCustomizable category,
                        int32_t clotheSet );

    bool matchesCategory( const AvatarDBEntry *pPart ) override;
    bool isAtEnd() override;

    virtual void next();
    virtual void previous();

    bool seekTo( uint16_t id );

protected:
    int32_t Gender;
    eAvatarCustomizable Category;
    int32_t ClotheSet;
};

class AvatarWrappingClotheCursor : public AvatarClotheCursor {
public:
    using AvatarClotheCursor::AvatarClotheCursor;

    bool isAtEnd() override { /* FUN_008ed980 */ return false; }
    void next() override;
    void previous() override;
};

class AvatarGenderCursor : public AvatarPartCursor {
public:
    AvatarGenderCursor( AvatarPartDB *pTargetDB, int32_t gender );

    bool matchesCategory( const AvatarDBEntry *pPart ) override;
    bool isAtEnd() override;

    virtual void next();
    virtual void previous();

    bool seekTo( uint16_t id );

protected:
    int32_t Gender;
};

class AvatarWrappingGenderCursor : public AvatarGenderCursor {
public:
    using AvatarGenderCursor::AvatarGenderCursor;

    bool isAtEnd() override { /* FUN_00630320 */ return false; }
    void next() override;
    void previous() override;
};

class AvatarOwnedClotheCursor : public AvatarClotheCursor {
public:
    using AvatarClotheCursor::AvatarClotheCursor;

    bool matchesCategory( const AvatarDBEntry *pPart ) override;
};

class AvatarBrandClotheCursor : public AvatarClotheCursor {
public:
    AvatarBrandClotheCursor( AvatarPartDB *pTargetDB,
                             int32_t gender,
                             eAvatarCustomizable category,
                             int32_t clotheSet,
                             const char *pBrandName );

    bool matchesCategory( const AvatarDBEntry *pPart ) override;

private:
    const char *pBrand;
};
