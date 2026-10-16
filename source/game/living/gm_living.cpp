#include "shared.h"
#include "gm_living.h"

#include "tdu_instance.h"

#include "core/time.h"
#include "flash/flash_callbacks_register.h"
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
#include "player_data/saved_mailbox.h"
#include "online/gs_online.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_ocean.h"
#include "world/streaming_manager.h"
#include "physics/gs_physics.h"
#include "render/camera/camera_first_person.h"
#include "render/camera/gs_camera.h"

bool gbEnteringLivingHub = false;                       // BOOL_00f94778
static constexpr float kArrestedTimeReset = -144000.0f; // DAT_00f41a00

static inline void BuildHouseBankName( const SpotMap *pSpot,
                                       char ( &outName )[9] ) {
    memcpy( outName, &pSpot->SourceHashcode, sizeof( pSpot->SourceHashcode ) );
    outName[8] = '\0';
    outName[0] = 'L';
}

GMLiving::GMLiving()
    : GameModeWorldBase(),
      mngReflection(),
      mngLogoDisplay(),
      mngCollection(),
      mngRankings(),
      mngSpotPicture(),
      sceneLiving(),
      newsWidget(),
      firstPerson(),
      pFlashPlayer( nullptr ),
      resetTime( 0.0f ),
      bBlockSceneUpdate( false ),
      bSceneResumeAllowed( false ),
      bHouseSpotInstalled( false ),
      bRadialMenuOpen( false ),
      pendingHouseSpotIndex( -1 ) {
    // FUN_0076a6f0
    registerManager( &mngReflection );

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (house menu)

    registerManager( &mngLogoDisplay );
    registerManager( &mngCollection );
    registerManager( &mngRankings );
    registerManager( &mngSpotPicture );

    mngCollection.setGameMode( this );
    mngRankings.setGameMode( this );

    setTransitionFlags( 0x27 );

    resolveHouseSpot();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

GMLiving::~GMLiving() {
    // FUN_0076a580
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngSpotPicture::PictureDrawCallback );
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
    pCinematicIntroE->installMusicBanks();
    pCinematicIntroE->clearEnded();

    if ( gpMngFade != nullptr ) {
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
    }
}

#ifdef OTDU_DEVBUILD
void GMLiving::requestHouseSwitch( int32_t spotIndex ) {
    pendingHouseSpotIndex = spotIndex;
}

void GMLiving::switchHouse( int32_t spotIndex ) {
    if ( gpScenaric == nullptr ) {
        return;
    }

    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked( spotIndex );
    if ( pSpot == nullptr || pSpot->Type != eSpotType::ST_House ) {
        return;
    }

    char bankName[9];
    BuildHouseBankName( pSpot, bankName );
    OTDU_LOG_INFO( "Switching to house spot %d '%s' at (%.1f %.1f %.1f)\n",
                   spotIndex, bankName, pSpot->X, pSpot->Y, pSpot->Z );

    firstPerson.leave();

    pCinematicLoop = nullptr;
    pCinematicIntroI = nullptr;

    sceneLiving.terminate();

    gpScenaric->setCurrentSpotIndex( spotIndex );
    if ( gpPlayerData != nullptr && gpPlayerData->getLoadedSave() != nullptr ) {
        gpPlayerData->getLoadedSave()->getDriver()->selectCurrentHouse(
            pSpot->Hashcode );
    }

    sceneLiving.initialize();
    bHouseSpotInstalled = false;
}
#endif

