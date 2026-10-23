#include "shared.h"
#include "cin_res_collection.h"

#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "tdu_instance.h"

#include "game/cinematic/cinematic_music_emitter.h"

#include <algorithm>
#include <cstring>

#include "core/maths_helper.h"
#include "core/hash/crc64.h"
#include "core/random/bcpl.h"
#include "filesystem/bank.h"
#include "flash/mng_fade.h"
#include "game/scenaric/entity/entity_actor.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_pedestrian.h"
#include "world/gs_pedestrians.h"
#include "database/gs_database.h"
#include "database/db_aftermarket_pack.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "game/gm_scene.h"
#include "game/gm_world.h"
#include "game/game_mechanism.h"
#include "game/mng_hud.h"
#include "game/intro/gm_intro.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_traffic.h"
#include "game/vehicle/gs_car_display.h"
#include "game/vehicle/vehicle_controller.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/driver.h"
#include "player_data/saved_garage.h"
#include "render/mng_env_map.h"
#include "render/3dd.h"
#include "render/animation/clip.h"
#include "render/instance.h"
#include "render/hiearchy_node.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_cinematic.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "world/gs_world_streaming.h"
#include "world/mng_world.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "audio/gs_audio.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_radial_blur.h"
#include "render/gs_render.h"
#include "render/mng_shadow.h"
#include "game/scenaric/entity/entity_driver.h"
#include "flash/entities/flash_entity.h"
#include "game/scenaric/entity/entity_pnj.h"
#include "game/scenaric/mng_life_set.h"

// TDU_PNJ row index in the game database
static constexpr int32_t kPNJDatabaseIndex = 0x10;

static constexpr int32_t kCinematicCameraMode = 0xa; // DAT_00f143b4

static constexpr float kCinematicReferenceAspectRatio = 1.7777778f;
static constexpr float kSkinnedPropCullRadius = 100.0f; // DAT_00f41708

static float gCinematicShakePreviousTime = 0.0f; // DAT_016a6a14

static void StepCameraShakeAngle( float &angle,
                                  float &target,
                                  const float speed,
                                  const float amplitude,
                                  const float deltaTime ) {
    const float step = speed * deltaTime;

    float value = target;
    if ( target - angle > step ) {
        value = angle + step;
    }
    if ( -step > value - angle ) {
        value = angle - step;
    }
    angle = value;

    if ( value == target && gpScenaric != nullptr ) {
        target = RandomUnit( gpScenaric->getRandomGenerator() ) * amplitude;
    }
}

static constexpr float kDefaultRadialBlurIntensity = 20.0f; // DAT_00f03ee0
static constexpr float kEndTimeEpsilon = 1e-6f;             // FLOAT_00f41888
static constexpr int32_t kMaxCinematicAvatars = 8;

static void SetPlayerCarCinematicMode( bool bCinematic ) {
    if ( gpScenaric == nullptr ) {
        return;
    }
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( pCarState == nullptr ) {
        return;
    }
    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    if ( pVehicle == nullptr || !pVehicle->isActive() ) {
        return;
    }

    if ( pVehicle->isInCinematicMode() == bCinematic ) {
        return;
    }

    OTDU_LOG_DEBUG( "PLAYER_CAR: %s cinematic mode (driving input %s)\n",
                    bCinematic ? "entered" : "left",
                    bCinematic ? "detached" : "restored" );

    PhysicsLock lock;
    pVehicle->setCinematicMode( bCinematic );
    if ( bCinematic ) {
        pVehicle->setIgnition( false, false );
    } else {
        pVehicle->setKeyframed( false, false );
    }
}

static Matrix4x4f_t BuildScaledActorMatrix( const CinActorState &state,
                                            const Eigen::Vector3f &scale ) {
    Matrix4x4f_t transform = BuildRotationMatrix( state.Heading );
    transform.row( 0 ) *= scale.x();
    transform.row( 1 ) *= scale.y();
    transform.row( 2 ) *= scale.z();
    transform.row( 3 ) = Eigen::Vector4f(
        state.Position.x(), state.Position.y(), state.Position.z(), 1.0f );
    return transform;
}

static bool IsLocalPlayerCarInstalled() {
    return gpScenaric != nullptr &&
           gpScenaric->getLocalPlayer().getCarState() != nullptr;
}

static GarageSlot *GetCurrentGarageSlot() {
    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;
    if ( pSave == nullptr ) {
        return nullptr;
    }

    Driver *pDriver = pSave->getDriver();
    Garage *pGarage = pSave->getGarage();
    if ( pDriver == nullptr || pGarage == nullptr ) {
        return nullptr;
    }

    const int32_t index = static_cast<int32_t>( pDriver->CurrentCar );
    if ( index < 0 || index >= Garage::kNumCarSlots ) {
        return nullptr;
    }
    return &pGarage->pCars[index];
}

static float FindKeyframe( const void *pTrack,
                           float time,
                           int32_t *pLower,
                           int32_t *pUpper ) {
    // FUN_0067b8d0
    const uint16_t numKeys = *static_cast<const uint16_t *>( pTrack );
    const float *pKeyTimes = reinterpret_cast<const float *>(
        static_cast<const uint8_t *>( pTrack ) + 4 );

    // Degenerate / before-first / at-or-after-last: no interpolation.
    if ( numKeys < 2 || time <= pKeyTimes[0] ) {
        *pLower = 0;
        *pUpper = 0;
        return 0.0f;
    }
    if ( pKeyTimes[numKeys - 1] <= time ) {
        *pLower = numKeys - 1;
        *pUpper = numKeys - 1;
        return 0.0f;
    }

    int32_t i = numKeys >> 1;
    while ( time < pKeyTimes[i] && i > 1 ) {
        i >>= 1;
    }
    if ( i == 0 ) {
        i = 1;
    }
    const float *p = pKeyTimes + i;
    if ( *p <= time ) {
        do {
            ++p;
        } while ( *p <= time );
    }

    *pUpper = static_cast<int32_t>( p - pKeyTimes );
    *pLower = *pUpper - 1;

    const float span = p[0] - p[-1];
    if ( span <= 0.0f ) {
        return 0.0f;
    }
    const float frac = ( time - p[-1] ) / span;
    if ( frac < 0.0f ) {
        return 0.0f;
    }
    if ( frac > 1.0f ) {
        return 1.0f;
    }
    return frac;
}

const float *TrackKeyframeBase( const uint16_t *pTrack ) {
    // FUN_0067bd50
    const uint16_t numKeys = pTrack[0];
    const uint16_t flags = pTrack[1];
    int32_t u16Offset;
    if ( ( flags & 1 ) == 0 ) {
        u16Offset = numKeys * 2 + 2; // (NumKeys+1) floats
    } else {
        uint32_t n = numKeys;
        if ( ( numKeys & 1 ) != 0 ) {
            n -= 1;
        }
        u16Offset = static_cast<int32_t>( n ) + 4;
    }
    return reinterpret_cast<const float *>( pTrack + u16Offset );
}

struct CinAnimClipKey {
    int32_t Ref;
    float Start[3];
    float End[3];
    float Length[3];
};
OTDU_SIZE_MUST_MATCH( CinAnimClipKey, 0x28 );

static bool DecodeAnimClipKey( const uint16_t *pTrack,
                               float time,
                               int32_t &outRefA,
                               float &outTimeA,
                               bool &bOutBlending,
                               int32_t &outRefB,
                               float &outTimeB,
                               float &outRatio ) {
    // FUN_0067baf0
    int32_t lower = 0;
    int32_t upper = 0;
    FindKeyframe( pTrack, time, &lower, &upper );

    const float *pKeyTimes = reinterpret_cast<const float *>( pTrack + 2 );
    const float elapsed = time - pKeyTimes[lower];
    if ( elapsed < 0.0f ) {
        return false;
    }

    const CinAnimClipKey *pKeys =
        reinterpret_cast<const CinAnimClipKey *>( TrackKeyframeBase( pTrack ) );
    const CinAnimClipKey &key = pKeys[lower];

    if ( elapsed < key.Length[0] && lower != 0 ) {
        const CinAnimClipKey &previous = pKeys[lower - 1];
        const float ratio =
            ( key.Length[0] > 0.0f ) ? elapsed / key.Length[0] : 0.0f;
        outTimeA =
            ratio * previous.End[2] + ( 1.0f - ratio ) * previous.Start[2];
        outRefA = previous.Ref;
        outTimeB = ratio * key.End[0] + ( 1.0f - ratio ) * key.Start[0];
        outRefB = key.Ref;
        bOutBlending = true;
        outRatio = ratio;
        return true;
    }

    float ratio = 0.0f;
    if ( key.Length[1] > 0.0f ) {
        ratio = ( elapsed - key.Length[0] ) / key.Length[1];
        if ( ratio >= 1.0f ) {
            return false;
        }
    }

    outTimeA = ratio * key.End[1] + ( 1.0f - ratio ) * key.Start[1];
    bOutBlending = false;
    outRatio = 0.0f;
    outRefA = key.Ref;
    return true;
}

Eigen::Vector3f InterpolateVec3Track( const uint16_t *pTrack, float time ) {
    // FUN_0067bd50
    int32_t lower = 0, upper = 0;
    const float frac = FindKeyframe( pTrack, time, &lower, &upper );
    const float *kf = TrackKeyframeBase( pTrack );
    const Eigen::Vector3f vLower( kf[lower * 3], kf[lower * 3 + 1],
                                  kf[lower * 3 + 2] );
    const Eigen::Vector3f vUpper( kf[upper * 3], kf[upper * 3 + 1],
                                  kf[upper * 3 + 2] );
    return Lerp( vLower, vUpper, frac );
}

Eigen::Quaternionf InterpolateQuatTrack( const uint16_t *pTrack, float time ) {
    // FUN_0067be60
    int32_t lower = 0, upper = 0;
    const float frac = FindKeyframe( pTrack, time, &lower, &upper );
    const float *kf = TrackKeyframeBase( pTrack );
    const Eigen::Quaternionf qLower( kf[lower * 4 + 3], kf[lower * 4],
                                     kf[lower * 4 + 1], kf[lower * 4 + 2] );
    const Eigen::Quaternionf qUpper( kf[upper * 4 + 3], kf[upper * 4],
                                     kf[upper * 4 + 1], kf[upper * 4 + 2] );
    return qLower.slerp( frac, qUpper );
}

float InterpolateScalarTrack( const uint16_t *pTrack, float time ) {
    // FUN_0067ba50
    int32_t lower = 0, upper = 0;
    const float frac = FindKeyframe( pTrack, time, &lower, &upper );
    const float *kf = TrackKeyframeBase( pTrack );
    return frac * kf[upper] + ( 1.0f - frac ) * kf[lower];
}

Eigen::Vector3f
InterpolateVec3RefTrack( const uint16_t *pTrack, float time, int32_t *pRefId ) {
    // FUN_0067bf40
    int32_t lower = 0, upper = 0;
    const float frac = FindKeyframe( pTrack, time, &lower, &upper );
    const float *kf = TrackKeyframeBase( pTrack );
    const Eigen::Vector3f vLower( kf[lower * 4 + 1], kf[lower * 4 + 2],
                                  kf[lower * 4 + 3] );
    const Eigen::Vector3f vUpper( kf[upper * 4 + 1], kf[upper * 4 + 2],
                                  kf[upper * 4 + 3] );
    if ( pRefId != nullptr ) {
        *pRefId = *reinterpret_cast<const int32_t *>( &kf[lower * 4] );
    }
    return Lerp( vLower, vUpper, frac );
}

void CinResCollection::bindPlayerCar( CarState *pCarState ) {
    // FUN_007e1bb0
    if ( pCarState == nullptr ) {
        return;
    }

    if ( !bActorPoolActive || pPlayerCarActor != nullptr ) {
        pPendingPlayerCar = pCarState;
        return;
    }

    CinematicActor *pActor = registerActor( "PLAYER_CAR", nullptr, nullptr,
                                            CinematicActor::CAK_PlayerCar );
    if ( pActor != nullptr ) {
        pActor->pCarState = pCarState;
        PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
        if ( pVehicle != nullptr ) {
            PhysicsLock lock;
            pActor->DisplayMatrix =
                pVehicle->computeDisplayMatrix( pVehicle->getPhysicsMatrix() );
            pActor->TrackMatrix = Matrix4x4f_t::Identity();
        }
    }
    pPlayerCarActor = pActor;
    pPendingPlayerCar = nullptr;
}

void CinResCollection::releasePlayerCarActors() {
    for ( int32_t i = 0; i < numActors; i++ ) {
        if ( actors[i].Kind == CinematicActor::CAK_PlayerCar ) {
            SetPlayerCarCinematicMode( false );
            return;
        }
    }
}

void CinResCollection::releaseTrafficCarActors() {
    // FUN_007e5f70
    for ( int32_t i = 0; i < numActors; i++ ) {
        CinematicActor &actor = actors[i];
        if ( actor.Kind == CinematicActor::CAK_IntroCarPart ) {
            releaseIntroCarPart( i );
            continue;
        }
        if ( actor.Kind == CinematicActor::CAK_IntroCar ) {
            // FUN_007e4f00
            GMIntro::IntroCarSlot *pSlot =
                GMIntro::FindIntroCarSlot( actor.Name );
            if ( pSlot != nullptr ) {
                gpGMIntro->releaseIntroCar( pSlot );
            }
            actor.pCarState = nullptr;
            actor.pIntroCarSlot = nullptr;
            continue;
        }
        if ( actor.Kind != CinematicActor::CAK_TrafficCar ||
             actor.pCarState == nullptr ) {
            continue;
        }

        PhysicsVehicle *pVehicle = actor.pCarState->getPhysicsVehicle();
        if ( pVehicle != nullptr ) {
            PhysicsLock lock;
            pVehicle->setCinematicMode( false );
        }
        if ( gpCarState != nullptr ) {
            gpCarState->releaseCarState( actor.pCarState, false );
        }
        actor.pCarState = nullptr;
    }
}

void CinResCollection::releaseIntroCarPart( const int32_t index ) {
    CinematicActor &actor = actors[index];
    if ( actor.pEntity != nullptr ) {
        static_cast<GMIntro::IntroCarPart *>( actor.pEntity )->bUsed = false;
        actor.pEntity = nullptr;
    }
}

void CinResCollection::reset() {
    // FUN_007e23c0
    numAvatars = 0;
    numHostesses = 0;

    releasePlayerCarActors();
    releaseTrafficCarActors();
    releaseCinematicCamera();
    pCinematicData = nullptr;
    pRefData = nullptr;
    pBuffer = nullptr;
    pSourceTable = nullptr;
    pRefTable = nullptr;
    pBank = nullptr;
    numActors = 0;
    pPlayerCarActor = nullptr;
    bActorPoolActive = false;
    bCreated = false;
    firedMessages.clear();
}

