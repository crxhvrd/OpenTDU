#include "shared.h"
#include "entity_flag.h"

#include "filesystem/binary.h"
#include "game/game_mechanism.h"
#include "physics/gs_cloth_system.h"
#include "render/2dm.h"
#include "render/serialized_skinned_model.h"
#include "world/weather/gs_weather.h"

static constexpr float kFlagGridDensity = 8.0f; // DAT_00f41ac4
static constexpr int32_t kFlagMaxGridSize = 8;
static constexpr int32_t kFlagMinGridSize = 3;
static constexpr int32_t kFlagNumNeighbors = 8;
static constexpr float kFlagWindFactor = 0.05f; // 0x3d4ccccd
static constexpr uint32_t kClothParticlePinned = 1;

static inline float ReadStreamF32( BinaryReader *pReader ) {
    const float value = *reinterpret_cast<float *>( pReader->pCursor );
    pReader->pCursor += sizeof( float );
    return value;
}

static inline void PinParticle( ClothInstance *pCloth, int32_t index ) {
    pCloth->pParticles[index].Flags |= kClothParticlePinned;
}

EntityFlag::EntityFlag()
    : Entity(),
      size( Eigen::Vector4f::Zero() ),
      unknownVector( Eigen::Vector4f::Zero() ),
      pRef2DM( nullptr ),
      num2DBRefs( 0 ),
      pRef2DBs( nullptr ),
      pCloth( nullptr ),
      attachMode( AM_LeftEdge ),
      bPinWholeEdges( false ) {
    // FUN_00424280
}

EntityFlag::~EntityFlag() {
    // FUN_00424120
}

void EntityFlag::initialize() {
    // FUN_009c7620
    setTransformFromDesc();

    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }
}

void EntityFlag::loadResources( ScenaricBank *param_2 ) {
    // FUN_009c7450
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();

    pRef2DM =
        ResolveRender2DM( param_2, *reinterpret_cast<uint64_t *>( pRef2DM ) );

    uint64_t *pHashSlots = reinterpret_cast<uint64_t *>( pRef2DBs );
    Render2DB **pPtrSlots = reinterpret_cast<Render2DB **>( pRef2DBs );
    for ( int32_t i = 0; i < num2DBRefs; i++ ) {
        pPtrSlots[i] = ResolveRender2DB( param_2, pHashSlots[i] );
    }
}

void EntityFlag::initializeInstance( void *param_2, void *param_3 ) {
    // FUN_009c7160
    Entity::initializeInstance( param_2, param_3 );

    pCloth = ( gpClothSystem != nullptr ) ? gpClothSystem->allocateCloth()
                                          : nullptr;
    if ( pCloth == nullptr ) {
        return;
    }

    const float width = size.x();
    const float height = size.y();

    int32_t gridWidth = kFlagMaxGridSize;
    int32_t gridHeight = kFlagMaxGridSize;
    if ( width > height ) {
        gridHeight = static_cast<int32_t>( ( height / width ) *
                                           kFlagGridDensity );
        gridHeight = std::max( gridHeight, kFlagMinGridSize );
    } else {
        gridWidth = static_cast<int32_t>( ( width / height ) *
                                          kFlagGridDensity );
        gridWidth = std::max( gridWidth, kFlagMinGridSize );
    }

    Material *pMaterial =
        ( pRef2DM != nullptr ) ? pRef2DM->getMaterialAtIndex( 0 ) : nullptr;
    pCloth->setup( gridWidth, gridHeight, width, height, modelMatrix, pMaterial,
                   kFlagNumNeighbors );

    pinAttachedEdges();
}

