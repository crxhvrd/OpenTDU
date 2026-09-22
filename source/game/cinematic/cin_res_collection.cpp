#include "shared.h"
#include "cin_res_collection.h"

#include "tdu_instance.h"

#include "game/cinematic/cinematic_music_emitter.h"

#include <algorithm>
#include <cstring>

#include "core/maths_helper.h"
#include "core/hash/crc64.h"
#include "core/random/bcpl.h"
#include "filesystem/bank.h"
#include "game/scenaric/entity/entity_actor.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_pedestrian.h"
#include "database/gs_database.h"
#include "database/db_aftermarket_pack.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "game/gm_scene.h"
#include "game/vehicle/gs_car_state.h"
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
#include "render/camera/gs_camera.h"
#include "render/camera/camera_cinematic.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "world/gs_world_streaming.h"

// TDU_PNJ row index in the game database
static constexpr int32_t kPNJDatabaseIndex = 0x10;
static constexpr int32_t kDefaultCarDBID = 999;
static constexpr int32_t kDefaultBikeDBID = 666;

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

void CinResCollection::releasePlayerCarActors() {
    for ( int32_t i = 0; i < numActors; i++ ) {
        if ( actors[i].Kind == CinematicActor::CAK_PlayerCar ) {
            SetPlayerCarCinematicMode( false );
            return;
        }
    }
}