bool CinResCollection::parseCinematic( const void *pData, uint32_t size ) {
    // FUN_0067b2a0
    pCinematicData = nullptr;
    pRefData = nullptr;

    const uint8_t *pCursor = static_cast<const uint8_t *>( pData );
    const uint8_t *pEnd = pCursor + size;
    while ( pCursor < pEnd ) {
        const CinematicChunk *pChunk =
            reinterpret_cast<const CinematicChunk *>( pCursor );
        const uint32_t *pPayload = reinterpret_cast<const uint32_t *>(
            pCursor + sizeof( CinematicChunk ) );

        switch ( pChunk->Magic ) {
        case kCinematicMagic:
            if ( pPayload[0] != kCinematicVersion ) {
                pCinematicData = nullptr;
                return false;
            }
            break;
        case kCinematicDataMagic:
            pCinematicData =
                reinterpret_cast<const CinematicHeader *>( pPayload );
            break;
        case kCinematicRefsMagic:
            pRefData = pPayload;
            break;
        default:
            break;
        }

        pCursor += sizeof( CinematicChunk ) + pChunk->Size;
    }

    return pCinematicData != nullptr;
}

static const void *
ParseScene( const void *pBlob, uint32_t size, const void **ppRefs ) {
    // FUN_0067cdd0
    const void *pSceneData = nullptr;
    if ( ppRefs != nullptr ) {
        *ppRefs = nullptr;
    }

    const uint8_t *pCursor = static_cast<const uint8_t *>( pBlob );
    const uint8_t *pEnd = pCursor + size;
    while ( pCursor < pEnd ) {
        const CinematicChunk *pChunk =
            reinterpret_cast<const CinematicChunk *>( pCursor );
        const uint32_t *pPayload = reinterpret_cast<const uint32_t *>(
            pCursor + sizeof( CinematicChunk ) );

        switch ( pChunk->Magic ) {
        case kCinematicSceneMagic:
            if ( pPayload[0] != kCinematicSceneVersion ) {
                return nullptr;
            }
            break;
        case kCinematicDataMagic:
            pSceneData = pPayload;
            break;
        case kCinematicRefsMagic:
            if ( ppRefs != nullptr ) {
                *ppRefs = pPayload;
            }
            break;
        default:
            break;
        }

        pCursor += sizeof( CinematicChunk ) + pChunk->Size;
    }

    return pSceneData;
}

bool CinResCollection::load( const void *pData,
                             uint32_t size,
                             void *pBufferIn ) {
    // FUN_0067b2a0
    pBuffer = pBufferIn;
    pSourceTable = nullptr;
    pRefTable = nullptr;

    // Split the .cin into CIN! (version) / DATA / REFS payloads.
    if ( !parseCinematic( pData, size ) ) {
        return false;
    }

    const int32_t numSources = pCinematicData->NumSources;
    const int32_t numRefs =
        pRefData != nullptr ? *static_cast<const int32_t *>( pRefData ) : 0;

    pRefTable = reinterpret_cast<CinRefSlot *>( pBuffer );
    pSourceTable = reinterpret_cast<void **>( pRefTable + numRefs );
    void **pSourceData = pSourceTable + numSources;

    // The source array immediately follows the 60-byte header.
    const CinSourceEntry *pSources =
        reinterpret_cast<const CinSourceEntry *>( pCinematicData + 1 );

    for ( int32_t i = 0; i < numSources; i++ ) {
        pSourceTable[i] = pSourceData;
        if ( pSources[i].Type == 1 ) {
            OTDU_ASSERT( pRefData != nullptr );
            if ( !buildSourceMedia( pSources[i], pSourceData ) ) {
                return false;
            }
        }
    }

    if ( pRefData != nullptr ) {
        const uint8_t *pEntry = static_cast<const uint8_t *>( pRefData ) + 4;
        for ( int32_t i = 0; i < numRefs; i++ ) {
            pRefTable[i].pDescriptor = pEntry;
            pRefTable[i].pHandle = nullptr;
            // Low byte of the first dword is the ref type; type 3 is data-only.
            if ( ( pEntry[0] & 0xff ) != 3 ) {
                pRefTable[i].pHandle = buildRefMedia( pEntry );
            }
            pEntry += *reinterpret_cast<const uint16_t *>( pEntry + 2 );
        }
    }

    return true;
}

bool CinResCollection::initialize() {
    // FUN_0067b500
    OTDU_UNIMPLEMENTED;
    return true;
}

void CinResCollection::debugDumpControllers() const {
    // TODO: DEBUG: REMOVEME
    if ( pCinematicData == nullptr ) {
        OTDU_LOG_ALWAYS( "[CINCTRL] no DATA\n" );
        return;
    }
    const uint8_t *pCin = reinterpret_cast<const uint8_t *>( pCinematicData );
    const int32_t numSources = pCinematicData->NumSources;
    const int32_t numCtrl = pCinematicData->NumMediaControllers;

    const uint8_t *pCtrl = pCin + static_cast<size_t>( numSources ) * 8 + 0x3c;
    OTDU_LOG_ALWAYS( "[CINCTRL] numSources=%d numCtrl=%d ctrlBase=+0x%x "
                     "(dataPtr=%p)\n",
                     numSources, numCtrl, static_cast<uint32_t>( pCtrl - pCin ),
                     (const void *)pCin );

    for ( int32_t i = 0; i < numCtrl && i < 4; i++ ) {
        const uint16_t type = *reinterpret_cast<const uint16_t *>( pCtrl );
        const uint32_t advance =
            *reinterpret_cast<const uint32_t *>( pCtrl + 4 );
        OTDU_LOG_ALWAYS( "[CINCTRL] ctrl[%d] off=+0x%x type=%u advance=%u\n", i,
                         static_cast<uint32_t>( pCtrl - pCin ), type, advance );
        for ( int32_t row = 0; row < 3; row++ ) {
            const uint32_t *p =
                reinterpret_cast<const uint32_t *>( pCtrl + row * 16 );
            OTDU_LOG_ALWAYS( "[CINCTRL]   +0x%02x: %08x %08x %08x %08x\n",
                             row * 16, p[0], p[1], p[2], p[3] );
        }
        if ( advance == 0 ) {
            break;
        }
        pCtrl += advance;
    }
}

void CinResCollection::processCinematicMessages( const uint16_t *pTrack,
                                                 float time ) {
    if ( pRefTable == nullptr || pRefData == nullptr ) {
        return;
    }

    int32_t lower = 0;
    int32_t upper = 0;
    FindKeyframe( pTrack, time, &lower, &upper );

    const float *pKeyTimes = reinterpret_cast<const float *>( pTrack + 2 );
    const float elapsed = time - pKeyTimes[lower];
    if ( elapsed < 0.0f ) {
        return;
    }

    const float *pValues = TrackKeyframeBase( pTrack );
    const int32_t refIdx =
        reinterpret_cast<const int32_t *>( pValues )[lower * 2];
    const float duration = pValues[lower * 2 + 1];

    if ( elapsed >= duration ) {
        return;
    }

    const int32_t numRefs = *static_cast<const int32_t *>( pRefData );
    if ( refIdx < 0 || refIdx >= numRefs ) {
        return;
    }

    const uint8_t *pDesc =
        static_cast<const uint8_t *>( pRefTable[refIdx].pDescriptor );
    if ( pDesc == nullptr || pDesc[0] != 0x0b ) {
        return;
    }

    const char *pMsg = reinterpret_cast<const char *>(
        pDesc + ( ( pDesc[1] & 1 ) ? 0xc : 4 ) );
    if ( pMsg[0] == '\0' ) {
        return;
    }

    onCinematicMessage( pValues + lower * 2, pMsg );
}

uint32_t ExtractBufferSize( const void *pData, uint32_t size ) {
    // FUN_0067b1e0
    const uint8_t *pCursor = static_cast<const uint8_t *>( pData );
    const uint8_t *pEnd = pCursor + size;
    while ( pCursor < pEnd ) {
        const CinematicChunk *pChunk =
            reinterpret_cast<const CinematicChunk *>( pCursor );
        if ( pChunk->Magic == kCinematicDataMagic ) {
            const CinematicHeader *pHeader =
                reinterpret_cast<const CinematicHeader *>(
                    pCursor + sizeof( CinematicChunk ) );
            const uint32_t slots =
                static_cast<uint32_t>( pHeader->NumEntities ) +
                static_cast<uint32_t>( pHeader->NumRefs +
                                       pHeader->NumSources ) *
                    2u;
            // FIXME: The original's 32-bit slot count under-reserves the
            // per-source area. Calc proper size for x64.
            return slots * sizeof( void * ) * 4u + 256u;
        }
        pCursor += sizeof( CinematicChunk ) + pChunk->Size;
    }
    return 0;
}

void CinResCollection::tick( float time, float deltaTime ) {
    // FUN_00425d80
    lastDeltaTime = deltaTime;

    // FUN_0067b560
    if ( pCinematicData == nullptr ) {
        return;
    }

    // The upper bound stays inside the last shot so a seek to the end still
    // fires that shot's open messages.
    const float lastTime = pCinematicData->TotalLength[0] - kEndTimeEpsilon;
    if ( time > lastTime ) {
        time = lastTime;
    }
    if ( time < 0.0f ) {
        time = 0.0f;
    }

    const int32_t numSources = pCinematicData->NumSources;
    const int32_t numControllers = pCinematicData->NumMediaControllers;

    const uint8_t *pCtrl = reinterpret_cast<const uint8_t *>( pCinematicData ) +
                           sizeof( CinematicHeader ) +
                           static_cast<size_t>( numSources ) * 8;
    for ( int32_t i = 0; i < numControllers; i++ ) {
        const uint16_t type = *reinterpret_cast<const uint16_t *>( pCtrl );
        const uint32_t advance =
            *reinterpret_cast<const uint32_t *>( pCtrl + 4 );
        if ( type == 1 ) {
            tickController( pCtrl, time );
        }
        if ( advance == 0 ) {
            break;
        }
        pCtrl += advance;
    }

    const int32_t numCineTracks = pCinematicData->NumSubAnimatedProps;
    for ( int32_t i = 0; i < numCineTracks; i++ ) {
        const uint32_t typeHash =
            *reinterpret_cast<const uint32_t *>( pCtrl + 8 );
        if ( typeHash == 0x73d8ccaeu ) {
            processCinematicMessages(
                reinterpret_cast<const uint16_t *>( pCtrl + 0xc ), time );
        }

        const uint32_t advance =
            *reinterpret_cast<const uint32_t *>( pCtrl + 4 );
        if ( advance == 0 ) {
            break;
        }
        pCtrl += advance;
    }
}

void CinResCollection::tickController( const uint8_t *pController,
                                       float time ) {
    // FUN_0067b560
    const uint8_t *pTrack = pController + 0xc;
    const uint16_t numKeys = *reinterpret_cast<const uint16_t *>( pTrack );
    const uint16_t flags = *reinterpret_cast<const uint16_t *>( pTrack + 2 );

    if ( ( flags & 1 ) != 0 ) {
        OTDU_UNIMPLEMENTED; // relocated keytime-pointer layout
        return;
    }

    int32_t lower = 0;
    int32_t upper = 0;
    FindKeyframe( pTrack, time, &lower, &upper );

    const float *pKeyTimes =
        reinterpret_cast<const float *>( pController + 0x10 );
    const float frac = time - pKeyTimes[lower];

    const float *pKeyframes =
        reinterpret_cast<const float *>( pTrack ) + ( numKeys + 1 );
    const float *kf = pKeyframes + static_cast<size_t>( lower ) * 0xf;

    const float duration = kf[0];
    const float startTime = kf[3];
    const float endTime = kf[4];
    // remapped = (frac / duration) * (end - start) + start  (= time for 1 key).
    const float remapped =
        duration != 0.0f
            ? ( frac / duration ) * ( endTime - startTime ) + startTime
            : startTime;

    const int32_t sourceIdx = *reinterpret_cast<const int32_t *>( &kf[1] );
    const uint32_t mediaId = *reinterpret_cast<const uint32_t *>( &kf[2] );

    if ( pSourceTable == nullptr || pCinematicData == nullptr ) {
        return;
    }
    if ( sourceIdx < 0 || sourceIdx >= pCinematicData->NumSources ) {
        return;
    }
    void **pSource = static_cast<void **>( pSourceTable[sourceIdx] );
    if ( pSource == nullptr || pSource[0] == nullptr ) {
        return;
    }

    const uint8_t *pSceneData = static_cast<const uint8_t *>( pSource[0] );
    const int32_t numMedia = *reinterpret_cast<const int32_t *>(
        pSceneData + kCinSceneMediaCountOffset );

    onShotEvaluated( lower, kf, frac );

    CinSourceContext ctx;
    ctx.pPlayer = this;
    ctx.pSceneData = pSceneData;
    ctx.pHandles = pSource + 1;
    ctx.pSceneRefs = reinterpret_cast<CinRefSlot *>( pSource + 1 + numMedia );
    tickSource( &ctx, remapped, mediaId );
}

void CinResCollection::tickSource( const CinSourceContext *pCtx,
                                   float time,
                                   uint32_t mediaId ) {
    // FUN_0067cc10
    const uint8_t *pSceneData =
        static_cast<const uint8_t *>( pCtx->pSceneData );
    const int32_t numMedia = *reinterpret_cast<const int32_t *>(
        pSceneData + kCinSceneMediaCountOffset );

    const uint8_t *pEntry = pSceneData + kCinSceneMediaArrayOffset;
    for ( int32_t i = 0; i < numMedia; i++ ) {
        const CinSceneMediaHeader *pMedia =
            reinterpret_cast<const CinSceneMediaHeader *>( pEntry );
        CinMediaContext mediaCtx{ pMedia, pCtx, pCtx->pHandles + i };

        if ( pMedia->TypeHash == kCinMediaCamera ) {
            // The camera track is keyed by the controller's media id: only the
            // camera entry whose Id matches is tickd this frame.
            const bool bMatch = pMedia->Id == mediaId;
            if ( !bQuiet )
                OTDU_LOG_ALWAYS(
                    "[CINEVAL]   media[%d] CAMERA id=0x%x match=%d\n", i,
                    pMedia->Id, bMatch ? 1 : 0 );
            if ( bMatch ) {
                tickCameraMedia( &mediaCtx, time );
            }
        } else if ( pMedia->TypeHash == kCinMediaActor ) {
            if ( !bQuiet )
                OTDU_LOG_ALWAYS(
                    "[CINEVAL]   media[%d] ACTOR  id=0x%x dispatch\n", i,
                    pMedia->Id );
            tickActorMedia( &mediaCtx, time );
        }

        pEntry += pMedia->Size;
    }
}

