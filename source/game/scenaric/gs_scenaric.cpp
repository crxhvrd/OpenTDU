#include "shared.h"
#include "gs_scenaric.h"

#include "audio/gs_audio.h"
#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_sample.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "render/camera/gs_camera.h"
#include "filesystem/gs_boot_datas.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "database/gs_database.h"
#include "game/avatar/gs_avatar.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/mng_hud.h"
#include "render/animation/skeleton.h"
#include "render/animation/clip.h"
#include "game/gm_base.h"
#include "game/video_bumper/gm_video_bumper.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"
#include "render/gs_render.h"
#include "render/heightmap_renderer.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/render_scene.h"
#include "tdu_instance.h"

GSScenaric *gpScenaric = nullptr;
GSScenaric gScenaric = {}; // DAT_011f30c0

// TODO: This is most likely owned by a GameSystem or Manager (for now it'll
// static to save time)
std::vector<uint64_t> gRemovedSpots; // DAT_010dfe08 / DAT_010e7d24
bool gbWooshEnabled = false;         // DAT_010e7c30
bool gbDisplayWooshDebug = true;     // gDisplayWooshDebug

static bool BOOL_010e7c40 = false;  // DAT_010e7c40
static int32_t DAT_010e7c3c = 0;    // DAT_010e7c3c
static int32_t gDemoVideoIndex = 0; // DAT_010e7c34

static constexpr float kWooshSpeedToKmh = 3.6f; // DAT_00f41768

bool IsWooshImpostor( uint64_t instanceHashcode, int32_t gridIndex ) {
    // FUN_00803340
    static const char *kVegetationPrefixes[] = {
        "EL_",   "V_ARBU", "RS_",    "V_CAS",  "V_EUCAL", "V_FIC",  "V_FIR",
        "V_PAL", "V_PAP",  "V_PLUM", "V_SAMA", "V_SCH",   "V_THES", "V_YOUK" };
    static const char *kStreetFurniturePrefixes[] = {
        "EL_",    "RS_",     "SS_PWOR", "SF_TRAV",
        "ST_STI", "SF_LBOX", "SF_FIR",  "SF_DBIN" };

    const char *pName = reinterpret_cast<const char *>( &instanceHashcode );

    const char *const *ppPrefixes = nullptr;
    size_t numPrefixes = 0;
    if ( gridIndex == 0 ) {
        ppPrefixes = kVegetationPrefixes;
        numPrefixes =
            sizeof( kVegetationPrefixes ) / sizeof( kVegetationPrefixes[0] );
    } else if ( gridIndex == 1 ) {
        ppPrefixes = kStreetFurniturePrefixes;
        numPrefixes = sizeof( kStreetFurniturePrefixes ) /
                      sizeof( kStreetFurniturePrefixes[0] );
    } else {
        return false;
    }

    for ( size_t i = 0; i < numPrefixes; i++ ) {
        const size_t length = strlen( ppPrefixes[i] );
        if ( memcmp( pName, ppPrefixes[i], length ) == 0 ) {
            return true;
        }
    }

    return false;
}

bool CinematicPlayer::executeAction( int32_t param_2, void *param_3 ) {
    return false;
}

GSScenaric::GSScenaric()
    : GameSystem(),
      defaultIGE( "" ),
      startPosition( Eigen::Vector4f::Zero() ),
      bDemoMode( false ),
      demoTimer( -1.0f ),
      demoTimeEnd( 0u ),
      traceSize( 300 ),
      currentDefVersion( 0 ),
      currentSpotIndex( -1 ),
      extraRewardsCineCount( 0 ),
      rentalTimer( -1.0f ),
      objectiveSpotHashcode( 0 ),
      bDelockChallengesRoad( true ),
      bInstantChallengeEnabled( false ) {
    // FUN_00804720
    BOOL_010e7c40 = false;
    DAT_010e7c3c = 0;

    for ( ObjectiveMarker &objective : objectives ) {
        objective.Type = "";
    }

    randomGenerator.setSeed( 0x3039 );

    OTDU_UNIMPLEMENTED;
    gpScenaric = this;
}

GSScenaric::~GSScenaric() { gpScenaric = nullptr; }