#ifdef OTDU_USE_IMGUI
void GMLiving::drawDebug( float deltaTime ) {
    if ( gpScenaric == nullptr ) {
        return;
    }

    SceneInterior *pInterior = sceneLiving.getContent();
    Spots &spots = gpScenaric->getSpotList();

    ImGui::SeparatorText( "House" );

    const int32_t currentIndex = gpScenaric->getCurrentSpotIndex();
    SpotMap *pCurrentSpot = spots.getWorldSpotChecked( currentIndex );
    if ( pCurrentSpot != nullptr ) {
        char bankName[9];
        BuildHouseBankName( pCurrentSpot, bankName );
        ImGui::Text( "Spot %d '%s'  hashcode 0x%016llx", currentIndex, bankName,
                     (unsigned long long)pCurrentSpot->Hashcode );
        ImGui::Text( "Origin %.1f %.1f %.1f", pCurrentSpot->X, pCurrentSpot->Y,
                     pCurrentSpot->Z );
    } else {
        ImGui::Text( "No house spot resolved (index %d)", currentIndex );
    }

    ImGui::Text( "Scene state %d (4 = Live), root node %p",
                 (int32_t)pInterior->getState(),
                 (void *)pInterior->getRootNode() );

    ImGui::SeparatorText( "Jump to house" );
    if ( ImGui::BeginChild( "HouseList", ImVec2( 0.0f, 200.0f ),
                            ImGuiChildFlags_Borders ) ) {
        const uint32_t numSpots = spots.getNumWorldSpots();
        int32_t numHouses = 0;
        for ( uint32_t i = 0; i < numSpots; i++ ) {
            SpotMap *pSpot = spots.getWorldSpotChecked( (int32_t)i );
            if ( pSpot == nullptr || pSpot->Type != eSpotType::ST_House ) {
                continue;
            }
            numHouses++;

            char bankName[9];
            BuildHouseBankName( pSpot, bankName );

            char label[96];
            snprintf( label, sizeof( label ),
                      "%s##%u   lvl %u   %.0f %.0f %.0f", bankName, i,
                      (uint32_t)pSpot->MinLevelRequired, pSpot->X, pSpot->Y,
                      pSpot->Z );

            const bool bSelected = ( (int32_t)i == currentIndex );
            if ( ImGui::Selectable( label, bSelected ) && !bSelected ) {
                requestHouseSwitch( (int32_t)i );
            }
        }

        if ( numHouses == 0 ) {
            ImGui::TextUnformatted( "No house spots loaded yet." );
        }
    }
    ImGui::EndChild();

    ImGui::SeparatorText( "Camera" );

    ImGui::Text( "First Person Mode Active: %i", firstPerson.isActive() );
    if ( ImGui::Button( firstPerson.isActive() ? "Leave first person"
                                               : "Enter first person" ) ) {
        if ( firstPerson.isActive() ) {
            firstPerson.leave();
        } else {
            firstPerson.enter( pInterior, pCinematicLoop );
        }
    }

    const Eigen::Vector4f &hullMin = firstPerson.getHullMin();
    const Eigen::Vector4f &hullMax = firstPerson.getHullMax();
    ImGui::Text( "Hull %u triangles", firstPerson.getHullNumTriangles() );
    ImGui::Text( "  min %.1f %.1f %.1f", hullMin.x(), hullMin.y(),
                 hullMin.z() );
    ImGui::Text( "  max %.1f %.1f %.1f", hullMax.x(), hullMax.y(),
                 hullMax.z() );

    if ( gpCamera != nullptr && gpCamera->getRenderCamera() != nullptr ) {
        const Eigen::Vector3f renderPos =
            gpCamera->getRenderCamera()->getWorldPosition();
        ImGui::Text( "Render camera %.1f %.1f %.1f", renderPos.x(),
                     renderPos.y(), renderPos.z() );
    }

    ImGui::Checkbox( "Draw collision wireframe (gDrawCollisionVis)",
                     &gDrawCollisionVis );
}
#endif

void GMLiving::initialize() {
    // FUN_0076b1c0
    GameModeWorldBase::initialize();

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }

    mngWorld.setDisableStreamingOnCar( true );
    mngWorld.setStreamEnabled( false );

    sceneLiving.initialize();

    sceneLiving.getContent()->setModeActive( true );

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (house menu)

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;

    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;

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

    newsWidget.pMovie = pFlashPlayer;
    SavedMailbox *pMailbox = gpPlayerData->getLoadedSave()->getMailbox();
    newsWidget.pTarget = pMailbox;
    pMailbox->subscribe( &newsWidget );

    pFlashPlayer->showFrame( "living_in", "/", true );

    const eGameMode previousGM = gpTestDriveInstance->getPreviousGameMode();
    if ( previousGM == GM_Garage ) {
        pFlashPlayer->setVariableValueNonLinked( "/:WhereFrom", "garage" );
    } else if ( previousGM == GM_Dressing ) {
        pFlashPlayer->setVariableValueNonLinked( "/:WhereFrom", "avatar" );
    }

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (weather + FUN_00765fd0)

    if ( gpWorldStreaming != nullptr &&
         gpWorldStreaming->isStreamingActive() ) {
        gpWorldStreaming->setStreamCenter( nullptr );
    }

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
    if ( pendingHouseSpotIndex >= 0 ) {
        const int32_t spotIndex = pendingHouseSpotIndex;
        pendingHouseSpotIndex = -1;
        switchHouse( spotIndex );
    }

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

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING (FUN_00768a90)

    if ( !bHouseSpotInstalled &&
         sceneLiving.getContent()->getState() == SceneInterior::SIS_Live ) {
        installHouseSpot();
        bHouseSpotInstalled = true;
    }

    firstPerson.tick( sceneLiving.getContent(), pCinematicLoop );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void GMLiving::pause() {
    // FUN_007698b0
    firstPerson.leave();

    sceneLiving.suspend();

    newsWidget.detach();
    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (FUN_00765f80)

    GameModeScene::pause();
}