void EntityFlag::pinAttachedEdges() {
    // FUN_009c7160 (inlined)
    const int32_t gridWidth = pCloth->GridWidth;
    const int32_t gridHeight = pCloth->GridHeight;
    const int32_t lastColumn = gridWidth - 1;
    const int32_t lastRowStart = ( gridHeight - 1 ) * gridWidth;

    switch ( attachMode ) {
    case AM_LeftEdge:
        if ( bPinWholeEdges ) {
            for ( int32_t row = 0; row < gridHeight; row++ ) {
                PinParticle( pCloth, row * gridWidth );
            }
        } else {
            PinParticle( pCloth, 0 );
            PinParticle( pCloth, lastRowStart );
        }
        break;
    case AM_LeftAndTopEdges:
        if ( bPinWholeEdges ) {
            for ( int32_t row = 0; row < gridHeight; row++ ) {
                PinParticle( pCloth, row * gridWidth );
            }
            for ( int32_t column = 0; column < gridWidth; column++ ) {
                PinParticle( pCloth, column );
            }
        } else {
            PinParticle( pCloth, 0 );
            PinParticle( pCloth, lastColumn );
            PinParticle( pCloth, lastRowStart );
        }
        break;
    case AM_LeftAndRightEdges:
        if ( bPinWholeEdges ) {
            for ( int32_t row = 0; row < gridHeight; row++ ) {
                PinParticle( pCloth, row * gridWidth );
                PinParticle( pCloth, row * gridWidth + lastColumn );
            }
        } else {
            PinParticle( pCloth, 0 );
            PinParticle( pCloth, lastColumn );
            PinParticle( pCloth, lastRowStart );
            PinParticle( pCloth, lastRowStart + lastColumn );
        }
        break;
    default:
        break;
    }
}

void EntityFlag::unloadResources() {
    // FUN_009c70b0
    Entity::unloadResources();

    if ( pCloth == nullptr ) {
        return;
    }

    pCloth->State = 0;
    pCloth->pMaterial = nullptr;
    pCloth->bVisible = 0;
    if ( pCloth->pBoundSector != nullptr ) {
        pCloth->unregister();
        pCloth->pBoundSector = nullptr;
    }
    pCloth = nullptr;
}

void EntityFlag::tick( PlayerGameMech *pGameMech,
                       float deltaTime,
                       float totalTime ) {
    // FUN_009c7580
    Entity::tick( pGameMech, deltaTime, totalTime );

    if ( pCloth == nullptr || gpWeather == nullptr ) {
        return;
    }

    if ( gpWeather->isConfigDirty() ) {
        gpWeather->updateActiveConfig();
    }

    const WeatherConfig &weather = gpWeather->getActiveRandomizedWeatherConfig();
    Eigen::Vector4f wind = weather.getWindDirection();
    wind.head<3>() *= weather.getWindSpeed() * kFlagWindFactor;
    wind.w() = 0.0f;
    pCloth->Acceleration = wind;
}

bool EntityFlag::draw( RenderScene * ) {
    // FUN_009c6ee0
    return true;
}

void EntityFlag::deserialize( BinaryReader *param_2 ) {
    // FUN_009c6fa0
    Entity::deserialize( param_2 );

    size.x() = ReadStreamF32( param_2 );
    size.y() = ReadStreamF32( param_2 );
    size.z() = ReadStreamF32( param_2 );
    size.w() = 0.0f;

    unknownVector.x() = ReadStreamF32( param_2 );
    unknownVector.y() = ReadStreamF32( param_2 );
    unknownVector.z() = ReadStreamF32( param_2 );
    unknownVector.w() = ReadStreamF32( param_2 );

    attachMode = static_cast<eAttachMode>( ReadStreamU32( param_2 ) );
    bPinWholeEdges = ReadStreamU32( param_2 ) != 0;

    num2DBRefs = static_cast<int32_t>( ReadStreamU32( param_2 ) ) - 1;
    pRef2DM = reinterpret_cast<Render2DM *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint64_t );
    pRef2DBs = reinterpret_cast<Render2DB *>( param_2->pCursor );
    param_2->pCursor += num2DBRefs * sizeof( uint64_t );
}

void EntityFlag::setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) {
    // FUN_009c70f0
    Entity::setTransform( transformMatrix, param_1 );

    if ( pCloth != nullptr ) {
        pCloth->Origin = transformMatrix.row( 3 ).transpose();
        pCloth->Origin.w() = 0.0f;
    }
}

void EntityFlag::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009c6ef0
    Entity::instanciate( pMngActor, param_1 );

    EntityFlag *pInstance = static_cast<EntityFlag *>( param_1 );
    pInstance->size = size;
    pInstance->unknownVector = unknownVector;
    pInstance->pRef2DM = pRef2DM;
    pInstance->num2DBRefs = num2DBRefs;
    pInstance->pRef2DBs = pRef2DBs;
    pInstance->pCloth = nullptr;
    pInstance->attachMode = attachMode;
    pInstance->bPinWholeEdges = bPinWholeEdges;
}
