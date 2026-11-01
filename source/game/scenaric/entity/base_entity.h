#pragma once

#include "render/particles/particle_instance.h"
#include "flash/mng_flash.h"
#include "core/color.h"
#include "game/cinematic/cinematic_entity.h"
#include "entity_factory.h"

#include <cstdint>
#include <utility>
#include <vector>

class Spot;
class SoundBinary;
class EntityMechanism;
class ScenaricBank;
class SerializedEntity;
struct BinaryReader;
class RenderScene;
class MngActor;
class GameMechanism;
class SectorHeightmap;

struct SoundScene {
    union {
        uint64_t Hashcode;
        SoundBinary *pSound;
    };
};

struct SerializedTransform {
    float RotationX = 0.0f;
    float RotationY = 0.0f;
    float RotationZ = 0.0f;
    float RotationW = 1.0f;
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    float PositionZ = 0.0f;
    float ScaleX = 1.0f;
    float ScaleY = 1.0f;
    float ScaleZ = 1.0f;
};
OTDU_SIZE_MUST_MATCH( SerializedTransform, 40 );

struct SerializedAttribute {
public:
    SerializedAttribute( void *pPtr ) : pStreamPointer( pPtr ) {}

    virtual bool serialize( void *pPtr ) { return true; }
    virtual bool deserialize( void *pPtr ) { return true; }
    virtual uint32_t getSize() const { return sizeof( void * ); }

protected:
    void *pStreamPointer;
};

// Big endian matrix (vtable 0x00f2ac60).
struct SerializedMatrix : public SerializedAttribute {
    Matrix4x4f_t *pMatrixPointer = nullptr;

    SerializedMatrix( Matrix4x4f_t *pResource )
        : SerializedAttribute( pResource ), pMatrixPointer( pResource ) {}

    virtual bool serialize( void *pPtr ) override;
    virtual bool deserialize( void *pPtr ) override;
    virtual uint32_t getSize() const override { return sizeof( Matrix4x4f_t ); }
};

// Big endian 32 bit integer (vtable 0x00f2a7a0).
struct SerializedInt32 : public SerializedAttribute {
    int32_t *pValuePointer = nullptr;

    SerializedInt32( int32_t *pResource )
        : SerializedAttribute( pResource ), pValuePointer( pResource ) {}

    virtual bool serialize( void *pPtr ) override;
    virtual bool deserialize( void *pPtr ) override;
    virtual uint32_t getSize() const override { return sizeof( int32_t ); }
};

// Requests of the game editor control API (Entity::gmeEditionFactory).
enum eGMEControlRequest : int32_t {
    GCR_Deserialize = 0x0,
    GCR_Serialize = 0x1,
    GCR_GetSize = 0x2,
    GCR_NumControls = 0x9,
    GCR_GetID = 0xc,
    GCR_GetAttribute = 0xd,
    GCR_GetVersion = 0xe,
};

struct EntityDesc {
    eEntityType Type = eEntityType::ET__ENT;
    SerializedTransform Transform;
    float Priority = 0.0f;
    float RelevanceDistanceMax = 0.0f;
    uint16_t Flags = 0;
};

class Entity : public CinematicEntity {
    // The bank entity deserializer (FUN_009db370) binds the in-buffer name and
    // descriptor directly onto the entity it creates.
    friend class SerializedEntity;

public:
    virtual const char *GetName() { return "Entity"; /* FUN_00424140 */ }

public:
    inline const Matrix4x4f_t &getModelMatrix() const { return modelMatrix; }
    inline eEntityType getType() const {
        return pDesc != nullptr ? pDesc->Type : eEntityType::ET_None;
    }
    inline bool isShifted() const { return bShifted != 0; }
    inline void setPrimaryInstance( const bool b ) { bPrimaryInstance = b; }
    inline bool isPrimaryInstance() const { return bPrimaryInstance != 0; }
    inline uint32_t getFlags() const { return flags; }
    inline void setFlags( const uint32_t newFlags ) { flags = newFlags; }
    inline const std::string &getInstanceName() const { return name; }
    inline const EntityDesc *getDesc() const { return pDesc; }
    inline Entity *getParent() const { return pParent; }
    inline EntityMechanism *getMechanism() const { return pMechanism; }
    inline Spot *getSpot() const { return pSpot; }
    inline void setSpot( Spot *pNewSpot ) { pSpot = pNewSpot; }
    inline bool isStreamManaged() const { return bStreamManaged; }
    inline void setStreamManaged( bool b ) { bStreamManaged = b; }
    inline bool isStreamActive() const { return bStreamActive; }
    inline void setStreamActive( bool b ) { bStreamActive = b; }
    inline int32_t getTileKey() const { return defaultTileIndex; }
    inline void setTileKey( int32_t key ) { defaultTileIndex = key; }
    inline bool isVisible() const { return ( flags & 1u ) != 0; }
    inline void setVisible( const bool b ) {
        flags = b ? ( flags | 1u ) : ( flags & ~1u );
    }
    inline bool isTickEnabled() const { return ( flags & 2u ) != 0; }
    inline void setTickEnabled( const bool b ) {
        flags = b ? ( flags | 2u ) : ( flags & ~2u );
    }
    inline void setTileVisible( bool b ) {
        flags = b ? ( flags | 4u ) : ( flags & ~4u );
    }
    inline void releaseFromCinematic() {
        // FUN_007e5f70 (inlined)
        flags &= ~8u;
        flags = bVisibleBefore ? ( flags | 1u ) : ( flags & ~1u );
    }