bool GSScenaric::initialize( TestDriveGameInstance *pInstance ) {
    // FUN_008065a0
    pGameInstance = pInstance;
    bPaused = false;

    OTDU_UNIMPLEMENTED;

    LoadScenaricBank( "scenarics/Missions", "scworld", &worldScenaricBank );
    LoadScenaricBank( "scenarics/Missions/packs", "pack_cinematiks",
                      &cinematicPackBank );
    LoadScenaricBank( "scenarics/Missions", "extrachallenges",
                      &worldExtraChallengesBank );
    LoadScenaricBank( "scenarics/pedestrian", "scenaric_pedestrian",
                      &pedestrianBank );
    LoadScenaricBank( "scenarics/Packs", "aircrafts", &aircraftsBank );
    LoadScenaricBank( "scenarics/Packs", "boats", &boatBank );
    LoadScenaricBank( "scenarics", "world_aircrafts", &aircraftsPathsBank );
    LoadScenaricBank( "scenarics", "world_boats", &boatsPathsBank );
    LoadScenaricBank( "scenarics/Packs", "pack_spots", &spotMeshBank );

    extraChallengesCineBank.load( ExtraChallengeCineBank::ECBM_Intro );

    extraRewardsCineCount = 0;
    for ( int32_t i = 0; i < worldExtraChallengesBank.getEntityCount(); i++ ) {
        Entity *pEntity = worldExtraChallengesBank.getEntity( i );
        if ( pEntity == nullptr ) {
            continue;
        }

        const eEntityType type = pEntity->getType();
        if ( type == eEntityType::ET_SHOG || type == eEntityType::ET_HIHI ||
             type == eEntityType::ET_TRAN || type == eEntityType::ET_CVOY ) {
            static_cast<EntityExtraChallenge *>( pEntity )->setIndex(
                extraRewardsCineCount );
            extraRewardsCineCount++;
        }
    }

    ikOffsetDB.initialize();

    initWoosh();

    introTutorial.initialize( spotList );

    avatarManager.initialize();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (bookmark refresh throttle)

    return true;
}

void GSScenaric::tick( float totalTime, float deltaTime ) {
    // FUN_00806000
    if ( rentalTimer >= 0.0f ) {
        const eGameMode activeGM = pGameInstance->getActiveGameMode();
        if ( activeGM == GM_FreeRide || activeGM == GM_Mission ) {
            rentalTimer -= deltaTime;
            if ( rentalTimer < 0.0f ) {
                rentalTimer = 0.0f;
            }
        }
    }

    OTDU_UNIMPLEMENTED;

    float listenerXZ[2] = { 0.0f, 0.0f };
    float listenerSpeed = kWooshNoListener;
    CameraGame *pVehicleCamera =
        gpCamera != nullptr ? gpCamera->getActiveCamera() : nullptr;
    if ( localPlayer.getCarState() != nullptr && gpAudio != nullptr &&
         gpAudio->isListenerSyncedToCamera() && pVehicleCamera != nullptr ) {
        const Eigen::Vector3f cameraPosition =
            pVehicleCamera->getWorldPosition();
        listenerXZ[0] = cameraPosition.x();
        listenerXZ[1] = cameraPosition.z();
        listenerSpeed = pVehicleCamera->getVelocity().norm();
    }

    if ( gpWorld != nullptr && gpWorld->getStreamingManager() != nullptr ) {
        gpWorld->getStreamingManager()->tickWoosh( listenerXZ, listenerSpeed );
    }

    introTutorial.tick();

    avatarManager.tick( deltaTime, totalTime );

    if ( objectives[1].Type != nullptr && objectives[1].Type[0] != '\0' &&
         objectiveSpotHashcode != 0 ) {
        SpotSector *pSector = spotList.getSpotSector( objectiveSpotHashcode );
        if ( pSector != nullptr ) {
            objectives[1].Position =
                Eigen::Vector4f( pSector->X, pSector->Y, pSector->Z, 1.0f );
            objectiveSpotHashcode = 0;

            if ( gpMngHud != nullptr ) {
                pushObjectivesToCompass( gpMngHud );
            }
        }
    }
}