void CinResCollection::tickActorMedia( const CinMediaContext *pCtx,
                                       float time ) {
    // FUN_0067c4a0
    const uint8_t *pEntry = static_cast<const uint8_t *>(
        static_cast<const void *>( pCtx->pEntry ) );
    const int32_t propOffset = *reinterpret_cast<const int32_t *>( pEntry + 4 );
    const int32_t numProps =
        *reinterpret_cast<const int32_t *>( pEntry + 0x10 );

    Eigen::Vector3f position = Eigen::Vector3f::Zero();
    Eigen::Quaternionf heading = Eigen::Quaternionf::Identity();
    Eigen::Vector3f scale( 1.0f, 1.0f, 1.0f );
    bool bVisible = true;
    bool bHasTransformTrack = false;
    CinActorState state;

    const uint8_t *pProp = pEntry + propOffset;
    for ( int32_t i = 0; i < numProps; i++ ) {
        const int16_t subtype = *reinterpret_cast<const int16_t *>( pProp );
        const int32_t advance = *reinterpret_cast<const int32_t *>( pProp + 4 );
        const uint32_t typeHash =
            *reinterpret_cast<const uint32_t *>( pProp + 8 );
        const uint16_t *pTrack =
            reinterpret_cast<const uint16_t *>( pProp + 0xc );

        switch ( typeHash ) {
        case 0x64c8d3b1u: // heading
            if ( subtype == 0 ) {
                heading = InterpolateQuatTrack( pTrack, time );
                bHasTransformTrack = true;
            }
            break;
        case 0x6c6163b8u: // scale
            if ( subtype == 1 ) {
                scale = InterpolateVec3Track( pTrack, time );
            }
            break;
        case 0xd7e2d8c4u: // position
            if ( subtype == 1 ) {
                position = InterpolateVec3Track( pTrack, time );
                bHasTransformTrack = true;
            }
            break;
        case 0xd2df4b2cu: { // visibility flag (byte keyframes after the
                            // keytimes)
            int32_t lower = 0, upper = 0;
            FindKeyframe( pTrack, time, &lower, &upper );
            const uint8_t *pBytes = reinterpret_cast<const uint8_t *>(
                TrackKeyframeBase( pTrack ) );
            bVisible = pBytes[lower] != 0;
            break;
        }
        case 0x73d8ccaeu: // message keys
            collectActorMessages( pCtx, pTrack, time, state );
            break;
        case 0xdcd2e210u: // anim clip keys
            collectActorClips( pCtx, pTrack, time, state );
            break;
        default:
            OTDU_UNIMPLEMENTED; // TODO: FX keys
            break;
        }

        pProp += advance;
    }

    CinematicActor *pActor =
        static_cast<CinematicActor *>( *pCtx->pHandleSlot );
    if ( !bQuiet )
        OTDU_LOG_ALWAYS(
            "[CINACT] t=%.3f '%s' entity=%p vis=%d pos=(%.2f,%.2f,%.2f) "
            "head=(%.3f,%.3f,%.3f,%.3f) scale=(%.2f,%.2f,%.2f)\n",
            time, pActor != nullptr ? pActor->Name : "?",
            pActor != nullptr ? pActor->pEntity : nullptr, bVisible ? 1 : 0,
            position.x(), position.y(), position.z(), heading.x(), heading.y(),
            heading.z(), heading.w(), scale.x(), scale.y(), scale.z() );

    state.Position = position;
    state.Heading = heading;
    state.bVisible = bVisible;
    dispatchActor( pActor, state, scale, bHasTransformTrack );
}

void CinResCollection::dispatchActor( CinematicActor *pActor,
                                      CinActorState &state,
                                      const Eigen::Vector3f &scale,
                                      bool bHasTransformTrack ) {
    // FUN_007e3e30
    if ( pActor == nullptr ) {
        return;
    }
    if ( pActor->Kind != CinematicActor::CAK_PlayerCar &&
         pActor->Kind != CinematicActor::CAK_TrafficCar &&
         pActor->Kind != CinematicActor::CAK_IntroCar &&
         pActor->pEntity == nullptr ) {
        return;
    }

    if ( pActor->Kind == CinematicActor::CAK_IntroCarPart ) {
        dispatchIntroCarPart( pActor, state, scale );
        return;
    }
    if ( pActor->Kind == CinematicActor::CAK_IntroCar ) {
        applyIntroCarPose( pActor, state, scale, bHasTransformTrack );
        return;
    }

    if ( pActor->Kind == CinematicActor::CAK_Entity ) {
        Entity *pEntity = static_cast<Entity *>( pActor->pEntity );

        if ( bFirstShotPending ) {
            pEntity->initializeForCinematic( &state, this );
        }

        for ( int32_t i = 0; i < state.NumMessages; i++ ) {
            const CinActorMessage &message = state.Messages[i];
            if ( isMessageFired( message.pKey ) ) {
                continue;
            }

            FlashMessage *pMessage = reinterpret_cast<FlashMessage *>(
                const_cast<char *>( message.pString ) );
            if ( pEntity->onMessage( pMessage ) ) {
                recordFiredMessage( message.pString, message.pKey );
                OTDU_LOG_DEBUG(
                    "Cinematic '%s' actor '%s' consumed message '%s'\n",
                    debugName, pActor->Name, message.pString );
            }
        }

        pEntity->initializeForCinematicKeyframed( &state, this );

        const Eigen::Vector4f worldPos = pEntity->getModelMatrix().row( 3 );
        return;
    }

    if ( pActor->Kind == CinematicActor::CAK_PlayerCar ) {
        if ( bFirstShotPending ) {
            initializePlayerCarForCinematic();
        }

        pActor->TrackMatrix = BuildRotationMatrix( state.Heading );
        pActor->TrackMatrix.row( 3 ) = Eigen::Vector4f(
            state.Position.x(), state.Position.y(), state.Position.z(), 1.0f );

        for ( int32_t i = 0; i < state.NumMessages; i++ ) {
            const CinActorMessage &message = state.Messages[i];
            if ( !isMessageFired( message.pKey ) &&
                 onPlayerCarMessage( pActor->pCarState, message.pString ) ) {
                recordFiredMessage( message.pString, message.pKey );
            }
        }
    }

    Matrix4x4f_t transform = BuildScaledActorMatrix( state, scale );
    if ( bHasBaseMatrix ) {
        transform = transform * baseMatrix;
    }

    if ( pActor->Kind == CinematicActor::CAK_TrafficCar ) {
        if ( bHasTransformTrack ) {
            applyCarTransform( pActor->pCarState, transform,
                               pActor->bCarTeleport );
        }
        PhysicsVehicle *pVehicle = ( pActor->pCarState != nullptr )
                                       ? pActor->pCarState->getPhysicsVehicle()
                                       : nullptr;
        if ( pVehicle != nullptr ) {
            PhysicsLock lock;
            pActor->DisplayMatrix =
                pVehicle->computeDisplayMatrix( pVehicle->getPhysicsMatrix() );
        }
        return;
    }

    if ( pActor->Kind == CinematicActor::CAK_PlayerCar ) {
        if ( bHasTransformTrack ) {
            applyCarTransform( pActor->pCarState, transform,
                               bPlayerCarTeleport );
        }

        CarState *pPlayerCar = pActor->pCarState;
        PhysicsVehicle *pPlayerVehicle = ( pPlayerCar != nullptr )
                                             ? pPlayerCar->getPhysicsVehicle()
                                             : nullptr;
        if ( pPlayerVehicle != nullptr ) {
            PhysicsLock lock;
            pActor->DisplayMatrix = pPlayerVehicle->computeDisplayMatrix(
                pPlayerVehicle->getPhysicsMatrix() );
        }

        // FUN_007e5610
        const CarState *pLocalCar =
            ( gpScenaric != nullptr )
                ? gpScenaric->getLocalPlayer().getCarState()
                : nullptr;
        const PhysicsVehicle *pLocalVehicle =
            ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
        if ( pLocalVehicle != nullptr && pLocalVehicle->isInCinematicMode() &&
             gpMngShadow != nullptr && !bAvatarShadowTarget &&
             pOwner != nullptr && pOwner->hasFocus() ) {
            Eigen::Vector4f shadowTarget = transform.row( 3 ).transpose();
            if ( gpWorldStreaming != nullptr &&
                 gpWorldStreaming->isEnabled() ) {
                shadowTarget.head<3>() +=
                    gpWorldStreaming->getPosition().head<3>();
            }
            gpMngShadow->setTarget( &shadowTarget );
        }

        if ( gpMngWorld != nullptr && gpMngWorld->isStreamEnabled() &&
             !bStreamTarget && pOwner != nullptr && pOwner->hasFocus() &&
             bActorsDriveStreaming ) {
            gpMngWorld->setDisableStreamingOnCar( false );
        }
        return;
    }

    Entity *pEntity = static_cast<Entity *>( pActor->pEntity );
    pEntity->setTransform( transform, true );

    const Eigen::Vector4f worldPos = pEntity->getModelMatrix().row( 3 );
    if ( !bQuiet )
        OTDU_LOG_ALWAYS(
            "[CINACT]   applied -> entity world pos=(%.2f,%.2f,%.2f)\n",
            worldPos.x(), worldPos.y(), worldPos.z() );
}

void CinResCollection::dispatchIntroCarPart( CinematicActor *pActor,
                                             const CinActorState &state,
                                             const Eigen::Vector3f &scale ) {
    // FUN_0076bb50
    GMIntro::IntroCarPart *pPart =
        static_cast<GMIntro::IntroCarPart *>( pActor->pEntity );
    GMIntro::IntroCarSlot *pSlot = pPart->pSlot;
    if ( pSlot == nullptr || pSlot->pCarState == nullptr ) {
        return;
    }

    if ( pPart->PartIndex == -1 ) {
        const size_t length = std::strlen( pActor->Name );
        const int32_t numWheels =
            static_cast<int32_t>( pSlot->pCarState->getNumWheels() );
        const bool bRear = pActor->Name[length - 2] != 'F';
        const bool bRight = pActor->Name[length - 1] != 'L';
        const bool bFork = std::strstr( pActor->Name, "_Fork_" ) != nullptr;
        pPart->PartIndex =
            ( static_cast<int32_t>( bRear ) << ( numWheels / 2 - 1 ) ) +
            static_cast<int32_t>( bRight ) * ( numWheels / 4 ) +
            ( bFork ? 4 : 0 );
    }

    Matrix4x4f_t transform = BuildScaledActorMatrix( state, scale );
    if ( bHasBaseMatrix ) {
        transform = transform * baseMatrix;
    } else {
        const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                          ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();
        transform.row( 3 ) -= shift.transpose();
    }

    pSlot->PartMatrices[pPart->PartIndex] = transform;
    pSlot->NumPartsUpdated++;
    if ( pSlot->NumPartsUpdated >= pSlot->NumParts && pSlot->bPosePending &&
         pSlot->pActor != nullptr ) {
        applyIntroCarPose( pSlot->pActor, pSlot->PendingPose,
                           pSlot->PendingScale, pSlot->bPendingTransformTrack );
        pSlot->bPosePending = false;
        pSlot->NumPartsUpdated = 0;
    }
}

void CinResCollection::applyIntroCarPose( CinematicActor *pActor,
                                          CinActorState &state,
                                          const Eigen::Vector3f &scale,
                                          bool bHasTransformTrack ) {
    // FUN_007e5610
    GMIntro::IntroCarSlot *pSlot =
        static_cast<GMIntro::IntroCarSlot *>( pActor->pIntroCarSlot );
    if ( pSlot != nullptr ) {
        if ( !pSlot->bPosePending ) {
            pSlot->PendingPose = state;
            pSlot->PendingScale = scale;
            pSlot->bPendingTransformTrack = bHasTransformTrack;
            pSlot->bPosePending = true;
            if ( pSlot->NumPartsUpdated < pSlot->NumParts ) {
                return;
            }
            applyIntroCarPose( pActor, pSlot->PendingPose, pSlot->PendingScale,
                               pSlot->bPendingTransformTrack );
            pSlot->bPosePending = false;
            pSlot->NumPartsUpdated = 0;
            return;
        }
        if ( pSlot->NumPartsUpdated < pSlot->NumParts ) {
            return;
        }
    }

    Matrix4x4f_t transform = BuildScaledActorMatrix( state, scale );
    if ( bHasBaseMatrix ) {
        transform = transform * baseMatrix;
    }

    if ( bHasTransformTrack ) {
        const bool bUseParts = pSlot != nullptr && pSlot->NumParts != 0;
        applyCarTransform(
            pActor->pCarState, transform, pActor->bCarTeleport,
            bUseParts ? &pSlot->PartMatrices[GMIntro::kMaxIntroCarSlotParts / 2]
                      : nullptr,
            bUseParts ? &pSlot->PartMatrices[0] : nullptr );
    }

    PhysicsVehicle *pVehicle = ( pActor->pCarState != nullptr )
                                   ? pActor->pCarState->getPhysicsVehicle()
                                   : nullptr;
    if ( pVehicle != nullptr ) {
        PhysicsLock lock;
        pActor->DisplayMatrix =
            pVehicle->computeDisplayMatrix( pVehicle->getPhysicsMatrix() );
    }
}

Matrix4x4f_t
CinResCollection::buildActorMatrix( const CinActorState &state ) const {
    // FUN_007fb590 (inlined)
    Matrix4x4f_t transform = BuildRotationMatrix( state.Heading );
    transform.row( 3 ) = Eigen::Vector4f(
        state.Position.x(), state.Position.y(), state.Position.z(), 1.0f );

    if ( bHasBaseMatrix ) {
        return transform * baseMatrix;
    }

    const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                      ? gpWorldStreaming->getPosition()
                                      : Eigen::Vector4f::Zero();
    transform( 3, 0 ) -= shift.x();
    transform( 3, 1 ) -= shift.y();
    transform( 3, 2 ) -= shift.z();
    return transform;
}

void CinResCollection::checkForRegisterActor( Entity *pEntity ) {
    // FUN_007e3fd0
    ScenaricBank *pSCBank = ( pOwner != nullptr ) ? pOwner->getBank() : nullptr;
    if ( pSCBank == nullptr || !pEntity->needRegisterEntity() ||
         pSCBank->getMngActor() != nullptr || gpActiveGMScene == nullptr ) {
        return;
    }

    // FUN_007e3f70
    gpActiveGMScene->getActorManager().registerActor( pEntity );
}

void CinResCollection::checkForUnregisterActor( Entity *pEntity ) {
    // FUN_007e3fa0
    ScenaricBank *pSCBank = ( pOwner != nullptr ) ? pOwner->getBank() : nullptr;
    if ( pSCBank == nullptr || !pEntity->needRegisterEntity() ||
         pSCBank->getMngActor() != nullptr || gpActiveGMScene == nullptr ) {
        return;
    }

    // FUN_007e3f50
    gpActiveGMScene->getActorManager().removeActorFromList( pEntity );
}

void CinResCollection::animActor( EntityActor *pActor,
                                  const CinActorState *pState ) {
    // FUN_007e49e0
    if ( pOwner == nullptr ) {
        return;
    }

    AnimStateInstance *pClipA = nullptr;
    if ( pState->ClipRefA != -1 && pState->ClipColumnA != -1 ) {
        for ( int32_t row = pActor->getClipRow(); row >= 0 && pClipA == nullptr;
              row-- ) {
            pClipA = pOwner->getClip( row, pState->ClipColumnA );
        }
    }

    AnimStateInstance *pClipB = nullptr;
    if ( pState->ClipRefB != -1 && pState->ClipColumnB != -1 ) {
        for ( int32_t row = pActor->getClipRow(); row >= 0 && pClipB == nullptr;
              row-- ) {
            pClipB = pOwner->getClip( row, pState->ClipColumnB );
        }
    }

    if ( pClipA == nullptr ) {
        return;
    }

    Animation *pAnimation = static_cast<Animation *>( pActor->getAnimation() );
    if ( pAnimation == nullptr || !pAnimation->isValid() ) {
        return;
    }

    // FUN_00401330
    if ( pClipB != nullptr ) {
        pAnimation->playStatesAtTime( pClipA, pState->TimeA, pClipB,
                                      pState->TimeB, pState->Ratio );
    } else {
        pAnimation->playStatesAtTime( pClipA, pState->TimeA, nullptr, 0.0f,
                                      0.0f );
    }
}

