#include "shared.h"
#include "gm_living.h"

#include "tdu_instance.h"

#include "core/time.h"
#include "flash/gs_flash.h"
#include "flash/mng_fade.h"
#include "flash/implementation/flash_player.h"
#include "game/mng_music.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/spots.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/achievement_checks.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/mng_ocean.h"
#include "world/streaming_manager.h"

bool gbEnteringLivingHub = false;                       // BOOL_00f94778
static constexpr float kArrestedTimeReset = -144000.0f; // DAT_00f41a00

GMLiving::GMLiving()
    : GameModeWorldBase(),
      mngReflection(),
      mngLogoDisplay(),
      mngCollection(),
      mngRankings(),
      sceneLiving(),
      pFlashPlayer( nullptr ),
      resetTime( 0.0f ),
      bBlockSceneUpdate( false ),
      bSceneResumeAllowed( false ),
      bHouseSpotInstalled( false ),
      bRadialMenuOpen( false ) {
    // FUN_0076a6f0
    registerManager( &mngReflection );

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING

    registerManager( &mngLogoDisplay );
    registerManager( &mngCollection );
    registerManager( &mngRankings );

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING

    mngCollection.setGameMode( this );
    mngRankings.setGameMode( this );

    setTransitionFlags( 0x27 );

    resolveHouseSpot();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

GMLiving::~GMLiving() {
    // FUN_0076a580
}

void GMLiving::resolveHouseSpot() {
    if ( gpScenaric == nullptr || gpPlayerData == nullptr ||
         gpPlayerData->getLoadedSave() == nullptr ) {
        return;
    }

    Spots &spots = gpScenaric->getSpotList();
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();

    int32_t spotIndex = gpScenaric->getCurrentSpotIndex();
    SpotMap *pSpot =
        ( spotIndex != -1 ) ? spots.getWorldSpotChecked( spotIndex ) : nullptr;

    if ( pSpot == nullptr || pSpot->Type != eSpotType::ST_House ) {
        const uint64_t houseHashcode = pDriver->getCurrentHouse();

        spotIndex = spots.getSpotMapIndex( houseHashcode );
        pSpot = spots.getWorldSpotChecked( spotIndex );

        if ( spotIndex == -1 ) {
            pSpot = spots.findSpotMapByType( eSpotType::ST_House, 2, 0,
                                             &spotIndex );
        }
    }

    gpScenaric->setCurrentSpotIndex( spotIndex );

    if ( pSpot != nullptr ) {
        OTDU_LOG_DEBUG( "House spot resolved: index %d, hashcode 0x%016llx, "
                        "position %.1f %.1f %.1f\n",
                        spotIndex, (unsigned long long)pSpot->Hashcode,
                        pSpot->X, pSpot->Y, pSpot->Z );
        pDriver->selectCurrentHouse( pSpot->Hashcode );
    }
}

void GMLiving::installHouseSpot() {
    // FUN_00769770
    SceneInterior *pInterior = sceneLiving.getContent();

    installCinematics( pInterior->getSourceSpot(), &pInterior->getBank() );

    const eGameMode previousGM = gpTestDriveInstance->getPreviousGameMode();
    if ( previousGM != GM_BootMenu && previousGM != GM_Login &&
         previousGM != GM_IntroDemo ) {
        return;
    }

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    Spots &spots = gpScenaric->getSpotList();

    const int32_t houseIndex =
        spots.getSpotMapIndex( pDriver->getCurrentHouse() );
    SpotMap *pHouse = spots.getWorldSpotChecked( houseIndex );
    if ( pHouse == nullptr ) {
        return;
    }

    const SpotSector *pHouseSector = spots.getSpotSector( pHouse->Hashcode );
    if ( pHouseSector == nullptr ) {
        return;
    }

    pCinematicIntroE =
        FindStreamedSpotCinematic( pHouseSector, "C_INTRO_SHORT" );
    if ( pCinematicIntroE == nullptr ) {
        return;
    }

    pCinematicIntroE->setStopWhenSkipped( false );
    pCinematicIntroE->clearEnded();

    if ( gpMngFade != nullptr ) {
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
    }
}

void GMLiving::initialize() {
    // FUN_0076b1c0
    GameModeWorldBase::initialize();

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }

    mngWorld.setDisableStreamingOnCar( true );
    mngWorld.setStreamEnabled( false );

    sceneLiving.initialize();

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (house menu back-pointers)

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;

    OTDU_UNIMPLEMENTED; // TODO: SECTOR

    pStreaming->setActiveRenderLayers( 1 );

    if ( gpMngMusic != nullptr ) {
        gpMngMusic->setPlayFlags( 1 );
    }
}