void GSScenaric::pushObjectivesToCompass( MngHud *pHud ) {
    // FUN_008036f0
    int32_t slot = 0;
    for ( const ObjectiveMarker &objective : objectives ) {
        if ( objective.Type == nullptr || objective.Type[0] == '\0' ) {
            continue;
        }
        pHud->setObjectiveMarker( slot, objective.Position, objective.Type );
        slot++;
    }
}

void GSScenaric::initWoosh() {
    // FUN_00805eb0
    bOwnsWooshWavData = false;
    if ( !gbWooshEnabled ) {
        return;
    }

    wooshMinSpeed = 80.0f;   // DAT_00f419f0
    wooshMaxSpeed = 184.0f;  // DAT_00f421e8
    wooshClipNear = 1.76f;   // DAT_00f421e4
    wooshClipFar = 12.0f;    // DAT_00f41850
    wooshRefraction = 0.75f; // DAT_00f416a4
    wooshDoppler = 1.6f;     // DAT_00f41e84

    std::string wavPath = gpConfig->getResRootPath();
    wavPath += "scenarics/woosh.wav";

    uint32_t wavSize = 0;
    if ( gpConfig->bSearchInBootFirst ) {
        pWooshWavData = gpBootDatas->findInCache( wavPath.c_str(), &wavSize );
        if ( pWooshWavData == nullptr &&
             gpFile->loadFile( wavPath.c_str(), &pWooshWavData, &wavSize ) ) {
            bOwnsWooshWavData = true;
        }
    } else {
        if ( gpFile->loadFile( wavPath.c_str(), &pWooshWavData, &wavSize ) ) {
            bOwnsWooshWavData = true;
        } else {
            pWooshWavData =
                gpBootDatas->findInCache( wavPath.c_str(), &wavSize );
        }
    }

    if ( pWooshWavData != nullptr && wavSize > 0 ) {
        auto pSample = std::make_unique<edAudioSample>();
        if ( pSample->loadFromMemory( pWooshWavData, int32_t( wavSize ) ) ) {
            wooshSample = std::move( pSample );
        }
    }

    if ( gpAudio != nullptr ) {
        pWooshAudioClient = gpAudio->createClient( "ClientWoosh" );
    }

    if ( wooshSample == nullptr || pWooshAudioClient == nullptr ) {
        OTDU_LOG_WARN( "Woosh disabled: sample %s, client %s ('%s')\n",
                       wooshSample != nullptr ? "OK" : "MISSING",
                       pWooshAudioClient != nullptr ? "OK" : "MISSING",
                       wavPath.c_str() );
    }
}

void GSScenaric::terminateWoosh() {
    // FUN_00805570
    if ( !gbWooshEnabled ) {
        return;
    }

    for ( WooshVoice &voice : wooshVoices ) {
        if ( voice.pOwner != nullptr ) {
            removeWoosh( voice.pOwner );
        }
    }

    pWooshAudioClient = nullptr;
    wooshSample.reset();

    if ( pWooshWavData != nullptr && bOwnsWooshWavData ) {
        TestDrive::Free( pWooshWavData );
        pWooshWavData = nullptr;
    }
}

bool GSScenaric::addWoosh( const Eigen::Vector3f &position,
                           const void *pOwner ) {
    // FUN_00803b70
    if ( !gbWooshEnabled ) {
        return false;
    }

    WooshVoice *pFreeVoice = nullptr;
    for ( WooshVoice &voice : wooshVoices ) {
        if ( pFreeVoice == nullptr && voice.pOwner == nullptr ) {
            pFreeVoice = &voice;
        }
        if ( voice.pOwner == pOwner ) {
            return false; // already wooshing for this object
        }
    }

    if ( pFreeVoice == nullptr ) {
        return false;
    }

    pFreeVoice->pOwner = pOwner;

    if ( pWooshAudioClient == nullptr || wooshSample == nullptr ) {
        return true;
    }

    pFreeVoice->pSound = pWooshAudioClient->acquireSoundInstance();
    if ( pFreeVoice->pSound == nullptr ) {
        return true;
    }

    AudioDispatcherPlaybackSoundInstance *pSound = pFreeVoice->pSound;
    pSound->setSample( wooshSample.get() );
    pSound->setLoopCount( -1 );
    pSound->setClipDistances( wooshClipNear, wooshClipFar );
    pSound->setPosition( position,
                         AudioDispatcherPlaybackInstanceValues::ADPCS_World );
    pSound->setRefractionFactor( wooshRefraction );
    pSound->setDopplerFactor( wooshDoppler );
    pSound->setVolume( 0.0f );
    pSound->play( 0.0f );

    return true;
}