void CinResCollection::collectActorMessages( const CinMediaContext *pCtx,
                                             const uint16_t *pTrack,
                                             float time,
                                             CinActorState &state ) {
    // FUN_0067c4a0
    if ( pCtx->pSource == nullptr || pCtx->pSource->pSceneRefs == nullptr ||
         state.NumMessages >= CinActorState::kMaxMessages ) {
        return;
    }

    int32_t lower = 0;
    int32_t upper = 0;
    FindKeyframe( pTrack, time, &lower, &upper );

    const float *pKeyTimes = reinterpret_cast<const float *>( pTrack + 2 );
    const float elapsed = time - pKeyTimes[lower];

    const float *pValues = TrackKeyframeBase( pTrack );
    const int32_t refIdx =
        reinterpret_cast<const int32_t *>( pValues )[lower * 2];
    const float duration = pValues[lower * 2 + 1];
    if ( elapsed < 0.0f || !( elapsed < duration || duration < 0.0f ) ||
         refIdx < 0 ) {
        return;
    }

    const CinRefSlot &ref = pCtx->pSource->pSceneRefs[refIdx];
    const uint8_t *pDesc = static_cast<const uint8_t *>( ref.pDescriptor );
    if ( pDesc == nullptr || pDesc[0] != 0x0b ) {
        return;
    }

    CinActorMessage &message = state.Messages[state.NumMessages++];
    message.pRef = ref.pHandle;
    message.pKey = pValues + lower * 2;
    message.pString = reinterpret_cast<const char *>(
        pDesc + ( ( pDesc[1] & 1 ) ? 0xc : 4 ) );
    message.Length = static_cast<int32_t>( std::strlen( message.pString ) );
    message.Duration = duration;
    message.Elapsed = elapsed;
}

void CinResCollection::collectActorClips( const CinMediaContext *pCtx,
                                          const uint16_t *pTrack,
                                          float time,
                                          CinActorState &state ) {
    // FUN_0067c4a0
    int32_t refA = -1;
    int32_t refB = -1;
    float timeA = 0.0f;
    float timeB = 0.0f;
    float ratio = 0.0f;
    bool bBlending = false;
    if ( !DecodeAnimClipKey( pTrack, time, refA, timeA, bBlending, refB, timeB,
                             ratio ) ) {
        return;
    }

    const CinRefSlot *pSceneRefs =
        ( pCtx->pSource != nullptr ) ? pCtx->pSource->pSceneRefs : nullptr;

    if ( refA != -1 && pSceneRefs != nullptr ) {
        state.ClipColumnA = static_cast<int32_t>(
            reinterpret_cast<intptr_t>( pSceneRefs[refA].pHandle ) );
    }
    state.ClipRefA = refA;
    state.TimeA = timeA;

    if ( !bBlending ) {
        return;
    }

    if ( refB != -1 && pSceneRefs != nullptr ) {
        state.ClipColumnB = static_cast<int32_t>(
            reinterpret_cast<intptr_t>( pSceneRefs[refB].pHandle ) );
    }
    state.ClipRefB = refB;
    state.TimeB = timeB;
    state.Ratio = ratio;
}

bool CinResCollection::isMessageFired( const void *pKey ) const {
    // FUN_007e7860
    for ( const FiredMessage &fired : firedMessages ) {
        if ( fired.pKey == pKey ) {
            return true;
        }
    }
    return false;
}

void CinResCollection::recordFiredMessage( const char *pMessage,
                                           const void *pKey ) {
    if ( firedMessages.size() >= kMaxFiredMessages ) {
        return;
    }

    const uint32_t tag =
        ( static_cast<uint32_t>( static_cast<uint8_t>( pMessage[0] ) ) << 24 ) |
        ( static_cast<uint32_t>( static_cast<uint8_t>( pMessage[1] ) ) << 16 ) |
        ( static_cast<uint32_t>( static_cast<uint8_t>( pMessage[2] ) ) << 8 ) |
        static_cast<uint32_t>( static_cast<uint8_t>( pMessage[3] ) );
    firedMessages.push_back( { tag, pKey } );
}

void CinResCollection::removeFiredMessages( uint32_t tag ) {
    // FUN_007e77c0
    for ( size_t i = 0; i < firedMessages.size(); ) {
        if ( firedMessages[i].Tag == tag ) {
            firedMessages[i] = firedMessages.back();
            firedMessages.pop_back();
        } else {
            i++;
        }
    }
}

static void SetFadeColor( const float *pRGB ) {
    static constexpr float kFloatToByte = 255.0f; // DAT_00f41688
    gpMngFade->setTransitionColorRGB(
        static_cast<uint8_t>( FloatToInt( pRGB[0] * kFloatToByte ) ),
        static_cast<uint8_t>( FloatToInt( pRGB[1] * kFloatToByte ) ),
        static_cast<uint8_t>( FloatToInt( pRGB[2] * kFloatToByte ) ) );
}

void CinResCollection::onShotEvaluated( int32_t shotIndex,
                                        const float *pShotKey,
                                        float shotTime ) {
    // FUN_007e12c0
    static constexpr uint32_t kCineAvatarModeMessages[] = {
        0x61766d31, // "avm1"
        0x61766d32, // "avm2"
        0x61766d33, // "avm3"
        0x61766d34, // "avm4"
    };
    static constexpr uint32_t kIntroCarMessages[] = {
        0x636c646c, // "cldl"
        0x636c6472, // "cldr"
        0x636c6461, // "clda"
        0x6f70646c, // "opdl"
        0x6f706472, // "opdr"
        0x6f706461, // "opda"
        0x6f707772, // "opwr"
        0x6f70776c, // "opwl"
        0x6f707761, // "opwa"
        0x636c7772, // "clwr"
        0x636c776c, // "clwl"
        0x636c7761, // "clwa"
    };

    if ( bFirstShotConsumed ) {
        bFirstShotPending = false;
        bFirstShotConsumed = false;
    }
    if ( bFirstShotPending ) {
        bFirstShotConsumed = true;
    }

    const float duration = pShotKey[0];
    const float fadeInDuration = pShotKey[6];
    const float fadeOutDuration = pShotKey[11];

    if ( shotIndex != currentShot ) {
        currentShot = shotIndex;
        bPlayerCarTeleport = true;

        if ( pOwner != nullptr ) {
            for ( const uint32_t tag : kCineAvatarModeMessages ) {
                removeFiredMessages( tag );
            }
            if ( gpTestDriveInstance != nullptr &&
                 gpTestDriveInstance->getActiveGameMode() == GM_Intro ) {
                for ( const uint32_t tag : kIntroCarMessages ) {
                    removeFiredMessages( tag );
                }
            }
        }

        if ( fadeInDuration > 0.0f && gpMngFade != nullptr ) {
            const uint32_t fadeFlags = gpMngFade->getTransitionFlags();
            SetFadeColor( &pShotKey[7] );
            gpMngFade->restartTransition( ( fadeFlags & ~8u ) | 5u,
                                          fadeInDuration );
        }
        bFadeOutNotDone = true;
    }

    const float fadeOutStart = duration - fadeOutDuration;
    if ( fadeOutStart < shotTime && fadeOutDuration > 0.0f &&
         gpMngFade != nullptr ) {
        const float remaining = fadeOutDuration - ( shotTime - fadeOutStart );
        const uint32_t fadeFlags = gpMngFade->getTransitionFlags();
        if ( ( fadeFlags & 1u ) == 0 && remaining > 0.0f ) {
            SetFadeColor( &pShotKey[12] );
            gpMngFade->restartTransition( ( fadeFlags & ~0xcu ) | 1u,
                                          lastDeltaTime + remaining );
            bFadeOutNotDone = false;
        }
    }
}

