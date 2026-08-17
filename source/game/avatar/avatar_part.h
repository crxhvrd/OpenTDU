#pragma once

#include <cstdint>

#include "avatar_configuration.h"

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

enum eAvatarDrivingType : int32_t {
    ADT_Car = 0,
    ADT_Bike = 1,
    ADT_Mixte = 2,   // valid for both sets
    ADT_Invalid = 3, // anything else
};

struct AvatarDBEntry {
    uint32_t Unknown;
    uint16_t IDHashcode;
    uint16_t Unknown2;
    int32_t Gender;
};

// A wearable part
struct AvatarClothePart : public AvatarDBEntry {
    eAvatarCustomizable Category;
    eAvatarClotheLength Length;
    eAvatarClotheSize Size;
    eAvatarDrivingType DrivingType;
    int32_t Price;
    uint8_t bIsBodyPart;
    uint8_t bIsPartBoth;
    int32_t Index; // index of the source row in TDU_Clothes; -1 when hardcoded
};

// The hardcoded default-parts entries carry their asset names inline
struct AvatarDefaultPart : public AvatarClothePart {
    const char *pMeshName;
    const char *pDisplayName;
    const char *pBrandName;
    int32_t BrandID;
};

struct AvatarStylePart : public AvatarDBEntry {
    uint32_t UnknownChild;
    uint64_t NameHashcode;
};

struct AvatarSkinPart : public AvatarDBEntry {
    uint32_t UnknownChild;
    float Tint[4];
};

struct AvatarEyePart : public AvatarDBEntry {
    uint32_t UnknownChild;
    uint64_t NameHC;
    uint64_t UnknownChild2;
    float UVOffset[4];
};

struct AvatarNamedPartPart : public AvatarDBEntry {
    const char *pName;
};

class AvatarPartDB {
public:
    inline int32_t getCount() const { return NumElements; }
    inline int32_t getCapacity() const { return Capacity; }
    inline int32_t getElementSize() const { return ElementSize; }
    inline void *getElement( int32_t index ) const {
        return static_cast<uint8_t *>( pElements ) + index * ElementSize;
    }

public:
    AvatarPartDB();
    ~AvatarPartDB();

    void reserve( int32_t capacity, int32_t elementSize );
    void clear();
    void *addSlot();

private:
    void *pElements = nullptr;
    int32_t NumElements = 0;
    int32_t Capacity = 0;
    int32_t ElementSize = 0;
};

class AvatarPartCursor {
public:
    virtual bool isAtEnd() { return CurIndex < 0 || CurIndex > LastMatchIndex; }
    virtual void advance() { ++CurIndex; }

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