bool GSScenaric::removeWoosh( const void *pOwner ) {
    // FUN_00802e10
    if ( !gbWooshEnabled ) {
        return false;
    }

    for ( WooshVoice &voice : wooshVoices ) {
        if ( voice.pOwner != pOwner ) {
            continue;
        }

        voice.pOwner = nullptr;
        if ( voice.pSound != nullptr ) {
            voice.pSound->stop( 0.0f );
            voice.pSound->release();
            voice.pSound = nullptr;
        }
        return true;
    }

    return false;
}

void GSScenaric::updateWooshVoices( float listenerSpeed ) {
    // FUN_00803560
    if ( !gbWooshEnabled ) {
        return;
    }

    // No vehicle camera / no car state
    if ( listenerSpeed == kWooshNoListener ) {
        for ( WooshVoice &voice : wooshVoices ) {
            if ( voice.pOwner != nullptr ) {
                removeWoosh( voice.pOwner );
            }
        }
        return;
    }

    const float speedKmh = listenerSpeed * kWooshSpeedToKmh;
    const float volume = Saturate( ( speedKmh - wooshMinSpeed ) /
                                   ( wooshMaxSpeed - wooshMinSpeed ) );

    int32_t numActiveVoices = 0;
    for ( WooshVoice &voice : wooshVoices ) {
        if ( voice.pOwner == nullptr ) {
            continue;
        }

        if ( voice.pSound != nullptr ) {
            voice.pSound->setVolume( volume );

            if ( gbDisplayWooshDebug ) {
                voice.pSound->setClipDistances( wooshClipNear, wooshClipFar );
                voice.pSound->setRefractionFactor( wooshRefraction );
                voice.pSound->setDopplerFactor( wooshDoppler );
            }
        }
        numActiveVoices++;
    }

    if ( gbDisplayWooshDebug ) {
        OTDU_LOG_DEBUG( "Speed:%f\nVolume:%f\nRefractionFactor:%f\n"
                        "DopplerFactor:%f\n%02d woosh\n",
                        speedKmh, volume, wooshRefraction, wooshDoppler,
                        numActiveVoices );
    }
}

void GSScenaric::terminate() {
    // FUN_00805c80
    OTDU_UNIMPLEMENTED;
    terminateWoosh();
    unloadBank( &avatarManager.getBank() );
    unloadBank( &cinematicPackBank );
    unloadBank( &worldScenaricBank );
    unloadBank( &worldExtraChallengesBank );
    unloadBank( &pedestrianBank );
    unloadBank( &aircraftsBank );
    unloadBank( &boatBank );
    unloadBank( &aircraftsPathsBank );
    unloadBank( &boatsPathsBank );
    unloadBank( &spotMeshBank );
    extraChallengesCineBank.unload();
    ikOffsetDB.terminate();
}

void GSScenaric::unloadBank( ScenaricBank *pBank ) {
    // FUN_00805ab0
    pBank->destroy();
    pBank->terminate();
    pBank->unload();
    unregisterBank( pBank );
}

ScenaricBank *GSScenaric::getNewSpotBank() {
    // FUN_00803770
    for ( ScenaricBank &bank : spotBanks ) {
        const ScenaricBank::eBankState state = bank.getState();
        if ( state == ScenaricBank::BBS_Unused ||
             state == ScenaricBank::BBS_LoadFailed ) {
            return &bank;
        }
    }
    return nullptr;
}