bool CinResCollection::onCinematicMessage( const void *pKey,
                                           const char *pMessage ) {
    // FUN_007e1dc0
    EntityDriver *pDriver =
        ( pPlayerAvatar != nullptr ) ? pPlayerAvatar->getDriver() : nullptr;
    const FlashMessage message( pMessage );
    bool bConsumed = true;

    if ( message == FlashMessage( "pllo" ) ) {
        if ( !isMessageFired( pKey ) && pMusicEmitter != nullptr ) {
            pMusicEmitter->play( true, 0.0f );
        }
    } else if ( message == FlashMessage( "play" ) ||
                message == FlashMessage( "plon" ) ) {
        if ( !isMessageFired( pKey ) && pMusicEmitter != nullptr ) {
            pMusicEmitter->play( false, 0.0f );
        }
    } else if ( message == FlashMessage( "stop" ) ) {
        if ( !isMessageFired( pKey ) && pMusicEmitter != nullptr ) {
            pMusicEmitter->stop( 0.0f );
        }
    } else if ( message == FlashMessage( "avm1" ) ) {
        if ( pDriver != nullptr ) {
            pDriver->leaveSeatForCinematic();
            pDriver->setCineAvatarMode( 1 );
        }
    } else if ( message == FlashMessage( "avm2" ) ) {
        if ( pDriver != nullptr ) {
            if ( IsLocalPlayerCarInstalled() ) {
                pDriver->placeInLocalPlayerCar();
            }
            pDriver->setSeating( true );
            pDriver->setFreeMovement( true );
            pDriver->resetAnimationChannels();
            pDriver->requestSeatRefresh();
            pDriver->setUsedIK( false );
            pDriver->resetAnimationChannels();
            pDriver->requestSeatRefresh();
            pDriver->setCineAvatarMode( 2 );
        }
    } else if ( message == FlashMessage( "avm3" ) ) {
        if ( pDriver != nullptr ) {
            if ( IsLocalPlayerCarInstalled() ) {
                pDriver->placeInLocalPlayerCar();
            }
            pDriver->setSeating( true );
            pDriver->setFreeMovement( true );
            pDriver->resetAnimationChannels();
            pDriver->setUsedIK( true );
            pDriver->requestSeatRefresh();
            pDriver->setCineAvatarMode( 3 );
        }
    } else if ( message == FlashMessage( "avm4" ) ) {
        if ( pDriver != nullptr ) {
            if ( IsLocalPlayerCarInstalled() ) {
                pDriver->placeInLocalPlayerCar();
            }
            pDriver->setSeating( true );
            pDriver->setFreeMovement( false );
            pDriver->setUsedIK( true );
            pDriver->requestSeatRefresh();
            pDriver->setCineAvatarMode( 4 );
        }
    } else if ( message == FlashMessage( "321g" ) ) {
        if ( !isMessageFired( pKey ) ) {
            FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
            const char *pPath = "/3_2_1_go";
            if ( gpActiveGM->getGameMech()->getMechType() ==
                 GameMechanism::MT_ExtraChallenge ) {
                pPath = "/go_extra";
            }
            if ( pMessage[5] == '2' ) {
                pHudPlayer->showFrame( "2_go", pPath, true );
            } else if ( pMessage[5] == '3' ) {
                pHudPlayer->showFrame( "3_go", pPath, true );
            } else {
                pHudPlayer->showFrame( "1_go", pPath, true );
            }
        }
    } else if ( message == FlashMessage( "chmi" ) ) {
        pCurrentMicro = findMicroByName( pMessage + 5 );
        if ( gpAudio != nullptr ) {
            gpAudio->setListenerSyncedToCamera( pCurrentMicro == nullptr );
        }
    } else if ( message == FlashMessage( "dof1" ) ) {
        float focale = 0.0f;
        float postRamp = 0.0f;
        float preRamp = 0.0f;
        sscanf( pMessage + 5, "%f:%f:%f", &focale, &postRamp, &preRamp );
        CameraGame *pCamera = ( gpCamera != nullptr )
                                  ? gpCamera->findCameraWithMode(
                                        0, gpCamera->getBoundCarState() )
                                  : nullptr;
        if ( pCamera != nullptr ) {
            float *pFocale = pCamera->getFocaleInfosRW();
            pFocale[0] = focale;
            pFocale[1] = preRamp;
            pFocale[2] = postRamp;
        }
    } else if ( message == FlashMessage( "rbof" ) ) {
        if ( !isMessageFired( pKey ) && gpCamera != nullptr ) {
            gpCamera->setUseBlur( false );
        }
    } else if ( message == FlashMessage( "rbon" ) ) {
        if ( !isMessageFired( pKey ) ) {
            float intensity = kDefaultRadialBlurIntensity;
            sscanf( pMessage + 5, "%f", &intensity );
            if ( gpCamera != nullptr ) {
                gpCamera->setUseBlur( true );
            }
            PostFXStep *pStep =
                ( gpPostFX != nullptr )
                    ? gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
                    : nullptr;
            if ( pStep != nullptr &&
                 static_cast<PostFXStepRadialBlur *>( pStep )->pNode !=
                     nullptr ) {
                static_cast<PostFXStepRadialBlur *>( pStep )
                    ->pNode->setIntensity( intensity );
            }
        }
    } else if ( message == FlashMessage( "plvo" ) ) {
        if ( !isMessageFired( pKey ) ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC (voice emitters,
                                     // FUN_007debc0)
        }
    } else if ( message == FlashMessage( "zcne" ) ) {
        applyZConfig( zConfigNear );
    } else if ( message == FlashMessage( "zcde" ) ) {
        applyZConfig( zConfigDefault );
    } else if ( message == FlashMessage( "zcfa" ) ) {
        applyZConfig( zConfigFar );
    } else if ( message == FlashMessage( "zcvn" ) ) {
        applyZConfig( zConfigVeryNear );
    } else if ( message == FlashMessage( "lafe" ) ) {
    } else {
        bConsumed = false;
        CinematicPlayer *pGMInterface =
            ( pOwner != nullptr ) ? pOwner->getGameModeInterface() : nullptr;
        if ( pGMInterface != nullptr && !isMessageFired( pKey ) ) {
            bConsumed = pGMInterface->executeAction(
                CE_FlashMessage, pOwner, const_cast<char *>( pMessage ) );
        }
    }

    if ( !isMessageFired( pKey ) && bConsumed ) {
        recordFiredMessage( pMessage, pKey );
    }
    return bConsumed;
}

Entity *CinResCollection::findMicroByName( const char *pName ) const {
    // FUN_007e1a60
    ScenaricBank *pSCBank = ( pOwner != nullptr ) ? pOwner->getBank() : nullptr;
    if ( pSCBank == nullptr ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < pSCBank->getEntityCount(); i++ ) {
        Entity *pEntity = pSCBank->getEntity( i );
        if ( pEntity != nullptr && pEntity->getType() == eEntityType::ET_MICR &&
             pEntity->getInstanceName() == pName ) {
            return pEntity;
        }
    }
    return nullptr;
}

void CinResCollection::applyZConfig( const ZConfig &config ) {
    // FUN_007e1190
    const float nearPlane =
        ( config.Near < 0.0f ) ? gpRender->getDefaultNearPlane() : config.Near;
    const float farPlane =
        ( config.Far < 0.0f ) ? gpRender->getDefaultFarPlane() : config.Far;

    if ( pCinematicCamera == nullptr || gpCamera == nullptr ) {
        return;
    }

    if ( !bVeryFarScene ) {
        pCinematicCamera->setInteriorNearPlane(
            gpRender->getDefaultInteriorNearPlane() );
        pCinematicCamera->setInteriorFarPlane(
            gpRender->getDefaultInteriorFarPlane() );
        pCinematicCamera->setNearPlane( nearPlane );
        pCinematicCamera->setFarPlane( farPlane );
    } else {
        pCinematicCamera->setInteriorNearPlane( nearPlane );
        pCinematicCamera->setInteriorFarPlane( farPlane );
        pCinematicCamera->setNearPlane( ( zConfigVeryFar.Near >= 0.0f )
                                            ? zConfigVeryFar.Near
                                            : gpRender->getDefaultNearPlane() );
        pCinematicCamera->setFarPlane( ( zConfigVeryFar.Far >= 0.0f )
                                           ? zConfigVeryFar.Far
                                           : gpRender->getDefaultFarPlane() );
    }

    gpCamera->setFarPlane( pCinematicCamera->getFarPlane() );
    gpCamera->setNearPlane( pCinematicCamera->getNearPlane() );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( pCinematicCamera->getInteriorFarPlane() );
    gpCamera->setInteriorNearPlane( pCinematicCamera->getInteriorNearPlane() );
    gpCamera->setEnableDepthAwareInterior( true );
    pCinematicCamera->setKeepGlobalPlanes( true );
}

void CinResCollection::tickCameraMedia( const CinMediaContext *pCtx,
                                        float time ) {
    // FUN_0067c0c0
    const uint8_t *pEntry = static_cast<const uint8_t *>(
        static_cast<const void *>( pCtx->pEntry ) );
    const int32_t propOffset = *reinterpret_cast<const int32_t *>( pEntry + 4 );
    const int32_t numProps =
        *reinterpret_cast<const int32_t *>( pEntry + 0x10 );

    int32_t transformType = 0;
    float fov = 0.0f;
    Eigen::Vector3f position = Eigen::Vector3f::Zero();
    Eigen::Quaternionf heading = Eigen::Quaternionf::Identity();
    Eigen::Vector3f up = Eigen::Vector3f::Zero();
    Eigen::Vector3f target = Eigen::Vector3f::Zero();
    int32_t targetRef = -1;
    int32_t posRef = -1;

    const uint8_t *pProp = pEntry + propOffset;
    for ( int32_t i = 0; i < numProps; i++ ) {
        const int16_t subtype = *reinterpret_cast<const int16_t *>( pProp );
        const int32_t advance = *reinterpret_cast<const int32_t *>( pProp + 4 );
        const uint32_t typeHash =
            *reinterpret_cast<const uint32_t *>( pProp + 8 );
        const uint16_t *pTrack =
            reinterpret_cast<const uint16_t *>( pProp + 0xc );

        switch ( typeHash ) {
        case 0x64c8d3b1u: // heading
            if ( subtype == 0 ) {
                heading = InterpolateQuatTrack( pTrack, time );
            } else if ( subtype == 2 ) {
                target = InterpolateVec3RefTrack( pTrack, time, &targetRef );
            }
            break;
        case 0x00007055u: // up (RELATIVE mode)
            up = InterpolateVec3Track( pTrack, time );
            break;
        case 0x00766f46u: // field of view
            fov = InterpolateScalarTrack( pTrack, time );
            break;
        case 0xd7e2d8c4u: // position
            if ( subtype == 1 ) {
                transformType = 1;
                position = InterpolateVec3Track( pTrack, time );
            } else if ( subtype == 2 ) {
                transformType = 2;
                position = InterpolateVec3RefTrack( pTrack, time, &posRef );
            }
            break;
        default:
            break;
        }

        pProp += advance;
    }

    if ( !bQuiet )
        OTDU_LOG_ALWAYS(
            "[CINCAM] t=%.3f type=%d fov=%.3f pos=(%.2f,%.2f,%.2f) "
            "head=(%.3f,%.3f,%.3f,%.3f) up=(%.2f,%.2f,%.2f) "
            "tgt=(%.2f,%.2f,%.2f) "
            "tgtRef=%d posRef=%d\n",
            time, transformType, fov, position.x(), position.y(), position.z(),
            heading.x(), heading.y(), heading.z(), heading.w(), up.x(), up.y(),
            up.z(), target.x(), target.y(), target.z(), targetRef, posRef );

    applyCameraTransform( pCtx, transformType, position, heading, fov, target,
                          up, posRef, targetRef, time );
}

const char *CinResCollection::GetActorRefString( const CinMediaContext *pCtx,
                                                 int32_t refIdx,
                                                 CinematicActor *&pOutActor ) {
    pOutActor = nullptr;
    if ( pCtx == nullptr || pCtx->pSource == nullptr ||
         pCtx->pSource->pSceneRefs == nullptr || refIdx < 0 ) {
        return nullptr;
    }

    const CinRefSlot &slot = pCtx->pSource->pSceneRefs[refIdx];
    const uint8_t *pDesc = static_cast<const uint8_t *>( slot.pDescriptor );
    if ( pDesc == nullptr || pDesc[0] != 0x0c ) {
        return nullptr;
    }

    pOutActor = static_cast<CinematicActor *>( slot.pHandle );
    const bool bHasId = ( pDesc[1] & 1 ) != 0;
    return reinterpret_cast<const char *>( pDesc + ( bHasId ? 0xc : 4 ) );
}

static const Eigen::Vector4f *FindCarRefOffset( const char *pName,
                                                int32_t carID ) {
    // FUN_007fce50
    static constexpr const char *kCarRefOffsetNames[kNumPassengerIKOffsets] = {
        // PTR_DAT_00f8b1b8
        "Body_HR",    "Fork_FL",    "Fork_FR",  "Fork_RL", "Fork_RR", "Plate",
        "DvrSeat_HR", "PsgSeat_HR", "Steer_HR", "Door_R",  "Door_L" };

    if ( gpScenaric == nullptr ) {
        return nullptr;
    }

    SCIKOffsetDB &ikOffsetDB = gpScenaric->getIKOffsetDB();
    SCIKOffset *pOffsets = nullptr;
    if ( SCIKBikeOffsets *pBike = ikOffsetDB.findBikeOffsets( carID, true ) ) {
        pOffsets = pBike->PassengerOffsets;
    } else if ( SCIKCarOffsets *pCar =
                    ikOffsetDB.findCarOffsets( carID, true ) ) {
        pOffsets = pCar->PassengerOffsets;
    }
    if ( pOffsets == nullptr ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < kNumPassengerIKOffsets; i++ ) {
        if ( std::strcmp( kCarRefOffsetNames[i], pName ) == 0 ) {
            return &pOffsets[i].Offset;
        }
    }
    return nullptr;
}

static bool FindCarRefNodePoint( CarState *pCarState,
                                 const char *pName,
                                 Eigen::Vector4f &outPoint ) {
    // FUN_007e35a0
    if ( pCarState == nullptr ) {
        return false;
    }

    const uint64_t hashcode = GetIdentifier64bit( pName );
    HiearchyNode *pNode = nullptr;

    DBEntryCarPhysicsData *pCarData =
        ( gpDatabase != nullptr )
            ? gpDatabase->getCarDBEntry( pCarState->getCarID() )
            : nullptr;
    if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC
    } else {
        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        if ( pDisplay != nullptr &&
             ( pDisplay->getType() == VehicleBase::eType::VBT_Car ||
               pDisplay->getType() == VehicleBase::eType::VBT_Bike ) ) {
            HiearchyNode *pHighResRoot =
                static_cast<VehicleController *>( pDisplay )
                    ->getInstantiatedInteriorNode();
            HiearchyNode *pLowResRoot = pDisplay->shouldUnstream()
                                            ? nullptr
                                            : pDisplay->getNodeByIndex( 0 );
            if ( pHighResRoot != nullptr ) {
                pNode = pHighResRoot->findChild( hashcode );
            }
            if ( pNode == nullptr && pLowResRoot != nullptr ) {
                pNode = pLowResRoot->findChild( hashcode );
            }
        }
    }

    const Eigen::Vector4f *pOffset =
        FindCarRefOffset( pName, pCarState->getCarID() );
    if ( pNode == nullptr ) {
        return false;
    }

    Matrix4x4f_t nodeMatrix = Matrix4x4f_t::Identity();
    const int32_t depth = AccumulateNodeWorldMatrix( pNode, nodeMatrix );
    outPoint = nodeMatrix.row( 3 ).transpose();
    if ( pOffset != nullptr ) {
        outPoint += *pOffset;
    }
    return depth != 0;
}

bool CinResCollection::resolveActorRefPoint( const CinematicActor *pActor,
                                             const char *pRefString,
                                             const Eigen::Vector3f &spec,
                                             Eigen::Vector4f &outPoint,
                                             bool &bOutDeferred ) const {
    // FUN_007e3830
    bOutDeferred = false;
    if ( pActor == nullptr ) {
        return false;
    }

    char scratch[0x200] = {};
    strncpy( scratch, pRefString, sizeof( scratch ) - 1 );
    char *pSeparator = std::strrchr( scratch, ':' );
    if ( pSeparator == nullptr ) {
        return false;
    }
    *pSeparator = '\0';
    pSeparator = std::strrchr( scratch, ':' );
    if ( pSeparator == nullptr ) {
        return false;
    }
    const char *pName = pSeparator + 1;

    // FUN_007e3790
    Eigen::Vector4f refPoint = Eigen::Vector4f::Zero();
    switch ( pActor->Kind ) {
    case CinematicActor::CAK_PlayerCar:
    case CinematicActor::CAK_TrafficCar:
    case CinematicActor::CAK_IntroCar:
        bOutDeferred = true;
        if ( !FindCarRefNodePoint( pActor->pCarState, pName, refPoint ) ) {
            return false;
        }
        break;
    case CinematicActor::CAK_Entity: {
        const Entity *pEntity = static_cast<const Entity *>( pActor->pEntity );
        if ( pEntity == nullptr ) {
            return false;
        }
        refPoint = pEntity->getModelMatrix().row( 3 ).transpose();
        break;
    }
    default:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC (plane ref)
        return false;
    }

    outPoint = refPoint;
    if ( bOutDeferred ) {
        outPoint.w() = 0.0f;
        return true;
    }

    Eigen::Vector3f offset = spec;
    if ( bHasBaseMatrix ) {
        offset = baseMatrix.topLeftCorner<3, 3>().transpose() * spec;
    }
    outPoint.head<3>() += offset;
    outPoint.w() = 1.0f;
    return true;
}

bool CinResCollection::resolveRefPoint(
    const CinMediaContext *pCtx,
    int32_t refIdx,
    const Eigen::Vector3f &spec,
    Eigen::Vector4f &outPoint,
    bool &bOutHasRef,
    const CinematicActor *&pOutDeferredActor ) const {
    // FUN_007e3960
    pOutDeferredActor = nullptr;

    CinematicActor *pActor = nullptr;
    const char *pRefString = GetActorRefString( pCtx, refIdx, pActor );
    if ( pRefString == nullptr ) {
        bOutHasRef = false;
        outPoint = Eigen::Vector4f( spec.x(), spec.y(), spec.z(), 1.0f );
        if ( bHasBaseMatrix ) {
            outPoint = TDUMat4x4MulVec( baseMatrix, outPoint );
        }
        outPoint.w() = 1.0f;
        return true;
    }

    bool bDeferred = false;
    const bool bResolved =
        resolveActorRefPoint( pActor, pRefString, spec, outPoint, bDeferred );
    bOutHasRef = true;
    if ( bResolved && bDeferred ) {
        pOutDeferredActor = pActor;
    }
    return bResolved;
}

void CinResCollection::resolveDeferredRefPoint(
    const CinematicActor *pActor,
    const Eigen::Vector4f &spec,
    Eigen::Vector4f &inOutPoint ) const {
    const Matrix4x4f_t inverseTrack = pActor->TrackMatrix.inverse();
    inOutPoint += TDUMat4x4MulVec( inverseTrack, spec );
    inOutPoint.w() = 1.0f;
    inOutPoint = TDUMat4x4MulVec( pActor->DisplayMatrix, inOutPoint );
}

void CinResCollection::resolveRefCamera() {
    // FUN_007e3370
    if ( !bRefCameraArmed ) {
        return;
    }

    if ( bRefCameraHasPositionRef && pRefCameraPositionActor != nullptr ) {
        resolveDeferredRefPoint( pRefCameraPositionActor, refCameraPositionSpec,
                                 refCameraPosition );
        pRefCameraPositionActor = nullptr;
        bRefCameraHasPositionRef = false;
    }
    if ( bRefCameraHasTargetRef && pRefCameraTargetActor != nullptr ) {
        resolveDeferredRefPoint( pRefCameraTargetActor, refCameraTargetSpec,
                                 refCameraTarget );
        pRefCameraTargetActor = nullptr;
        bRefCameraHasTargetRef = false;
    }

    // FUN_007e3290
    cameraView.Position = refCameraPosition.head<3>();
    cameraView.Target = refCameraTarget.head<3>();
    cameraView.Up = refCameraUp.head<3>();
    cameraView.Fov = refCameraFov;
    cameraView.bValid = true;
    applyViewToCinematicCamera( pCinematicCamera, 0.0f, false );

    bRefCameraArmed = false;
}

void CinResCollection::applyCameraTransform( const CinMediaContext *pCtx,
                                             int32_t transformType,
                                             const Eigen::Vector3f &position,
                                             const Eigen::Quaternionf &heading,
                                             float fov,
                                             const Eigen::Vector3f &target,
                                             const Eigen::Vector3f &up,
                                             int32_t posRef,
                                             int32_t targetRef,
                                             float time ) {
    // FUN_007e3a50
    cameraView.bValid = false;
    CameraCinematic *pCamera =
        ( pCtx != nullptr && pCtx->pHandleSlot != nullptr )
            ? static_cast<CameraCinematic *>( *pCtx->pHandleSlot )
            : nullptr;

    if ( transformType == 2 ) {
        bRefCameraArmed = true;
        refCameraPositionSpec =
            Eigen::Vector4f( position.x(), position.y(), position.z(), 0.0f );
        refCameraTargetSpec =
            Eigen::Vector4f( target.x(), target.y(), target.z(), 0.0f );
        pRefCameraPositionActor = nullptr;
        pRefCameraTargetActor = nullptr;
        resolveRefPoint( pCtx, posRef, position, refCameraPosition,
                         bRefCameraHasPositionRef, pRefCameraPositionActor );
        resolveRefPoint( pCtx, targetRef, target, refCameraTarget,
                         bRefCameraHasTargetRef, pRefCameraTargetActor );
        if ( bHasBaseMatrix ) {
            const Eigen::Vector3f baseUp =
                baseMatrix.topLeftCorner<3, 3>().transpose() * up;
            refCameraUp =
                Eigen::Vector4f( baseUp.x(), baseUp.y(), baseUp.z(), 0.0f );
        } else {
            refCameraUp = Eigen::Vector4f( up.x(), up.y(), up.z(), 0.0f );
        }
        refCameraFov = fov;
        return;
    }

    if ( transformType != 1 ) {
        OTDU_UNIMPLEMENTED;
        return;
    }

    Matrix4x4f_t rotation = BuildRotationMatrix( heading );
    rotation.row( 3 ) =
        Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f );
    if ( bHasBaseMatrix ) {
        rotation = rotation * baseMatrix;
    } else if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        rotation.row( 3 ).head<3>() -= shift.head<3>();
    }

    const Eigen::Vector3f forward = rotation.row( 2 ).head<3>();
    const Eigen::Vector3f rotUp = rotation.row( 1 ).head<3>();
    const Eigen::Vector3f worldPos = rotation.row( 3 ).head<3>();
    const Eigen::Vector3f lookAt = worldPos + forward;

    // Publish the view so the active render camera can be latched to it.
    cameraView.Position = worldPos;
    cameraView.Target = lookAt;
    cameraView.Up = rotUp;
    cameraView.Fov = fov;
    cameraView.bValid = true;

    applyViewToCinematicCamera( pCamera, time, true );

    if ( !bQuiet )
        OTDU_LOG_ALWAYS(
            "[CINCAM]   view -> pos=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f) "
            "up=(%.2f,%.2f,%.2f) fov=%.2f\n",
            position.x(), position.y(), position.z(), target.x(), target.y(),
            target.z(), up.x(), up.y(), up.z(), fov );
}