void GMLiving::reset() {
    // FUN_00769a10
    GameModeScene::reset();

    if ( gpDetails != nullptr ) {
        TDUDetail *pBirds = gpDetails->getDetailByHash( 0x62697264 ); // "bird"
        if ( pBirds != nullptr ) {
            pBirds->setEnabled( false );
        }

        TDUDetail *pPapers = gpDetails->getDetailByHash( 0x70617072 ); // "papr"
        if ( pPapers != nullptr ) {
            pPapers->setEnabled( false );
        }
    }

    pFlashPlayer = gpFlash->getFlashPlayer( "HOME" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (bind the news widget to the movie and
                        // attach it to the save's news list)

    pFlashPlayer->showFrame( "living_in", "/", true );

    const eGameMode previousGM = gpTestDriveInstance->getPreviousGameMode();
    if ( previousGM == GM_Garage ) {
        pFlashPlayer->setVariableValueNonLinked( "/:WhereFrom", "garage" );
    } else if ( previousGM == GM_Dressing ) {
        pFlashPlayer->setVariableValueNonLinked( "/:WhereFrom", "avatar" );
    }

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING
    OTDU_UNIMPLEMENTED; // TODO: WORLD

    resetTime = GetTimeSeconds();

    gpPlayerData->resetArrestState( kArrestedTimeReset );

    bRadialMenuOpen = false;
    bSceneResumeAllowed = true;

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection, false );
}

void GMLiving::tick( float deltaTime, float totalTime ) {
    // FUN_0076abe0
    if ( !bBlockSceneUpdate && bSceneResumeAllowed &&
         gpTestDriveInstance != nullptr ) {
        const eGameMode previousGM = gpTestDriveInstance->getPreviousGameMode();
        if ( previousGM != GM_BootMenu && previousGM != GM_Login &&
             previousGM != GM_IntroDemo ) {
            sceneLiving.resume();

            if ( gbEnteringLivingHub ) {
                gbEnteringLivingHub = false;
            }
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (MngNetResource tick)

    GameModeScene::tick( deltaTime, totalTime );

    if ( gpScenaric != nullptr && gpScenaric->getCurrentSpotIndex() == -1 &&
         gpScenaric->getSpotList().getNumWorldSpots() > 0 ) {
        resolveHouseSpot();
    }

    sceneLiving.tick( deltaTime, totalTime );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING

    if ( !bHouseSpotInstalled &&
         sceneLiving.getContent()->getState() == SceneInterior::SIS_Live ) {
        installHouseSpot();
        bHouseSpotInstalled = true;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void GMLiving::pause() {
    // FUN_007698b0
    sceneLiving.suspend();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
    OTDU_UNIMPLEMENTED; // TODO: GMLIVING

    GameModeScene::pause();
}

void GMLiving::destroy() {
    // FUN_00769bd0
    OTDU_UNIMPLEMENTED; // TODO: ONLINE

    mngWorld.setDisableStreamingOnCar( false );

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING

    sceneLiving.pause();
    sceneLiving.terminate();

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;

    OTDU_UNIMPLEMENTED; // TODO: SECTOR

    pStreaming->setActiveRenderLayers( 1 );

    GameModeWorldBase::destroy();
}

bool GMLiving::executeAction( int32_t param_2, void *param_3 ) {
    // FUN_0076ab80
    if ( param_2 == CE_Stopping && pCinematicIntroE != nullptr &&
         pCinematicIntroE == param_3 ) {
        sceneLiving.resume();

        if ( gbEnteringLivingHub ) {
            gbEnteringLivingHub = false;
        }
    }

    return GameModeScene::executeAction( param_2, param_3 );
}

bool GMLiving::onMessage( FlashMessage *pMessage ) {
    // FUN_00769c90
    switch ( *pMessage ) {
    case FlashMessage( "gofr" ): { // the "go driving" door
        gpScenaric->setDemoModeActive( false );
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );

        SpotMap *pSourceSpot = sceneLiving.getContent()->getSourceSpot();
        if ( pSourceSpot != nullptr ) {
            gpScenaric->setNextIntro( "level\\hawai\\spots\\", nullptr,
                                      "C_OUTRO", GM_FreeRide, pSourceSpot );
        }

        pFlashPlayer->stopClip( "/" );
        return true;
    }

    case FlashMessage( "gain" ): // the garage door
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMGarage
        return true;

    case FlashMessage( "avin" ): // the wardrobe door
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMDressing
        return true;

    case FlashMessage( "liou" ): // back out to the house exterior
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GMHome
        return true;

    case FlashMessage( "mahe" ): { // the mailbox help line
        char helpText[32];
        snprintf( helpText, sizeof( helpText ), "There's no new mail" );
        pFlashPlayer->setVariableValueNonLinked( "/:mails_help ", helpText );
        return true;
    }

    case FlashMessage( "rain" ): // ranking in
        bRadialMenuOpen = true;
        mngRankings.onMessage( pMessage );
        return true;

    case FlashMessage( "raou" ): // ranking out
        bRadialMenuOpen = false;
        mngRankings.onMessage( pMessage );
        return true;

    case FlashMessage( "rsdm" ):
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYER_DATA (cheat switch)
        return true;

    case FlashMessage( "news" ): // open a news item
    case FlashMessage( "nehe" ): // news item closed
    case FlashMessage( "newq" ):
    case FlashMessage( "nede" ): // delete the selected news item
    case FlashMessage( "locs" ): // locate the news item's spot on the map
    case FlashMessage( "vgcd" ):
    case FlashMessage( "answ" ): // answer a club/team invitation
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (mail/news/club invitations)
        return true;

    default:
        break;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING (house menu message handler)

    return mngCollection.onMessage( pMessage ) ||
           mngRankings.onMessage( pMessage );
}