void CinResCollection::reset() {
    releasePlayerCarActors();
    releaseCinematicCamera();
    pCinematicData = nullptr;
    pRefData = nullptr;
    pBuffer = nullptr;
    pSourceTable = nullptr;
    pRefTable = nullptr;
    pBank = nullptr;
    numActors = 0;
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

    const void *pKeyframeEntry = pValues + lower * 2;
    if ( isMessageFired( pKeyframeEntry ) ) {
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

    recordFiredMessage( pMsg, pKeyframeEntry );

    const uint32_t tag4 =
        ( static_cast<uint32_t>( static_cast<uint8_t>( pMsg[0] ) ) << 24 ) |
        ( static_cast<uint32_t>( static_cast<uint8_t>( pMsg[1] ) ) << 16 ) |
        ( static_cast<uint32_t>( static_cast<uint8_t>( pMsg[2] ) ) << 8 ) |
        static_cast<uint32_t>( static_cast<uint8_t>( pMsg[3] ) );

    OTDU_LOG_DEBUG( "Cinematic '%s' message '%s' at %.2fs (%.2fs)\n", debugName,
                    pMsg, time, duration );

    processMusicMessage( tag4 );
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

void CinResCollection::tick( float time, float /*deltaTime*/ ) {
    // FUN_0067b560
    if ( pCinematicData == nullptr ) {
        return;
    }

    // Clamp `time` to the cinematic duration (the original biases the upper
    // bound by a small epsilon) and to zero.
    const float totalTime = pCinematicData->TotalLength[0];
    if ( time > totalTime ) {
        time = totalTime;
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

    const int32_t sourceIdx = static_cast<int32_t>( kf[1] );
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

    const CinematicActor *pActor =
        static_cast<const CinematicActor *>( *pCtx->pHandleSlot );
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

void CinResCollection::dispatchActor( const CinematicActor *pActor,
                                      CinActorState &state,
                                      const Eigen::Vector3f &scale,
                                      bool bHasTransformTrack ) {
    // FUN_007e3e30
    if ( pActor == nullptr ) {
        return;
    }
    if ( pActor->Kind != CinematicActor::CAK_PlayerCar &&
         pActor->pEntity == nullptr ) {
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

    Matrix4x4f_t transform = BuildRotationMatrix( state.Heading );
    transform.row( 0 ) *= scale.x();
    transform.row( 1 ) *= scale.y();
    transform.row( 2 ) *= scale.z();
    transform.row( 3 ) = Eigen::Vector4f(
        state.Position.x(), state.Position.y(), state.Position.z(), 1.0f );
    if ( bHasBaseMatrix ) {
        transform = transform * baseMatrix;
    }

    if ( pActor->Kind == CinematicActor::CAK_PlayerCar ) {
        if ( state.NumMessages > 0 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC (the PLAYER_CAR media's
                                     // GesCineMsg: doors, windows, avm*)
        }
        if ( bHasTransformTrack ) {
            applyPlayerCarTransform( transform );
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
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC

        if ( pOwner != nullptr ) {
            for ( const uint32_t tag : kCineAvatarModeMessages ) {
                removeFiredMessages( tag );
            }
            if ( gpTestDriveInstance != nullptr &&
                 gpTestDriveInstance->getActiveGameMode() == GM_Intro ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: GM_INTRO (gpGMIntro also
                                         // purges the cl/op door and window
                                         // messages)
            }
        }

        if ( fadeInDuration > 0.0f ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC (shot fade-in through
                                     // MngFade)
        }
    }

    if ( duration - fadeOutDuration < shotTime && fadeOutDuration > 0.0f ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC (shot fade-out through
                                 // MngFade)
    }
}

bool CinResCollection::processMusicMessage( uint32_t tag4 ) {
    // FUN_007e1dc0 (inlined)
    static constexpr uint32_t kMessagePlay = 0x706c6179;     // "play"
    static constexpr uint32_t kMessagePlayLoop = 0x706c6c6f; // "pllo"
    static constexpr uint32_t kMessagePlayOnce = 0x706c6f6e; // "plon"
    static constexpr uint32_t kMessageStop = 0x73746f70;     // "stop"

    if ( pMusicEmitter == nullptr ) {
        return false;
    }

    switch ( tag4 ) {
    case kMessagePlay:
    case kMessagePlayOnce:
        pMusicEmitter->play( false, 0.0f );
        return true;

    case kMessagePlayLoop:
        pMusicEmitter->play( true, 0.0f );
        return true;

    case kMessageStop:
        pMusicEmitter->stop( 0.0f );
        return true;

    default:
        return false;
    }
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

bool CinResCollection::resolveRefMatrix( const CinMediaContext *pCtx,
                                         int32_t refIdx,
                                         Matrix4x4f_t &outMatrix ) const {
    if ( pCtx == nullptr || pCtx->pSource == nullptr ||
         pCtx->pSource->pSceneRefs == nullptr || refIdx < 0 ) {
        return false;
    }

    const CinRefSlot &slot = pCtx->pSource->pSceneRefs[refIdx];
    const uint8_t *pDesc = static_cast<const uint8_t *>( slot.pDescriptor );
    if ( pDesc == nullptr || pDesc[0] != 0x0c || slot.pHandle == nullptr ) {
        return false;
    }

    const CinematicActor *pActor =
        static_cast<const CinematicActor *>( slot.pHandle );
    if ( pActor->Kind == CinematicActor::CAK_PlayerCar ) {
        CarState *pCarState = ( gpScenaric != nullptr )
                                  ? gpScenaric->getLocalPlayer().getCarState()
                                  : nullptr;
        if ( pCarState == nullptr ) {
            return false;
        }
        outMatrix = pCarState->getBody()->OffsetedWorldMatrix;
        return true;
    }

    Entity *pEntity = static_cast<Entity *>( pActor->pEntity );
    if ( pEntity == nullptr ) {
        return false;
    }
    outMatrix = pEntity->getModelMatrix();
    return true;
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
        Matrix4x4f_t refMatrix = Matrix4x4f_t::Identity();

        Eigen::Vector3f worldPos = position;
        if ( resolveRefMatrix( pCtx, posRef, refMatrix ) ) {
            worldPos = TDUMat4x4MulVec( refMatrix, position );
        } else if ( bHasBaseMatrix ) {
            worldPos = TDUMat4x4MulVec( baseMatrix, position );
        }

        Eigen::Vector3f worldTarget = target;
        Matrix4x4f_t targetMatrix = Matrix4x4f_t::Identity();
        const bool bTargetRelative =
            resolveRefMatrix( pCtx, targetRef, targetMatrix );
        if ( bTargetRelative ) {
            worldTarget = TDUMat4x4MulVec( targetMatrix, target );
        } else if ( bHasBaseMatrix ) {
            worldTarget = TDUMat4x4MulVec( baseMatrix, target );
        }

        if ( ( worldTarget - worldPos ).squaredNorm() < 1.0e-6f ) {
            return;
        }

        Eigen::Vector3f worldUp = up;
        if ( worldUp.squaredNorm() < 1.0e-6f ) {
            worldUp = Eigen::Vector3f( 0.0f, 1.0f, 0.0f );
        } else if ( bTargetRelative || posRef >= 0 ) {
            const Matrix4x4f_t &m = ( posRef >= 0 ) ? refMatrix : targetMatrix;
            worldUp =
                ( m.topLeftCorner<3, 3>().transpose() * worldUp ).normalized();
        }

        cameraView.Position = worldPos;
        cameraView.Target = worldTarget;
        cameraView.Up = worldUp;
        cameraView.Fov = fov;
        cameraView.bValid = true;

        applyViewToCinematicCamera( pCamera, time, false );
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
    *pOutSize = 0;
    if ( pBank == nullptr || pName == nullptr ) {
        return nullptr;
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

    CinematicActor *pActor = registerActor( pName, pMedia, pEntity, kind );
    if ( pActor != nullptr ) {
        pActor->AvatarRefCount = avatarRefCount;
    }
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
        OTDU_UNIMPLEMENTED;
        return pEntity;
    }
    if ( type == eEntityType::ET_CPNJ ) {
        OTDU_UNIMPLEMENTED;
        return pEntity;
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
                 gpTestDriveInstance != nullptr &&
                 gpTestDriveInstance->getActiveGameMode() == GM_Intro ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: GM_INTRO (FUN_0076b410)
                return nullptr;
            }

            OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR (FUN_008f5f90)
            return spawnNamedAvatar( nullptr, GetIdentifier64bit( pName ),
                                     false );
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
                // FUN_007e4b90
                SetPlayerCarCinematicMode( true );
            }
            return nullptr;
        }

        if ( std::strncmp( "TRAFFICCAR", pName, 10 ) == 0 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
            return nullptr;
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

void *CinResCollection::spawnPlayerCar() {
    // FUN_007e6520
    if ( gpScenaric == nullptr ) {
        return nullptr;
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        return pLocalCar;
    }

    const int32_t tutorialStep = gpScenaric->getIntroTutorial().getStep();
    if ( tutorialStep != kTutorialStatePending &&
         tutorialStep != kTutorialStateRunning &&
         tutorialStep < kTutorialStateBuyCar ) {
        return nullptr;
    }

    GarageSlot *pSlot = GetCurrentGarageSlot();
    if ( pSlot == nullptr || gpCarState == nullptr || gpCarDisplay == nullptr ||
         gpDatabase == nullptr ) {
        return nullptr;
    }

    Matrix4x4f_t spawn = Matrix4x4f_t::Identity();
    Eigen::Vector4f shift = Eigen::Vector4f::Zero();
    if ( gpWorldStreaming != nullptr ) {
        shift = gpWorldStreaming->getPosition();
        spawn.row( 3 ) << shift.x(), shift.y(), shift.z(), 1.0f;
    }

    VehicleBaseResources *pRes = gpCarState->grabFreeResourceEntry();
    if ( pRes == nullptr ) {
        return nullptr;
    }

    const int32_t configID = static_cast<int32_t>( pSlot->getCarConfigID() );
    if ( !pRes->load( configID, pSlot->rimID, 0, 0, pSlot->packHashcode, 0,
                      eVehicleResourceType::VRT_Exterior ) ) {
        gpCarState->releaseResourceEntry( pRes );
        return nullptr;
    }

    CarState &state = gpCarState->grabNextFreeCarState();
    state.reset();
    state.owner = CarState::eOwner::CSO_LocalPlayer;
    state.simulationType = CarState::eSimulationType::CSST_Local;
    state.bUseShowcaseControl = false;
    state.carID = pRes->CarID;
    state.tireID = pRes->TireID;
    state.rimID = pRes->RimID;
    state.body.BackOnTrackState = 0;
    state.body.resetBackOnTrack();

    DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( state.carID );
    if ( pCarEntry == nullptr ) {
        pCarEntry = gpDatabase->getCarDBEntry( kDefaultCarDBID );
    } else if ( ( ( pCarEntry->BitField & 1 ) == 0 ) ||
                ( pCarEntry->Brand == nullptr ) ||
                ( ( pCarEntry->Brand->Flags & 1 ) == 0 ) ) {
        const char *pBikeCategory =
            gpDatabase->getStringByHashcode( 1, 0x35da5b5 );
        pCarEntry = gpDatabase->getCarDBEntry(
            ( pBikeCategory == pCarEntry->Category.pAsString )
                ? kDefaultBikeDBID
                : kDefaultCarDBID );
    }

    VehicleController *pController =
        gpDatabase->isBike( pCarEntry )
            ? static_cast<VehicleController *>( gpCarDisplay->instantiateBike(
                  &state, &pRes->Exterior, &pRes->Interior, &pRes->FrontRims,
                  &pRes->RearRims ) )
            : static_cast<VehicleController *>( gpCarDisplay->instantiateCar(
                  &state, &pRes->Exterior, &pRes->Interior, &pRes->FrontRims,
                  &pRes->RearRims ) );
    if ( pController == nullptr ) {
        gpCarState->releaseResourceEntry( pRes );
        return nullptr;
    }

    if ( gpMngEnvMap != nullptr ) {
        GSCarDisplay::SetRealTimeEnvMappedCar( pController );
    }

    PhysicsVehicle *pPhysicsVehicle = nullptr;
    if ( pRes->bUseGamePhysics != 0 && gpPhysics != nullptr ) {
        DBEntryCarPhysicsData *pPhysEntry = pCarEntry;
        const DBEntryAftermarketPack *pPack =
            gpDatabase->getAftermarketPack( pRes->PackHashcode );
        if ( state.carID == kDefaultCarDBID ) {
            pPhysEntry = gpDatabase->getCarDBEntry( 223 );
            pPack = nullptr;
        } else if ( state.carID == kDefaultBikeDBID ) {
            pPhysEntry = gpDatabase->getCarDBEntry( 1502 );
            pPack = nullptr;
        }

        BankVehicle *pBank = pRes->Exterior.pBankInstance;
        PhysFactory *pFactory =
            ( pBank != nullptr ) ? pBank->getPhysFactory() : nullptr;
        const void *pTorqueBin =
            ( pBank != nullptr ) ? pBank->getTorqueBinary() : nullptr;
        if ( pPhysEntry != nullptr ) {
            pPhysicsVehicle = gpPhysics->createVehicle(
                pFactory, spawn, *pPhysEntry, pTorqueBin, pPack );
        }
    }

    state.pPhysicsVehicle = pPhysicsVehicle;
    state.bIsIndoor = false;
    state.pVehicleBase = pController;
    state.pResource = pRes;
    if ( pRes->bSoundResource && gpVehicleSound != nullptr ) {
        state.pVehicleSound = gpVehicleSound->createInstance( &state );
    }
    pRes->RefCount++;

    if ( pPhysicsVehicle != nullptr && gpPhysics != nullptr ) {
        PhysicsLock lock;
        pPhysicsVehicle->setCinematicMode( true );
        pPhysicsVehicle->setIgnition( false, false );
        pPhysicsVehicle->resetToTransform( spawn, true );
    }

    gpScenaric->getLocalPlayer().setCarState( &state );
    pController->applyGaragePaint( *pSlot );

    state.body.WorldMatrix = spawn;
    Matrix4x4f_t offseted = spawn;
    offseted( 3, 0 ) -= shift.x();
    offseted( 3, 1 ) -= shift.y();
    offseted( 3, 2 ) -= shift.z();
    state.body.OffsetedWorldMatrix = offseted;

    if ( pController->getType() == VehicleBase::eType::VBT_Car ||
         pController->getType() == VehicleBase::eType::VBT_Bike ) {
        GSCarDisplay::SetRealTimeEnvMappedCar( pController );
    } else {
        GSCarDisplay::SetRealTimeEnvMappedCar( nullptr );
    }

    return &state;
}

void CinResCollection::applyPlayerCarTransform(
    const Matrix4x4f_t &transform ) {
    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
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
        }
        pVehicle->moveKeyframed( world );
    }
}

void *CinResCollection::spawnPedestrian( int32_t /*gender*/ ) {
    // FUN_008518b0
    if ( gpScenaric == nullptr ) {
        return nullptr;
    }

    return gpScenaric->getPedestrianBank().getFirst( eEntityType::ET_PTDR );
}

static void ReadAvatarConfigFromPNJ( AvatarConfiguration &outConfig,
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
    }
    return pAvatar;
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