static bool IsSpotSectorVisible( const SpotSector *pSpotSector,
                                 const Sector *pSector ) {
    static constexpr int32_t kMaxNumSpotTile = 32;
    static constexpr uint16_t kSpotTileListEnd = 0xffff;
    static constexpr uint16_t kNumSectorTile =
        kHeightmapNumRealTile * kHeightmapNumRealTile;

    for ( int32_t i = 0; i < kMaxNumSpotTile; i++ ) {
        const uint16_t tile = pSpotSector->Tiles[i];
        if ( tile == kSpotTileListEnd ) {
            break;
        }

        if ( tile >= kNumSectorTile ) {
            continue;
        }

        if ( pSector->geometry.heightmapLODCache[tile] !=
             kHeightmapLODCulled ) {
            return true;
        }
    }

    return false;
}

void GSScenaric::draw( float deltaTime ) {
    // FUN_008050b0
    RenderScene *pScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Near ).pScene;

    if ( pScene != nullptr ) {
        for ( ScenaricBank &bank : spotBanks ) {
            const SpotSector *pSpotSector = bank.getSpotSector();
            const Sector *pSector = bank.getSector();

            if ( pSpotSector == nullptr || pSector == nullptr ||
                 pSector->state != 1 ) {
                continue;
            }

            if ( !IsSpotSectorVisible( pSpotSector, pSector ) ) {
                continue;
            }

            for ( HiearchyNode *pNode : bank.getSceneryNodes() ) {
                if ( pNode == nullptr || pNode->pInstance == nullptr ) {
                    continue;
                }

                pScene->enqueueDynamicHiearchy( pNode );
            }
        }
    }

    avatarManager.draw();
}

void GSScenaric::pause() {
    // FUN_00805e70
    localPlayer.setCarState( nullptr );
    rentalTimer = -1.0f;
    spotList.setUseOnlyValid( false );
    spotList.updateSpotMeshValidity();
    avatarManager.releaseAllAvatars();
}

bool GSScenaric::placeDriver( CarState *pCarState, EntityDriver *pDriver ) {
    // FUN_00805830
    if ( pCarState == nullptr || pDriver == nullptr || gpCarState == nullptr ||
         gpDatabase == nullptr ) {
        return false;
    }

    VehicleBaseResources *pResource = pCarState->getResource();
    DBEntryCarPhysicsData *pCarData = gpDatabase->getCarDBEntry(
        pResource != nullptr ? pResource->CarID : -1 );

    if ( !gpDatabase->isBike( pCarData ) &&
         gpDatabase->isTraffic( pCarData ) ) {
        return false;
    }

    gpCarState->registerCallback( pCarState, pDriver, nullptr );
    pDriver->setCarState( pCarState );
    pCarState->pDriver = pDriver;
    pDriver->setSeating( true );

    if ( pDriver->getDriveType() == RT_Car && gpAvatar != nullptr &&
         pCarData != nullptr ) {
        gpAvatar->installBelt( pCarData->BeltType.pAsString, pDriver,
                               ABS_Driver );
    }

    return true;
}

void GSScenaric::reset() {
    // FUN_00805c30
    EntityAvatar *pDriver = avatarManager.allocateAvatar( ~0ull, 1 );
    if ( pDriver != nullptr ) {
        pDriver->removeInstanceReference();
    }
}

GameIntroTutorial &GSScenaric::getIntroTutorial() { return introTutorial; }

void GSScenaric::clearObjectives() {
    // FUN_0074ecc0 (inlined)
    for ( ObjectiveMarker &objective : objectives ) {
        objective.Type = "";
    }
    objectiveSpotHashcode = 0;
}

void GSScenaric::setDefaultStartPosition( const Eigen::Vector4f &position ) {
    startPosition = position;
}

void GSScenaric::setCinematicViewerDefault( const std::string &param_1,
                                            const std::string &param_2 ) {
    cineViewConfig.BankName = param_1;
    cineViewConfig.CinematicName = param_2;
}

void GSScenaric::setNextIntro( const char *pPath,
                               const char *pFilename,
                               const char *pCinematicName,
                               const eGameMode nextGameMode,
                               SpotMap *pSpot ) {
    // FUN_008031f0
    nextIntro.pPath = pPath;
    nextIntro.pFilename = pFilename;
    nextIntro.pCinematicName = pCinematicName;
    nextIntro.bIsActive = true;
    nextIntro.NextGameMode = nextGameMode;
    nextIntro.pSpot = pSpot;
}