bool CinResCollection::createInterface( const void *pBankData,
                                        uint32_t size,
                                        void *pBufferIn,
                                        BankInstance *pSourceBank ) {
    // FUN_007e7c10
    if ( bCreated ) {
        return true;
    }
    if ( pBankData == nullptr ) {
        return false;
    }
    if ( pBufferIn == nullptr ) {
        return false;
    }
    pBank = pSourceBank;

    // FUN_007e0eb0
    bFirstShotPending = true;
    bFirstShotConsumed = false;
    bAvatarShadowTarget = false;

    if ( !load( pBankData, size, pBufferIn ) ) {
        return false;
    }
    initialize();
    bCreated = true;
    return true;
}

const uint8_t *CinResCollection::resolveRef( int32_t index ) const {
    // FUN_0067b250
    if ( pRefData == nullptr ) {
        return nullptr;
    }
    OTDU_ASSERT( index >= 0 &&
                 index < *static_cast<const int32_t *>( pRefData ) );

    const uint8_t *pEntry = static_cast<const uint8_t *>( pRefData ) + 4;
    for ( ; index != 0; index-- ) {
        pEntry += *reinterpret_cast<const uint16_t *>( pEntry + 2 );
    }
    return pEntry;
}

bool CinResCollection::buildSourceMedia( const CinSourceEntry &source,
                                         void **&pSourceData ) {
    // FUN_0067b2a0
    const uint8_t *pRefEntry = resolveRef( source.RefId );
    if ( pRefEntry == nullptr ) {
        return false;
    }

    const uint8_t flags = pRefEntry[1];
    uint64_t hashcode = 0;
    if ( ( flags & 1 ) != 0 ) {
        hashcode = ( static_cast<uint64_t>(
                         *reinterpret_cast<const uint32_t *>( pRefEntry + 8 ) )
                     << 32 ) |
                   *reinterpret_cast<const uint32_t *>( pRefEntry + 4 );
    }
    const char *pName = nullptr;
    if ( ( flags & 2 ) != 0 ) {
        pName = reinterpret_cast<const char *>(
            pRefEntry + ( ( flags & 1 ) != 0 ? 0xc : 4 ) );
    }

    uint32_t sceneSize = 0;
    void *pHandle = loadSourceByName(
        pName, ( flags & 1 ) != 0 ? &hashcode : nullptr, &sceneSize );
    pRefTable[source.RefId].pHandle = pHandle;

    return parseSourceScene( pHandle, sceneSize, pSourceData );
}