    inline void bindDebugDescriptor( EntityDesc *pNewDesc,
                                     const char *pNewName ) {
        pDesc = pNewDesc;
        name = pNewName;
    }

public:
    Entity();
    virtual ~Entity();

    static bool IsAvatarEntityType( const Entity *pEntity );

    int32_t getEntityIndex() override { return 1; }

    bool onMessage( FlashMessage *pMessage ) override;
    void checkValidity() override;
    void initializeForCinematic( void *param_2, void *param_3 ) override;
    void initializeForCinematicKeyframed( void *param_2,
                                          void *param_3 ) override;

    Matrix4x4f_t getWorldMatrix() const;
    void setTransformFromDesc();

    // TODO: Some entries in the vtable are never implemented (children inherit
    // the parent no-op) and therefore remain unknown for now
    virtual void initialize();
    virtual void loadResources( ScenaricBank *param_2 );
    virtual void initializeInstance( void *param_2, void *param_3 );
    virtual void destroyInstance();
    virtual void unknownFunc() { /* NOP */ }
    virtual void unloadResources();
    virtual void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime );
    virtual bool draw( RenderScene *pScene );
    virtual void deserialize( BinaryReader *param_2 );
    virtual void unknownFunc2( void *param_2 ) { /* NOP */ }
    virtual void setTransform( Matrix4x4f_t &transformMatrix, bool param_1 );
    virtual void setPosition( Eigen::Vector4f &position, bool param_1 );
    virtual void propagateTransform( Matrix4x4f_t &transformMatrix,
                                     bool param_1 );
    virtual void propagatePosition( Eigen::Vector4f &position, bool param_1 );
    virtual void reset();
    virtual void instanciate( MngActor *pMngActor, Entity *param_1 );
    virtual void deinstanciate();
    // GME (GameModeEditor) vtable entries.
    virtual int32_t gmeEditionFactory( const int32_t controlIndex,
                                       const int32_t request,
                                       void *pData );
    virtual int32_t gmeGetControlIndex( const uint32_t controlID );
    virtual void onWorldStreamed() { /* NOP */ }
    virtual void *getGeometryData() { return nullptr; }
    virtual void setColor( ColorRGBA color ) { /* NOP */ }
    virtual void onGeometrySubmitted() { /* NOP */ }
    virtual bool isPMIInTile( const SectorHeightmap *pHeightmap,
                              int32_t tileKey );
    // TEMP: Ensure dynamic prop hiearchy transform is correctly calculated.
    virtual int32_t refreshMechanismRender(
        const std::vector<std::pair<uint64_t, void *>> &members ) {
        (void)members; /* NOP */
        return 0;
    }
    virtual bool refreshModelFromTemplate() { return false; }

protected:
    static constexpr const FlashMessage kEnableParticles =
        FlashMessage( 0x6e6f7470 );
    static constexpr const FlashMessage kDisableParticles =
        FlashMessage( 0x666f7470 );

protected:
    std::string name;
    uint32_t flags;

    Matrix4x4f_t modelMatrix;
    SerializedMatrix serializedModelMatrix;

    EntityDesc *pDesc;
    Entity *pParent;
    Spot *pSpot;
    SoundScene *pSound;
    EntityMechanism *pMechanism;

    uint8_t bPrimaryInstance : 1;
    uint8_t bShifted : 1;
    uint8_t bStreamManaged : 1;
    uint8_t bStreamActive : 1;

    std::vector<Entity *> children;
    int32_t defaultTileIndex;

protected:
    void resolveSoundReference();

    static int32_t GMEControlAttribute( SerializedAttribute *pAttribute,
                                        const uint32_t controlID,
                                        const int32_t request,
                                        void *pData );
};

void ResolvePathList( uint32_t *pList );
void ResolveEntityRefList( uint32_t *pList );
void ResolvePathBlock( uint8_t *pBlock );
void ResolveEntityRef( uint8_t *pBlock );
void ResolvePathBlock( uint8_t *pBlock, ScenaricBank *pBank );
void ResolveEntityRef( uint8_t *pBlock, ScenaricBank *pBank );
void ResolveVolumeBlock( uint8_t *pBlock, ScenaricBank *pBank );
uint32_t ReadStreamU32( BinaryReader *pReader );
uint8_t *ReadStreamRef( BinaryReader *pReader );
const char *ReadStreamString( BinaryReader *pReader );