void GSScenaric::setDemoTimer( float param_1 ) { demoTimer = param_1; }

void GSScenaric::startDemoMode() {
    // FUN_008037e0
    static constexpr const char *kDemoVideoNames[2] = {
        "Trailer.bik", "IntroTDU.bik" }; // DAT_00f8b060

    bDemoMode = true;
    demoTimeEnd = 0u;

    GMVideoBumper::SetActiveBumper( kDemoVideoNames[gDemoVideoIndex] );

    gDemoVideoIndex++;
    if ( gDemoVideoIndex >= 2 ) {
        gDemoVideoIndex = 0;
    }

    gpActiveGameMode->requestExit();

    GMVideoBumper::SetNextGameMode( GM_Login );
    GMVideoBumper::SetSkippable();

    gpTestDriveInstance->setNextGameMode( GM_VideoBumper );
}

void GSScenaric::delockChallengeRoads( const bool bDelocked ) {
    bDelockChallengesRoad = bDelocked;
}

void GSScenaric::setTraceSize( uint32_t param_1 ) { traceSize = param_1; }

void GSScenaric::setDefaultIGE( const char *param_1 ) { defaultIGE = param_1; }

void GSScenaric::LoadScenaricBank( const char *pBankFolder,
                                   const char *pBankName,
                                   ScenaricBank *pOutBank,
                                   bool bNoCopy ) {
    // FUN_00806470
    std::string bankPath = gpConfig->getResRootPath();
    bankPath += pBankFolder;
    bankPath += '/';
    bankPath += pBankName;
    bankPath += ".bnk";

    uint32_t bankSize = 0;
    void *pCachedData = gpBootDatas->findInCache( bankPath.c_str(), &bankSize );

    if ( pCachedData == nullptr ) {
        pOutBank->loadFromFile( bankPath.c_str(), nullptr, bNoCopy );
        pOutBank->forceLoad();
    } else {
        pOutBank->load( pCachedData, 0, 0, nullptr, bNoCopy );
        pOutBank->forceLoad();
    }

    if ( gpScenaric != nullptr &&
         pOutBank != gpScenaric->getWorldScenaricBank() ) {
        gpScenaric->registerBank( pOutBank );
    }
}

void GSScenaric::loadPacks() {
    // FUN_00806550
    LoadScenaricBank( "scenarics/Missions/packs", "worldactors",
                      &worldActorsBank );
    LoadScenaricBank( "scenarics/Packs", "worldlifeset", &worldLifeSetBank );
}

void GSScenaric::unloadPacks() {
    // FUN_009b99b0 (inlined)
    unloadBank( &worldActorsBank );
    unloadBank( &worldLifeSetBank );
}

void GSScenaric::prepareWorldEntityTemplates() {
    if ( worldScenaricBank.isLoaded() ) {
        worldScenaricBank.activate( nullptr );
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr || !pBank->isLoaded() ) {
            continue;
        }
        pBank->activate( nullptr );
    }
    if ( gpWorld != nullptr && gpWorld->getStreamingManager() != nullptr ) {
        Bank &spotsBank = gpWorld->getStreamingManager()->getLevel().spotsBank;
        void *pSpotResource =
            spotsBank.getFirstEntry( static_cast<eAssetGroup>( 8 ),
                                     static_cast<eAssetType>( 0x13 ), nullptr );
        spotList.installWorldSpots( pSpotResource );
        introTutorial.initialize( spotList );

        // The initial sectors streamed in before gpScenaric existed, so they
        // never installed their per-sector spot lists; do it now.
        gpWorld->getStreamingManager()->refreshSectorSpots();
        gpWorld->getStreamingManager()->refreshSectorProps();
    }

    OTDU_UNIMPLEMENTED;
}