void *CinResCollection::buildRefMedia( const uint8_t *pRefEntry ) {
    // FUN_007e2420
    if ( pRefEntry == nullptr ) {
        return nullptr;
    }

    const uint8_t refType = pRefEntry[0];
    const bool bHasId = ( pRefEntry[1] & 1 ) != 0;
    const char *pPayload =
        reinterpret_cast<const char *>( pRefEntry + ( bHasId ? 0xc : 4 ) );

    switch ( refType ) {
    case 0x02: {
        // FUN_007e7cc0
        if ( !bHasId || pOwner == nullptr ) {
            return nullptr;
        }
        const uint32_t low =
            *reinterpret_cast<const uint32_t *>( pRefEntry + 4 );
        const uint32_t high =
            *reinterpret_cast<const uint32_t *>( pRefEntry + 8 );
        const uint64_t key = ( static_cast<uint64_t>( high ) << 32 ) | low;
        if ( key == 0 ) {
            return nullptr;
        }
        return reinterpret_cast<void *>(
            static_cast<intptr_t>( pOwner->findClipColumn( key ) ) );
    }
    case 0x0b: {
        const FlashMessage message( pPayload );
        if ( message == FlashMessage( "opwr" ) ||
             message == FlashMessage( "opwa" ) ||
             message == FlashMessage( "opwl" ) ) {
            if ( gpActiveGMScene != nullptr ) {
                bMustCloseWindowsAndDoors = true;
            }
        } else if ( message == FlashMessage( "plvo" ) ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC (voice emitters,
                                     // FUN_007dee40)
        }
        return nullptr;
    }
    case 0x0c: {
        char scratch[200] = {};
        strncpy( scratch, pPayload, sizeof( scratch ) - 1 );
        char *pActors = std::strstr( scratch, "Actors:" );
        if ( pActors == nullptr ) {
            return nullptr;
        }
        char *pName = pActors + 7;
        char *pEnd = std::strchr( pName, ':' );
        if ( pEnd == nullptr ) {
            return nullptr;
        }
        *pEnd = '\0';
        return findActor( pName );
    }
    default:
        return nullptr;
    }
}

void *CinResCollection::loadSourceByName( const char *pName,
                                          const uint64_t *,
                                          uint32_t *pOutSize ) {
    // FUN_007e1c70
    pPlayerCarActor = nullptr;
    bPlayerCarSpawned = false;

    *pOutSize = 0;
    if ( pBank == nullptr || pName == nullptr ) {
        return nullptr;
    }

    bActorPoolActive = true;
    if ( pPendingPlayerCar != nullptr ) {
        bindPlayerCar( pPendingPlayerCar );
    }

    ParsedBankEntry entry;
    pBank->getEntry( const_cast<char *>( pName ), entry );
    *pOutSize = static_cast<uint32_t>( entry.Size );
    void *pData = entry.pData;
    entry.Clear();
    return pData;
}

bool CinResCollection::parseSourceScene( void *pSceneBlob,
                                         uint32_t size,
                                         void **&pSourceData ) {
    // FUN_0067cdd0
    if ( pSceneBlob == nullptr ) {
        return false;
    }

    // Split the CinScene into SCE! (version) / DATA / REFS payloads.
    const uint8_t *pSceneData = nullptr;
    const uint8_t *pSceneRefs = nullptr;
    {
        const uint8_t *pCursor = static_cast<const uint8_t *>( pSceneBlob );
        const uint8_t *pEnd = pCursor + size;
        while ( pCursor < pEnd ) {
            const CinematicChunk *pChunk =
                reinterpret_cast<const CinematicChunk *>( pCursor );
            const uint8_t *pPayload = pCursor + sizeof( CinematicChunk );
            switch ( pChunk->Magic ) {
            case kCinematicSceneMagic:
                if ( *reinterpret_cast<const uint32_t *>( pPayload ) !=
                     kCinematicSceneVersion ) {
                    return false;
                }
                break;
            case kCinematicDataMagic:
                pSceneData = pPayload;
                break;
            case kCinematicRefsMagic:
                pSceneRefs = pPayload;
                break;
            default:
                break;
            }
            pCursor += sizeof( CinematicChunk ) + pChunk->Size;
        }
    }
    if ( pSceneData == nullptr ) {
        return false;
    }

    void **pBase = pSourceData;
    pBase[0] = const_cast<uint8_t *>( pSceneData );
    void **pHandles = pBase + 1;

    const int32_t numMedia = *reinterpret_cast<const int32_t *>(
        pSceneData + kCinSceneMediaCountOffset );

    const uint8_t *pEntry = pSceneData + kCinSceneMediaArrayOffset;
    for ( int32_t i = 0; i < numMedia; i++ ) {
        const CinSceneMediaHeader *pMedia =
            reinterpret_cast<const CinSceneMediaHeader *>( pEntry );
        switch ( pMedia->TypeHash ) {
        case kCinMediaCamera:
            pHandles[i] = createCameraMedia( pMedia );
            break;
        case kCinMediaActor:
            pHandles[i] = createActorMedia( pMedia );
            break;
        default:
            pHandles[i] = nullptr;
            break;
        }
        pEntry += pMedia->Size;
    }

    // The scene's ref table follows the media handles.
    CinRefSlot *pSceneRefTable =
        reinterpret_cast<CinRefSlot *>( pHandles + numMedia );
    int32_t numSceneRefs = 0;
    if ( pSceneRefs != nullptr ) {
        numSceneRefs = *reinterpret_cast<const int32_t *>( pSceneRefs );
        const uint8_t *pRefEntry = pSceneRefs + 4;
        for ( int32_t i = 0; i < numSceneRefs; i++ ) {
            pSceneRefTable[i].pDescriptor = pRefEntry;
            pSceneRefTable[i].pHandle = nullptr;
            if ( ( pRefEntry[0] & 0xff ) != 3 ) {
                pSceneRefTable[i].pHandle = buildRefMedia( pRefEntry );
            }
            pRefEntry += *reinterpret_cast<const uint16_t *>( pRefEntry + 2 );
        }
    }

    // Advance past [scene ptr][numMedia handles][numSceneRefs * CinRefSlot].
    pSourceData = reinterpret_cast<void **>( pSceneRefTable + numSceneRefs );
    return true;
}

void *CinResCollection::createCameraMedia( const CinSceneMediaHeader * ) {
    // FUN_007e3720
    if ( pOwner != nullptr && !pOwner->hasFocus() ) {
        return nullptr;
    }

    if ( pCinematicCamera == nullptr && gpCamera != nullptr ) {
        CameraGame *pFound = gpCamera->findCameraWithMode(
            kCinematicCameraMode, gpCamera->getBoundCarState() );
        if ( pFound != nullptr && gpCamera->isRegisteredCamera( pFound ) ) {
            pCinematicCamera = static_cast<CameraCinematic *>( pFound );
        }
    }

    if ( pCinematicCamera != nullptr ) {
        pCinematicCamera->setCinematicInterface(
            const_cast<CinResCollection *>( this ) );
        pCinematicCamera->setKeepGlobalPlanes( false );
    }

    return pCinematicCamera;
}

Eigen::Vector3f CinResCollection::tickCameraShake( CameraCinematic *pCamera,
                                                   const float deltaTime ) {
    // FUN_007e0620
    if ( !cameraShake.bEnabled ) {
        return Eigen::Vector3f::Zero();
    }

    const Eigen::Vector3f viewVector =
        pCamera->getTargetOffset() - pCamera->getWorldPositionOffset();
    const float distance = viewVector.norm();

    StepCameraShakeAngle( cameraShake.AngleX, cameraShake.TargetAngleX,
                          cameraShake.Speed, cameraShake.Amplitude, deltaTime );
    StepCameraShakeAngle( cameraShake.AngleY, cameraShake.TargetAngleY,
                          cameraShake.Speed, cameraShake.Amplitude, deltaTime );

    const Eigen::Vector3f right =
        pCamera->getUp().cross( viewVector ).normalized();
    const Eigen::Vector3f up = viewVector.cross( right ).normalized();

    return right * tanf( cameraShake.AngleX ) * distance +
           up * tanf( cameraShake.AngleY ) * distance;
}

void CinResCollection::applyViewToCinematicCamera( CameraCinematic *pCamera,
                                                   const float time,
                                                   const bool bApplyShake ) {
    if ( pCamera == nullptr || !cameraView.bValid ) {
        return;
    }

    pCamera->setFOV( cameraView.Fov * kDegToRad );
    pCamera->setPositionOffseted(
        Eigen::Vector4f( cameraView.Position.x(), cameraView.Position.y(),
                         cameraView.Position.z(), 1.0f ) );
    pCamera->setUp( Eigen::Vector4f( cameraView.Up.x(), cameraView.Up.y(),
                                     cameraView.Up.z(), 0.0f ) );
    pCamera->setTargetOffseted(
        Eigen::Vector4f( cameraView.Target.x(), cameraView.Target.y(),
                         cameraView.Target.z(), 1.0f ) );

    if ( bApplyShake ) {
        float shakeDelta = time - gCinematicShakePreviousTime;
        shakeDelta = std::min( std::max( shakeDelta, 0.0f ), 1.0f );
        gCinematicShakePreviousTime = time;

        const Eigen::Vector3f shake = tickCameraShake( pCamera, shakeDelta );
        const Eigen::Vector3f shakenTarget = cameraView.Target + shake;
        pCamera->setTargetOffseted( Eigen::Vector4f(
            shakenTarget.x(), shakenTarget.y(), shakenTarget.z(), 1.0f ) );
    }

    pCamera->setFOVAndAspectRatio( kCinematicReferenceAspectRatio,
                                   pCamera->getFOV() );

    pCamera->updateMatrix( pCamera->getWorldPosition(), pCamera->getUp(),
                           pCamera->getTarget() );
}

void CinResCollection::releaseCinematicCamera() {
    if ( pCinematicCamera == nullptr ) {
        return;
    }
    if ( pCinematicCamera->getCinematicInterface() == this ) {
        pCinematicCamera->destroy();
    }
    pCinematicCamera = nullptr;
}

void *CinResCollection::createActorMedia( const CinSceneMediaHeader *pMedia ) {
    // FUN_0067cd60
    const char *pName = reinterpret_cast<const char *>( pMedia ) + 0x14;

    CinematicActor *pExisting = findActor( pName );
    if ( pExisting != nullptr ) {
        return pExisting;
    }

    const uint8_t *pRaw = reinterpret_cast<const uint8_t *>( pMedia );
    const uint32_t hashLow = *reinterpret_cast<const uint32_t *>( pRaw + 0x34 );
    const uint32_t hashHigh =
        *reinterpret_cast<const uint32_t *>( pRaw + 0x38 );
    const uint64_t key = ( static_cast<uint64_t>( hashHigh ) << 32 ) | hashLow;

    CinematicActor::eKind kind = CinematicActor::CAK_Entity;
    void *pEntity = findActorByHashcode( key );
    if ( pEntity == nullptr ) {
        pEntity = spawnActorEntity( pName, key, kind );
    }

    CarState *pPlayerCar = nullptr;
    if ( kind == CinematicActor::CAK_PlayerCar ||
         kind == CinematicActor::CAK_TrafficCar ) {
        pPlayerCar = static_cast<CarState *>( pEntity );
        pEntity = nullptr;
    }

    GMIntro::IntroCarSlot *pIntroCarSlot = nullptr;
    if ( kind == CinematicActor::CAK_IntroCar ) {
        pIntroCarSlot = static_cast<GMIntro::IntroCarSlot *>( pEntity );
        pPlayerCar = pIntroCarSlot->pCarState;
        pEntity = nullptr;
    }

    int32_t avatarRefCount = 0;
    if ( pEntity != nullptr && kind == CinematicActor::CAK_Entity ) {
        Entity *pSpawned = static_cast<Entity *>( pEntity );
        applySpawnTail( pSpawned );
        if ( Entity::IsAvatarEntityType( pSpawned ) && gpScenaric != nullptr ) {
            // FUN_007f5780
            avatarRefCount = gpScenaric->getAvatarManager().getReferenceCount(
                static_cast<EntityAvatar *>( pSpawned ) );
        }
    }

    if ( bIsLifeSetCinematic && pEntity != nullptr &&
         kind == CinematicActor::CAK_Entity &&
         Entity::IsAvatarEntityType( static_cast<Entity *>( pEntity ) ) ) {
        registerLifeSetActor( key );
    }

    CinematicActor *pActor = registerActor( pName, pMedia, pEntity, kind );
    if ( pActor != nullptr ) {
        pActor->AvatarRefCount = avatarRefCount;
        pActor->Hashcode = key;
        pActor->pCarState = pPlayerCar;
        if ( pIntroCarSlot != nullptr && bBindIntroCarActor ) {
            pIntroCarSlot->pActor = pActor;
            pActor->pIntroCarSlot = pIntroCarSlot;
        }
    }
    bBindIntroCarActor = false;
    return pActor;
}

static void SetSkinnedPropCullRadius( EntityActor *pActor ) {
    // FUN_007e6cb0 (inlined)
    const SerializedSkinnedModel *pModel =
        static_cast<const SerializedSkinnedModel *>(
            pActor->getGeometryData() );
    if ( pModel == nullptr || pModel->pInstance == nullptr ) {
        return;
    }

    const InstanceLOD &lod = pModel->pInstance->getLOD( 0 );
    if ( lod.pHashcode == nullptr || lod.pHashcode->pEntry == nullptr ||
         !pActor->hasSkeleton() ) {
        return;
    }

    RenderFile::Section *pSection = lod.pHashcode->pEntry;
    RenderObject *pObject =
        (RenderObject *)( (uint8_t *)pSection + sizeof( *pSection ) );
    pObject->BoundingSphere.w() = kSkinnedPropCullRadius;
}

void CinResCollection::applySpawnTail( Entity *pEntity ) {
    // FUN_007e6cb0
    if ( pEntity->getType() == eEntityType::ET__ACT ) {
        EntityActor *pActor = static_cast<EntityActor *>( pEntity );
        pActor->setMustProjectOnHMap( false );
        pActor->relight();
        SetSkinnedPropCullRadius( pActor );
    }

    if ( Entity::IsAvatarEntityType( pEntity ) ) {
        static_cast<EntityAvatar *>( pEntity )->relight();
    }
}

CinematicActor *CinResCollection::findActor( const char *pName ) {
    // FUN_007e0af0
    if ( pName == nullptr ) {
        return nullptr;
    }
    for ( int32_t i = 0; i < numActors; i++ ) {
        if ( std::strcmp( actors[i].Name, pName ) == 0 ) {
            return &actors[i];
        }
    }
    return nullptr;
}

void *CinResCollection::findActorEntity( const char *pName ) {
    CinematicActor *pActor = findActor( pName );
    return ( pActor != nullptr ) ? pActor->pEntity : nullptr;
}

bool CinResCollection::areActorsValid() const {
    // FUN_007e0a00
    for ( int32_t i = 0; i < numActors; i++ ) {
        const CinematicActor &actor = actors[i];
        if ( actor.Kind != CinematicActor::CAK_Entity ||
             actor.pEntity == nullptr ) {
            continue;
        }
        if ( !static_cast<const Entity *>( actor.pEntity )->isValid() ) {
            return false;
        }
    }
    return true;
}

void CinResCollection::setVeryFarScene( const bool bEnabled ) {
    if ( bEnabled ) {
        zConfigVeryFar = { 100.0f, 40000.0f };
    }
    bVeryFarScene = bEnabled;
    applyZConfig( { 0.1f, -1.0f } );
}

CinematicActor *CinResCollection::registerActor( const char *pName,
                                                 const void *pSceneData,
                                                 void *pEntity,
                                                 CinematicActor::eKind kind ) {
    // FUN_007e0b70 + FUN_007e0c90
    if ( pName == nullptr || numActors >= kMaxActors ) {
        return nullptr;
    }
    CinematicActor &actor = actors[numActors];
    std::strncpy( actor.Name, pName, sizeof( actor.Name ) - 1 );
    actor.Name[sizeof( actor.Name ) - 1] = '\0';
    actor.pSceneData = pSceneData;
    actor.pEntity = pEntity;
    actor.Kind = kind;
    actor.bInstanced = false;
    actor.AvatarRefCount = 0;
    numActors++;
    return &actor;
}

void *CinResCollection::findActorByHashcode( uint64_t key ) {
    // FUN_007e5360
    ScenaricBank *pSCBank = ( pOwner != nullptr ) ? pOwner->getBank() : nullptr;
    if ( pSCBank == nullptr || key == 0 ) {
        return nullptr;
    }

    // FUN_00425c10
    Entity *pEntity = pSCBank->getEntityByHashcode( key );
    if ( pEntity == nullptr ) {
        return nullptr;
    }

    const eEntityType type = pEntity->getType();

    if ( type == eEntityType::ET_AVPL ) {
        EntityDriver *pDriver = static_cast<EntityDriver *>( pEntity );
        pDriver->leaveSeatForCinematic();
        numAvatars++;

        EntityCinematic *pLifeCinematic =
            ( gpActiveGMScene != nullptr )
                ? gpActiveGMScene->getLifeSetManager().findActorCinematic( key )
                : nullptr;
        if ( pLifeCinematic != nullptr ) {
            pLifeCinematic->stop( true );
        }
        return pEntity;
    }
    if ( type == eEntityType::ET_CPNJ ) {
        return spawnPnjAvatar(
            static_cast<EntityPnj *>( pEntity )->getPnjName(), key );
    }
    if ( type == eEntityType::ET_POCA ) {
        return nullptr;
    }

    return pEntity;
}

void *CinResCollection::spawnActorEntity( const char *pName,
                                          uint64_t hashcode,
                                          CinematicActor::eKind &outKind ) {
    // FUN_007e6cb0
    outKind = CinematicActor::CAK_Entity;

    if ( pName != nullptr ) {
        if ( std::strncmp( pName, "Pietonne", 8 ) == 0 ) {
            return spawnPedestrian( 1 );
        }
        if ( std::strncmp( pName, "Pieton", 6 ) == 0 ) {
            return spawnPedestrian( 0 );
        }
        if ( std::strncmp( pName, "Enfant", 6 ) == 0 ) {
            return spawnPedestrian( 0 );
        }
        if ( std::strcmp( pName, "Avatar" ) == 0 ) {
            return spawnAvatar( eEntityType::ET_AVPL, ~0ull, pName );
        }
        if ( std::strstr( pName, "Avatar_" ) != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_007e3d60)
            return nullptr;
        }
        if ( std::strncmp( pName, "Hostess", 7 ) == 0 ||
             std::strstr( pName, "PoliceWoman" ) != nullptr ) {
            return spawnAvatar( eEntityType::ET_AVAT, hashcode, pName );
        }
        if ( std::strstr( pName, "AvatarM_" ) != nullptr ||
             std::strstr( pName, "AvatarF_" ) != nullptr ) {
            if ( std::strstr( pName, "_Intro" ) != nullptr &&
                 gpGMIntro != nullptr ) {
                int32_t introAvatarIndex = 0;
                if ( std::strstr( pName, "_IntroMoto" ) != nullptr ) {
                    introAvatarIndex = 1;
                } else if ( std::strstr( pName, "_IntroSG" ) != nullptr ) {
                    introAvatarIndex = 2;
                }

                EntityAvatar *pIntroAvatar =
                    gpGMIntro->addIntroAvatarReference( introAvatarIndex );
                if ( pIntroAvatar != nullptr ) {
                    OTDU_UNIMPLEMENTED_ONCE; // TODO: GM_INTRO (driver setup,
                                             // FUN_007ebe50 / FUN_007ebe80)
                }
                return pIntroAvatar;
            }

            char avatarName[128];
            strncpy( avatarName, pName, sizeof( avatarName ) - 1 );
            avatarName[sizeof( avatarName ) - 1] = '\0';

            AvatarConfiguration styleConfig;
            const AvatarConfiguration *pStyleConfig = nullptr;
            if ( gpAvatar->loadStyleConfiguration( styleConfig, avatarName ) ) {
                pStyleConfig = &styleConfig;
            }
            return spawnNamedAvatar( pStyleConfig,
                                     GetIdentifier64bit( avatarName ), false );
        }
        if ( std::strncmp( pName, "Seller", 6 ) == 0 ) {
            return spawnAvatar( eEntityType::ET_AVAT, hashcode, pName );
        }

        if ( std::strncmp( pName, "glass", 6 ) == 0 ) {
            return findSceneActorByName( "glass" );
        }
        if ( std::strncmp( pName, "remote", 7 ) == 0 ) {
            return findSceneActorByName( "remote" );
        }
        if ( std::strncmp( pName, "phone", 6 ) == 0 ) {
            return findSceneActorByName( "telephone" );
        }

        if ( std::strncmp( "PLAYER_CAR", pName, 10 ) == 0 ) {
            void *pCar = spawnPlayerCar();
            if ( pCar != nullptr ) {
                outKind = CinematicActor::CAK_PlayerCar;
            }
            return pCar;
        }

        if ( std::strncmp( "TRAFFICCAR", pName, 10 ) == 0 ) {
            void *pCar = spawnTrafficCar( pName );
            if ( pCar != nullptr ) {
                outKind = CinematicActor::CAK_TrafficCar;
            }
            return pCar;
        }

        if ( std::strstr( pName, "_D_CAR" ) != nullptr ) {
            if ( gpGMIntro == nullptr ) {
                return nullptr;
            }
            GMIntro::IntroCarSlot *pSlot =
                gpGMIntro->acquireIntroCar( pName, bBindIntroCarActor );
            if ( pSlot != nullptr ) {
                outKind = CinematicActor::CAK_IntroCar;
            }
            return pSlot;
        }

        if ( ( std::strstr( pName, "_Fork_" ) != nullptr ||
               std::strstr( pName, "_Disc_" ) != nullptr ) &&
             gpGMIntro != nullptr ) {
            GMIntro::IntroCarPart *pPart =
                gpGMIntro->allocateIntroCarPart( pName );
            if ( pPart != nullptr ) {
                outKind = CinematicActor::CAK_IntroCarPart;
            }
            return pPart;
        }

        if ( std::strstr( pName, "PLANE" ) != nullptr ) {
            if ( pPlane == nullptr ) {
                return nullptr;
            }
            pPlane->setNeedRegisterEntity( false );
            pPlane->setMustProjectOnHMap( true );
            pPlane->setMustProjectOnRoad( false );
            outKind = CinematicActor::CAK_Plane;
            return static_cast<Entity *>( pPlane );
        }
    }

    return nullptr;
}

void *CinResCollection::findSceneActorByName( const char *pName ) {
    // FUN_007e4050
    if ( pName == nullptr || gpActiveGMScene == nullptr ) {
        return nullptr;
    }

    MngActor &sceneActors = gpActiveGMScene->getActorManager();
    const int32_t numSceneActors = sceneActors.getActorCount();
    for ( int32_t i = 0; i < numSceneActors; i++ ) {
        Entity *pActor = sceneActors.getActor( i );
        if ( pActor != nullptr &&
             std::strcmp( pActor->getInstanceName().c_str(), pName ) == 0 ) {
            return pActor;
        }
    }

    return nullptr;
}

void *CinResCollection::spawnTrafficCar( const char *pName ) {
    // FUN_007e6cb0
    static constexpr int32_t kDefaultTrafficCarID = 0x7da;
    static constexpr int32_t kCinematicLogoVariant = 1; // SWITCH2

    char name[0x32] = {};
    std::strncpy( name, pName, sizeof( name ) - 1 );

    int32_t carID = kDefaultTrafficCarID;
    const char *pSeparator = std::strrchr( name, '_' );
    if ( pSeparator != nullptr ) {
        const int32_t parsedID = std::atoi( pSeparator + 1 );
        if ( parsedID != 0 ) {
            carID = parsedID;
        }
    }

    VehicleBaseResources *pResource =
        gpAI->getAIPool()->getTraffic().getTemplateResource( carID );
    if ( pResource == nullptr || gpCarState == nullptr ) {
        return nullptr;
    }

    VehicleSpawnParams params;
    params.bCinematicMode = true;
    params.PhantomMode = 1;
    CarState *pCarState =
        gpCarState->installVehicle( pResource, params, VIT_Traffic, -1 );
    if ( pCarState == nullptr ) {
        return nullptr;
    }

    VehicleBase *pVehicle = pCarState->getVehicleInstance();
    if ( pVehicle != nullptr &&
         pVehicle->getType() == VehicleBase::eType::VBT_TrafficCar ) {
        static_cast<VehicleTraffic *>( pVehicle )
            ->setLogoVariant( kCinematicLogoVariant );
    }

    return pCarState;
}

void *CinResCollection::spawnPlayerCar() {
    // FUN_007e6520
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        const int32_t tutorialStep = gpScenaric->getIntroTutorial().getStep();
        if ( tutorialStep != kTutorialStatePending &&
             tutorialStep != kTutorialStateRunning &&
             tutorialStep < kTutorialStateBuyCar ) {
            return nullptr;
        }

        GarageSlot *pSlot = GetCurrentGarageSlot();
        if ( pSlot == nullptr ) {
            return nullptr;
        }

        const Eigen::Vector4f shift = ( gpWorldStreaming->isEnabled() )
                                          ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();

        VehicleSpawnParams params;
        params.Transform.row( 3 ) << shift.x(), shift.y(), shift.z(), 1.0f;
        params.bSetPosition = true;
        params.bCastToGround = true;
        params.bResetCar = true;
        params.bCinematicMode = true;
        params.PhantomMode = 1;

        VehicleBaseResources *pResource = gpCarState->loadVehicleResources(
            int32_t( pSlot->getCarConfigID() ), 0, pSlot->rimID,
            pSlot->packHashcode, 0, 0, eVehicleResourceType::VRT_Exterior );
        pLocalCar = ( pResource != nullptr )
                        ? gpCarState->installVehicle( pResource, params,
                                                      VIT_Player, -1 )
                        : nullptr;
        if ( pLocalCar == nullptr ) {
            return nullptr;
        }

        PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
        if ( pPhysicsVehicle != nullptr ) {
            PhysicsLock lock;
            pPhysicsVehicle->resetToTransform( params.Transform, true );
        }

        gpScenaric->getLocalPlayer().setCarState( pLocalCar );
        VehicleBase *pVehicle = pLocalCar->getVehicleInstance();
        if ( pVehicle != nullptr ) {
            pVehicle->applyGaragePaint( *pSlot );
        }
        bPlayerCarSpawned = true;

        pLocalCar->getBody()->WorldMatrix.row( 3 ) = shift;

        if ( pVehicle != nullptr &&
             ( pVehicle->getType() == VehicleBase::eType::VBT_Car ||
               pVehicle->getType() == VehicleBase::eType::VBT_Bike ) ) {
            GSCarDisplay::SetRealTimeEnvMappedCar( pVehicle );
        } else {
            GSCarDisplay::SetRealTimeEnvMappedCar( nullptr );
        }
    }

    if ( pPlayerAvatar != nullptr && pLocalCar->getDriver() == nullptr ) {
        gpScenaric->placeDriver( pLocalCar, pPlayerAvatar->getDriver() );
    }

    return pLocalCar;
}

