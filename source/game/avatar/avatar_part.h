#pragma once

#include <cstdint>

struct AvatarDBEntry {
    uint32_t Unknown;
    uint16_t IDHashcode;
    uint16_t Unknown2;
    int32_t Gender;
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