// TEMPORARY - Make sure world bank lookup always resolve to a valid entity.
// Can be removed once we confirm this is NOP/never hits
template <typename LookupFn>
static auto FindInUnregisteredWorldBank( ScenaricBank *pWorldBank,
                                         const char *pWhat,
                                         uint64_t hashcode,
                                         LookupFn lookup )
    -> decltype( lookup( pWorldBank ) ) {
    auto pResult = lookup( pWorldBank );
    if ( pResult != nullptr ) {
        OTDU_TRIGGER_BREAKPOINT;
        OTDU_LOG_WARN( "%s %016llx resolved from the world scenaric bank, "
                       "which retail leaves unregistered\n",
                       pWhat, (unsigned long long)hashcode );
    }
    return pResult;
}

Entity *GSScenaric::findEntityByHashcode( uint64_t hashcode ) const {
    // FUN_00425b90
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        Entity *pEntity = pBank->getEntityByHashcode( hashcode );
        if ( pEntity != nullptr ) {
            return pEntity;
        }
    }
    return FindInUnregisteredWorldBank(
        getWorldScenaricBank(), "entity", hashcode,
        [hashcode]( ScenaricBank *pBank ) {
            return pBank->getEntityByHashcode( hashcode );
        } );
}

SoundBinary *GSScenaric::getSCTDSound( uint64_t hashcode ) const {
    // FUN_00428e80
    if ( hashcode == 0 ) {
        return nullptr;
    }

    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        SoundBinary *pSound = pBank->getSoundByHashcode( hashcode );
        if ( pSound != nullptr ) {
            return pSound;
        }
    }
    return FindInUnregisteredWorldBank(
        getWorldScenaricBank(), "SCTDSound", hashcode,
        [hashcode]( ScenaricBank *pBank ) {
            return pBank->getSoundByHashcode( hashcode );
        } );
}

void *GSScenaric::findWavData( uint64_t hashcode, int32_t *pOutSize ) const {
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        void *pData = pBank->getWavData( hashcode, pOutSize );
        if ( pData != nullptr ) {
            return pData;
        }
    }
    return FindInUnregisteredWorldBank(
        getWorldScenaricBank(), "wav", hashcode,
        [hashcode, pOutSize]( ScenaricBank *pBank ) {
            return pBank->getWavData( hashcode, pOutSize );
        } );
}

Entity *GSScenaric::getSCVolume( uint64_t hashcode ) const {
    // FUN_00424510
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        Entity *pVolume = pBank->getVolumeByHashcode( hashcode );
        if ( pVolume != nullptr ) {
            return pVolume;
        }
    }
    return FindInUnregisteredWorldBank(
        getWorldScenaricBank(), "SCVolume", hashcode,
        [hashcode]( ScenaricBank *pBank ) {
            return pBank->getVolumeByHashcode( hashcode );
        } );
}

Path *GSScenaric::getSCPath( uint64_t hashcode ) const {
    // FUN_00428e00
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        Path *pPath = pBank->getPathByHashcode( hashcode );
        if ( pPath != nullptr ) {
            return pPath;
        }
    }
    return FindInUnregisteredWorldBank(
        getWorldScenaricBank(), "SCPath", hashcode,
        [hashcode]( ScenaricBank *pBank ) {
            return pBank->getPathByHashcode( hashcode );
        } );
}

void GSScenaric::registerBank( ScenaricBank *pBank ) {
    if ( pBank == nullptr ) {
        return;
    }
    if ( std::find( registeredBanks.begin(), registeredBanks.end(), pBank ) !=
         registeredBanks.end() ) {
        return;
    }
    registeredBanks.push_back( pBank );
}

void GSScenaric::unregisterBank( ScenaricBank *pBank ) {
    registeredBanks.erase(
        std::remove( registeredBanks.begin(), registeredBanks.end(), pBank ),
        registeredBanks.end() );
}

Render3DD *GSScenaric::findRender3DD( uint64_t hashcode ) {
    // FUN_00428b00
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        if ( Render3DD *pResult = pBank->getRender3DD( hashcode ) ) {
            return pResult;
        }
    }
    return FindInUnregisteredWorldBank( getWorldScenaricBank(), "3DD", hashcode,
                                        [hashcode]( ScenaricBank *pBank ) {
                                            return pBank->getRender3DD(
                                                hashcode );
                                        } );
}