void GMLiving::destroy() {
    // FUN_00769bd0
    firstPerson.leave();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (MngNetResource terminate)

    gpPlayerData->getLoadedSave()->getMailbox()->detachPictures();

    mngWorld.setDisableStreamingOnCar( false );

    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (FUN_009dc690)

    sceneLiving.pause();
    sceneLiving.getContent()->setModeActive( false );
    sceneLiving.terminate();

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;

    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;

    pStreaming->setActiveRenderLayers( 1 );

    GameModeWorldBase::destroy();
}

bool GMLiving::executeAction( int32_t param_2, void *param_3, void *pData ) {
    // FUN_0076ab80
    if ( param_2 == CE_Stopping && pCinematicIntroE != nullptr &&
         pCinematicIntroE == param_3 ) {
        sceneLiving.resume();

        if ( gbEnteringLivingHub ) {
            gbEnteringLivingHub = false;
        }
    }

    return GameModeScene::executeAction( param_2, param_3, pData );
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
        transitionFlags &= ~0x20u;
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Garage );
        pFlashPlayer->stopClip( "/" );
        return true;

    case FlashMessage( "avin" ): // the wardrobe door
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Dressing );
        pFlashPlayer->stopClip( "/" );
        return true;

    case FlashMessage( "liou" ): // back out to the house exterior
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Home );
        pFlashPlayer->stopClip( "/" );
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
        // Restart game with cheats enabled. NOTE: unreachable in retail game
        // (no flash movie send this message AFAIK)
        gpPlayerData->newGame( true );
        return true;

    case FlashMessage( "news" ): { // open a news item
        gpOnline->setNewsReaderOpen( true );

        SavedMailbox *pMailbox = gpPlayerData->getLoadedSave()->getMailbox();
        const int32_t newsIndex =
            pFlashPlayer->getVariableAsInt( "/:num_news" ) - 1;
        if ( newsIndex >= 0 ) {
            MailMessage *pMessage = pMailbox->getVisibleMessage( newsIndex );
            if ( pMessage->Category != 0 ) {
                pMessage->fillNewsLayout( pFlashPlayer );
                mngSpotPicture.setTarget(
                    pFlashPlayer, "ss_menu/news/news_layout/spot_datas/pict" );
                mngSpotPicture.showSpot( gpScenaric->getSpotList().getSpotMap(
                    pMessage->Hashcode ) );
            }
        }
        return true;
    }

    case FlashMessage( "nehe" ): // news item closed
    case FlashMessage( "newq" ):
        gpOnline->setNewsReaderOpen( false );
        return true;

    case FlashMessage( "nede" ): { // delete the selected news item
        SavedMailbox *pMailbox = gpPlayerData->getLoadedSave()->getMailbox();
        const int32_t newsIndex =
            pFlashPlayer->getVariableAsInt( "/:num_news" ) - 1;
        if ( pMailbox->getVisibleMessage( newsIndex )->Category != 0 ) {
            pMailbox->removeVisibleMessage( newsIndex );
        }
        return true;
    }

    case FlashMessage( "locs" ): { // locate the news item's spot on the map
        SavedMailbox *pMailbox = gpPlayerData->getLoadedSave()->getMailbox();
        const int32_t newsIndex =
            pFlashPlayer->getVariableAsInt( "/:num_news" ) - 1;
        const MailMessage *pMessage = pMailbox->getVisibleMessage( newsIndex );
        if ( pMessage->Category != 0 ) {
            pMessage->locateOnMap( pFlashPlayer );
        }
        return true;
    }

    case FlashMessage( "vgcd" ):
        if ( gpOnline->isNewsReaderOpen() ) {
            return true;
        }
        break;

    case FlashMessage( "answ" ): // answer a club/team invitation
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (club invitations)
        return true;

    default:
        break;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING (ebay)

    return mngCollection.onMessage( pMessage ) ||
           mngRankings.onMessage( pMessage );
}