void CinResCollection::releasePlayerCarActor( const int32_t index ) {
    // FUN_007e5f70
    CarState *pCarState = actors[index].pCarState;
    if ( pCarState == nullptr || actors[index].pSceneData == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI
    SetPlayerCarCinematicMode( false );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS

    if ( gpActiveGMScene != nullptr ) {
        MngShowCaseCarControl &showcase =
            gpActiveGMScene->getShowcaseCarControl();
        showcase.resetShowcase();
        showcase.setVehicle( nullptr );
    }

    if ( !bPlayerCarSpawned ) {
        return;
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        VehicleBase *pVehicle = pLocalCar->getVehicleInstance();
        if ( pVehicle != nullptr ) {
            pVehicle->unloadInterior();
        }
        gpScenaric->getLocalPlayer().setCarState( nullptr );
        gpCarState->unloadVehicle( pLocalCar, false, false );
    }
    bPlayerCarSpawned = false;
}

static void SnapCinematicWindow( WindowState &window, bool bOpening ) {
    window.ElapsedTime = 1.0f;
    window.MoveDuration = 1.0f;
    window.bIsOpening = bOpening;
    window.bPaused = false;
}

static void MoveCinematicWindow( WindowState &window,
                                 bool bOpening,
                                 bool bIntro,
                                 float duration ) {
    if ( duration == 0.0f ) {
        SnapCinematicWindow( window, bOpening );
        return;
    }

    float elapsed = 0.0f;
    if ( bIntro ) {
        const float scaled =
            ( window.ElapsedTime * duration ) / window.MoveDuration;
        elapsed =
            ( window.bIsOpening == bOpening ) ? scaled : duration - scaled;
    }
    window.ElapsedTime = elapsed;
    window.MoveDuration = duration;
    window.bIsOpening = bOpening;
    window.bPaused = false;
}

static void SnapCinematicRightDoor( MngShowCaseCarControl &control,
                                    bool bOpening ) {
    if ( control.isBikeCategory() ) {
        return;
    }
    DoorState &door = control.getRightDoor();
    door.bIsOpening = bOpening;
    door.ElapsedTime = 1.0f;
    door.MoveDuration = 1.0f;
}

void CinResCollection::initializePlayerCarForCinematic() {
    // FUN_007e4b90
    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pCarState == nullptr || pCarState->getPhysicsVehicle() == nullptr ) {
        return;
    }

    SetPlayerCarCinematicMode( true );
    {
        PhysicsLock lock;
        pCarState->getPhysicsVehicle()->resetSimulationState();
    }
    pCarState->getBody()->WorldMatrix( 3, 3 ) = 1.0f;

    if ( gpActiveGMScene == nullptr ) {
        return;
    }

    MngShowCaseCarControl &control = gpActiveGMScene->getShowcaseCarControl();
    control.setVehicle( pCarState->getVehicleInstance() );
    if ( !bMustCloseWindowsAndDoors ) {
        control.resetShowcase();
    } else {
        control.closeDoorsAndWindows();
    }
}

bool CinResCollection::onPlayerCarMessage( CarState *pCarState,
                                           const char *pMessage ) {
    // FUN_007e26a0
    if ( pCarState == nullptr ) {
        return false;
    }
    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();

    const FlashMessage message( pMessage );
    if ( message == FlashMessage( "moof" ) ||
         message == FlashMessage( "moon" ) ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS
        return true;
    }
    if ( message == FlashMessage( "ctci" ) ) {
        if ( pVehicle != nullptr ) {
            SetPlayerCarCinematicMode( true );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AI / PHYSICS
        }
        return true;
    }
    if ( message == FlashMessage( "ctia" ) ) {
        if ( pVehicle != nullptr ) {
            SetPlayerCarCinematicMode( false );
            bPlayerCarTeleport = true;
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
        }
        return true;
    }
    if ( message == FlashMessage( "rspo" ) ) {
        bPlayerCarTeleport = true;
        return true;
    }

    DBEntryCarPhysicsData *pCarData =
        ( gpDatabase != nullptr )
            ? gpDatabase->getCarDBEntry( pCarState->getCarID() )
            : nullptr;
    if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
        return false;
    }
    if ( gpActiveGMScene == nullptr ) {
        return false;
    }

    const bool bIntro = gpGMIntro != nullptr;
    if ( bIntro ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GM_INTRO (IntroCarSlot showcase
                                 // control, car actor +0xb0)
    }
    MngShowCaseCarControl &control = gpActiveGMScene->getShowcaseCarControl();

    float duration = 0.0f;
    sscanf( pMessage + 5, "%f", &duration );

    const bool bOpenLeftDoor = message == FlashMessage( "opdl" );
    const bool bOpenRightDoor = message == FlashMessage( "opdr" );
    const bool bOpenDoors = message == FlashMessage( "opda" );
    const bool bCloseLeftDoor = message == FlashMessage( "cldl" );
    const bool bCloseRightDoor = message == FlashMessage( "cldr" );
    const bool bCloseDoors = message == FlashMessage( "clda" );

    if ( bOpenDoors || bOpenLeftDoor || bOpenRightDoor || bCloseDoors ||
         bCloseLeftDoor || bCloseRightDoor ) {
        const bool bOpening = bOpenDoors || bOpenLeftDoor || bOpenRightDoor;
        const bool bLeft = !( bOpenRightDoor || bCloseRightDoor );
        const bool bRight = !( bOpenLeftDoor || bCloseLeftDoor );

        if ( !bIntro ) {
            if ( bLeft ) {
                control.moveDoor( true, !bOpening, false, 0.0f );
            }
            if ( bRight ) {
                SnapCinematicRightDoor( control, !bOpening );
            }
        }
        if ( bLeft ) {
            control.moveDoor( true, bOpening, bIntro, duration );
        }
        if ( bRight ) {
            control.moveDoor( false, bOpening, bIntro, duration );
        }
        return true;
    }

    const bool bOpenLeftWindow = message == FlashMessage( "opwl" );
    const bool bOpenRightWindow = message == FlashMessage( "opwr" );
    const bool bOpenWindows = message == FlashMessage( "opwa" );
    const bool bCloseLeftWindow = message == FlashMessage( "clwl" );
    const bool bCloseRightWindow = message == FlashMessage( "clwr" );
    const bool bCloseWindows = message == FlashMessage( "clwa" );

    if ( !( bOpenWindows || bOpenLeftWindow || bOpenRightWindow ||
            bCloseWindows || bCloseLeftWindow || bCloseRightWindow ) ) {
        return false;
    }

    const bool bOpening = bOpenWindows || bOpenLeftWindow || bOpenRightWindow;
    const bool bLeft = !( bOpenRightWindow || bCloseRightWindow );
    const bool bRight = !( bOpenLeftWindow || bCloseLeftWindow );
    if ( !control.hasWindows() ) {
        return true;
    }

    if ( !bIntro ) {
        if ( bLeft ) {
            SnapCinematicWindow( control.getLeftWindow(), !bOpening );
        }
        if ( bRight ) {
            SnapCinematicWindow( control.getRightWindow(), !bOpening );
        }
    }
    if ( bLeft ) {
        MoveCinematicWindow( control.getLeftWindow(), bOpening, bIntro,
                             duration );
    }
    if ( bRight ) {
        MoveCinematicWindow( control.getRightWindow(), bOpening, bIntro,
                             duration );
    }
    return true;
}

void CinResCollection::applyCarTransform( CarState *pCarState,
                                          const Matrix4x4f_t &transform,
                                          bool &bTeleport,
                                          const Matrix4x4f_t *pForkMatrices,
                                          const Matrix4x4f_t *pDiscMatrices ) {
    if ( pCarState == nullptr ) {
        return;
    }

    Matrix4x4f_t world = transform;
    if ( gpWorldStreaming != nullptr ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        world( 3, 0 ) += shift.x();
        world( 3, 1 ) += shift.y();
        world( 3, 2 ) += shift.z();
    }

    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    if ( pVehicle != nullptr && gpPhysics != nullptr ) {
        PhysicsLock lock;
        if ( !pVehicle->isKeyframed() ) {
            pVehicle->setKeyframed( true, true );
            bTeleport = true;
        }
        pVehicle->moveKeyframed( world, lastDeltaTime, bTeleport, pForkMatrices,
                                 pDiscMatrices );
        bTeleport = false;
    }
}

void *CinResCollection::spawnPedestrian( int32_t gender ) {
    // FUN_008518b0
    if ( gpScenaric == nullptr ) {
        return nullptr;
    }

    EntityPedestrian *pTemplate = static_cast<EntityPedestrian *>(
        gpScenaric->getPedestrianBank().getFirst( eEntityType::ET_PTDR ) );
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    AvatarCommonBnk &bank = gpPedestrians->getCharacterBank();
    const int32_t variantId =
        ( gender >= 0 && gender <= 1 && bank.getNumBody3DD( gender ) != 0 )
            ? bank.getVariantId( gender, 0 )
            : 0;
    pTemplate->setType( gender );
    pTemplate->setSubType( variantId );
    return pTemplate;
}

void ReadAvatarConfigFromPNJ( AvatarConfiguration &outConfig,
                              const char *pPNJName ) {
    // FUN_007e40d0
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numPNJ =
        static_cast<int32_t>( gameDB.pNumItems[kPNJDatabaseIndex] );

    for ( int32_t i = 0; i < numPNJ; i++ ) {
        const DBEntryPNJ &pnjEntry = gameDB.DBPNJ[i];
        if ( std::strcmp( pnjEntry.ConfigID.pAsString, pPNJName ) != 0 ) {
            continue;
        }

        if ( gpAvatar->applyPNJConfiguration( outConfig, pnjEntry ) ) {
            return;
        }

        break;
    }

    outConfig = gpAvatar->getDefaultConfiguration( 0 );
}

void *CinResCollection::spawnNamedAvatar( const AvatarConfiguration *pConfig,
                                          uint64_t hashcode,
                                          bool bBikeClothes ) {
    // FUN_007e5280
    if ( gpScenaric == nullptr || gpAvatar == nullptr ) {
        return nullptr;
    }

    AvatarConfiguration hostessConfig;
    if ( pConfig == nullptr ) {
        ReadAvatarConfigFromPNJ( hostessConfig, "hostess" );
        pConfig = &hostessConfig;
    }

    EntityAvatar *pAvatar =
        gpScenaric->getAvatarManager().allocateAvatar( hashcode, 0 );
    if ( pAvatar != nullptr ) {
        pAvatar->installConfiguration( pConfig, bBikeClothes ? 1 : 0,
                                       bAsyncActorLoad, false );
        if ( pAvatar->getType() == eEntityType::ET_AVPL ) {
            pAvatar->resetCineAvatarMode();
        }
        numAvatars++;
    }
    return pAvatar;
}

void *CinResCollection::spawnPnjAvatar( const char *pPnjName,
                                        uint64_t hashcode ) {
    // FUN_007e5160
    if ( gpScenaric == nullptr || gpAvatar == nullptr ) {
        return nullptr;
    }

    AvatarConfiguration config;
    ReadAvatarConfigFromPNJ( config, pPnjName );

    EntityAvatar *pAvatar = nullptr;
    if ( numAvatars < kMaxCinematicAvatars ) {
        pAvatar = gpScenaric->getAvatarManager().allocateAvatar( hashcode, 0 );
        if ( pAvatar != nullptr ) {
            pAvatar->installConfiguration( &config, 0, bAsyncActorLoad, false );
            numHostesses++;
            numAvatars++;
        }
    }

    EntityCinematic *pLifeCinematic =
        ( gpActiveGMScene != nullptr )
            ? gpActiveGMScene->getLifeSetManager().findActorCinematic(
                  hashcode )
            : nullptr;
    if ( pLifeCinematic != nullptr ) {
        pLifeCinematic->stop( true );
    }

    if ( pAvatar != nullptr && pAvatar->getType() == eEntityType::ET_AVPL ) {
        pAvatar->resetCineAvatarMode();
    }
    return pAvatar;
}

void CinResCollection::registerLifeSetActor( uint64_t hashcode ) {
    // FUN_007e3de0
    if ( gpActiveGMScene == nullptr ) {
        return;
    }
    MngLifeSet &lifeSet = gpActiveGMScene->getLifeSetManager();
    if ( lifeSet.findActorCinematic( hashcode ) == nullptr ) {
        lifeSet.addActorCinematic( hashcode, pOwner );
    }
}

void *CinResCollection::spawnAvatar( eEntityType type,
                                     uint64_t hashcode,
                                     const char *pName ) {
    // FUN_007e5010 / FUN_007e54f0
    if ( gpScenaric == nullptr || gpAvatar == nullptr ) {
        return nullptr;
    }

    AvatarManager &avatarManager = gpScenaric->getAvatarManager();

    if ( type == eEntityType::ET_AVPL ) {
        // FUN_007e54f0
        bPlayerAvatarSpawned = true;

        int32_t clotheSet = 0;
        GarageSlot *pSlot = GetCurrentGarageSlot();
        if ( pSlot != nullptr && pSlot->isValid() &&
             gpActiveGMScene != nullptr &&
             !gpActiveGMScene->isStaticFrontEndScene() &&
             gpDatabase != nullptr ) {
            DBEntryCarPhysicsData *pCarData = gpDatabase->getCarDBEntry(
                static_cast<int32_t>( pSlot->getCarConfigID() ) );
            if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
                clotheSet = 1;
            }
        }

        EntityAvatar *pAvatar = avatarManager.allocateAvatar( ~0ull, 1 );
        if ( pAvatar != nullptr ) {
            pAvatar->installConfiguration(
                TestDrive::GetSavedAvatarConfiguration(), clotheSet,
                bAsyncActorLoad, false );
            pAvatar->addInstanceReference();
        }
        pPlayerAvatar = pAvatar;

        return pAvatar;
    }

    const bool bIsPoliceWoman =
        pName != nullptr && std::strstr( pName, "PoliceWoman" ) != nullptr;

    AvatarConfiguration config;
    ReadAvatarConfigFromPNJ( config,
                             bIsPoliceWoman ? "cop_man" : "shop_lamborghini" );

    return avatarManager.createAvatar( &config, 0, false, hashcode, false, 0 );
}