Render3DG *GSScenaric::findRender3DG( uint64_t hashcode ) {
    // FUN_00428a80
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        if ( Render3DG *pResult = pBank->getRender3DG( hashcode ) ) {
            return pResult;
        }
    }
    return FindInUnregisteredWorldBank( getWorldScenaricBank(), "3DG", hashcode,
                                        [hashcode]( ScenaricBank *pBank ) {
                                            return pBank->getRender3DG(
                                                hashcode );
                                        } );
}

Render2DM *GSScenaric::findRender2DM( uint64_t hashcode ) {
    // FUN_00428a00
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        if ( Render2DM *pResult = pBank->getRender2DM( hashcode ) ) {
            return pResult;
        }
    }
    return FindInUnregisteredWorldBank( getWorldScenaricBank(), "2DM", hashcode,
                                        [hashcode]( ScenaricBank *pBank ) {
                                            return pBank->getRender2DM(
                                                hashcode );
                                        } );
}

Render2DB *GSScenaric::findRender2DB( uint64_t hashcode ) {
    // FUN_00428980
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        if ( Render2DB *pResult = pBank->getRender2DB( hashcode ) ) {
            return pResult;
        }
    }
    return FindInUnregisteredWorldBank( getWorldScenaricBank(), "2DB", hashcode,
                                        [hashcode]( ScenaricBank *pBank ) {
                                            return pBank->getRender2DB(
                                                hashcode );
                                        } );
}

SkeletonInstance *GSScenaric::findSkeleton( uint64_t hashcode ) {
    // FUN_00428d00
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        if ( SkeletonInstance *pResult = pBank->getSkeleton( hashcode ) ) {
            return pResult;
        }
    }
    return FindInUnregisteredWorldBank(
        getWorldScenaricBank(), "skeleton", hashcode,
        [hashcode]( ScenaricBank *pBank ) {
            return pBank->getSkeleton( hashcode );
        } );
}

SkeletonInstance *GSScenaric::resolveSkeleton( ScenaricBank *pBank,
                                               void *pRefSkeleton ) {
    // FUN_007da840
    if ( pRefSkeleton == nullptr ) {
        return nullptr;
    }
    const uint32_t *pRef = static_cast<const uint32_t *>( pRefSkeleton );
    const uint32_t id = pRef[0];
    const uint32_t hash = pRef[1];
    if ( id == 0 && hash == 0 ) {
        return nullptr;
    }
    const uint64_t key = ( static_cast<uint64_t>( hash ) << 32 ) | id;

    SkeletonInstance *pResult =
        pBank != nullptr ? pBank->getSkeleton( key ) : nullptr;
    if ( pResult == nullptr ) {
        pResult = findSkeleton( key );
    }
    return pResult;
}

ClipInstance *GSScenaric::findClip( uint64_t hashcode ) {
    if ( hashcode == 0 ) {
        return nullptr;
    }
    for ( ScenaricBank *pBank : registeredBanks ) {
        if ( pBank == nullptr ) {
            continue;
        }
        if ( ClipInstance *pResult = pBank->getClip( hashcode ) ) {
            return pResult;
        }
    }
    return FindInUnregisteredWorldBank( getWorldScenaricBank(), "clip",
                                        hashcode,
                                        [hashcode]( ScenaricBank *pBank ) {
                                            return pBank->getClip( hashcode );
                                        } );
}

ClipInstance *GSScenaric::resolveClip( ScenaricBank *pBank, void *pRefAnm ) {
    // FUN_007da8a0
    if ( pRefAnm == nullptr ) {
        return nullptr;
    }
    const uint32_t *pRef = static_cast<const uint32_t *>( pRefAnm );
    const uint32_t id = pRef[0];
    const uint32_t hash = pRef[1];
    if ( id == 0 && hash == 0 ) {
        return nullptr;
    }
    const uint64_t key = ( static_cast<uint64_t>( hash ) << 32 ) | id;

    ClipInstance *pResult = pBank != nullptr ? pBank->getClip( key ) : nullptr;
    if ( pResult == nullptr ) {
        pResult = findClip( key );
    }
    return pResult;
}
