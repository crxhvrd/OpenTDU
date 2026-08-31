#include "shared.h"
#include "gm_freeride.h"

#include "gs_timer.h"
#include "tdu_instance.h"

#include "database/gs_database.h"
#include "game/game_mechanism_player.h"
#include "game/gs_tutorial.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "flash/mng_fade.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/mng_flash_localize.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "player_data/driver.h"
#include "player_data/achievements_data.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "physics/vehicle/physics_vehicle.h"

static constexpr uint64_t kRewardBikeDealership = 0x5f2d20ull;
static constexpr uint64_t kRewardCarPaintDeluxe = 0x5da680ull;

static constexpr uint32_t kAchievementDBIndex = 0xfu;

GMFreeride::GMFreeride()
    : GameModeWorld( 6 ),
      mngBookmark(),
      mngFRMeetingIC(),
      mngPhotoMode(),
      mngCamGPS(),
      mngFontPlayers(),
      mngLogoDisplay(),
      mngMapScreenshot(),
      mngInGameMenu(),
      mngChat(),
      flashListSpots(),
      bResumedFromPauseMenu( false ),
      bEnterSpotClipVisible( false ),
      enterSpotClipTimer( 0.0f ),
      spotBank(),
      nextIntro(),
      pIntroCine( nullptr ),
      pSavedVehicleCamera( nullptr ),
      bIntroLevelLaunched( false ),
      bIntroCineDone( false ),
      pExtraChallenges{},
      numExtraChallenges( 0 ) {
    // FUN_00793880
    if ( gpMngMapScreenshot == nullptr ) {
        gpMngMapScreenshot = &mngMapScreenshot;
    }

    registerManager( &mngBookmark );
    registerManager( &mngFRMeetingIC );
    registerManager( &mngPhotoMode );
    registerManager( &mngFontPlayers );
    registerManager( &mngLogoDisplay );

    if ( gbMapScreenshotsMngEnabled ) {
        registerManagerFirst( &mngMapScreenshot );
    }

    registerManager( &mngInGameMenu );
    registerManager( &mngCamGPS );
    registerManager( &mngChat );

    mngFlash.setWantsAsyncInstall( true );
    mngBookmark.setMngFlash( &mngFlash );

    // Player-car game mechanic (overrides the GameModeWorld defaults).
    gameMechType = 4;    // player car
    gameMechID = 0xc0c1; // GMFreeRide-specific mechanic id

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC

    // When returning from the map screen, use a fast single-tick transition.
    if ( gpTestDriveInstance != nullptr &&
         gpTestDriveInstance->getPreviousGameMode() == GM_Map ) {
        transitionFlags = 0x11;
        numTicksPerTransition = 1;
    }
}

GMFreeride::~GMFreeride() {
    // FUN_00793750
    // Restore the game speed that was saved when the pause menu was opened.
    if ( gpTimer != nullptr ) {
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
    }
}

void GMFreeride::initialize() {
    // FUN_00797340
    if ( gpScenaric != nullptr ) {
        gpScenaric->getSpotList().spawnPendingSectorSpotMeshes();
    }

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC / FLASH (ad banners)

    if ( gpScenaric != nullptr ) {
        nextIntro = gpScenaric->getNextIntro();
        gpScenaric->getNextIntro().bIsActive = false;
    }

    if ( nextIntro.bIsActive && nextIntro.NextGameMode == GM_FreeRide ) {
        if ( nextIntro.pSpot == nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (FUN_007fcdc0 — load
                                     // NextIntro.pPath/pFilename as a package)
        } else {
            Spots &spots = gpScenaric->getSpotList();
            resolveNextIntroCine(
                spots.getSpotSector( nextIntro.pSpot->Hashcode ) );

            const Eigen::Vector4f spotPosition( nextIntro.pSpot->X,
                                                nextIntro.pSpot->Y,
                                                nextIntro.pSpot->Z, 1.0f );
            gpWorld->setPlayerPosition( &spotPosition );
            gpScenaric->getLocalPlayer().setLastPosition( spotPosition );
        }

        if ( pIntroCine == nullptr ) {
            nextIntro.bIsActive = false;
        }
    }

    if ( gpScenaric != nullptr ) {
        gpScenaric->setICEnabled( false );
    }

    GameModeWorld::initialize();
}

void GMFreeride::resolveNextIntroCine( const SpotSector *pSector ) {
    // FUN_00797340 (inlined)
    pIntroCine = FindStreamedSpotCinematic( pSector, nextIntro.pCinematicName );
    if ( pIntroCine == nullptr ) {
        return;
    }

    pIntroCine->setGameModeInterface( this );
    pIntroCine->setStopWhenSkipped( true );

    gpWorld->setAreaStreamingEnabled( false );
}

void GMFreeride::reset() {
    // FUN_00796830
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (P2P free-ride manager) / GSAI (cop and
                        // bot pools, infringement transfer)

    int32_t spawnMode = 6;
    switch ( gpTestDriveInstance->getPreviousGameMode() ) {
    case GM_BootMenu:
    case GM_Login:
        spawnMode = 8;
        break;
    case GM_Map:
    case GM_Mission:
    case GM_GameModeEdit:
    case GM_InstantChallenge:
    case GM_Estates:
        spawnMode = 7;
        break;
    default:
        spawnMode = 6;
        break;
    }
    GameModeWorld::reset();

    mngActor.placePlayerVehicle( spawnMode, true );

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC/TUTORIAL
    gatherExtraChallenges();

    OTDU_UNIMPLEMENTED; // TODO: CAMERA (map-exit hand-over) / PHYSICS
                        // (postponed pause) / WORLD (sector draw toggle)
}

void GMFreeride::tick( float deltaTime, float totalTime ) {
    // FUN_00796260
    if ( pIntroCine == nullptr ) {
        if ( nextIntro.bIsActive && nextIntro.NextGameMode == GM_FreeRide &&
             nextIntro.pSpot != nullptr ) {
            Spots &spots = gpScenaric->getSpotList();
            const SpotSector *pSector =
                spots.getSpotSector( nextIntro.pSpot->Hashcode );
            if ( pSector != nullptr ) {
                resolveNextIntroCine( pSector );

                const Eigen::Vector4f spotPosition( nextIntro.pSpot->X,
                                                    nextIntro.pSpot->Y,
                                                    nextIntro.pSpot->Z, 1.0f );
                gpWorld->setPlayerPosition( &spotPosition );
                gpScenaric->getLocalPlayer().setLastPosition( spotPosition );
            }
        }
    } else if ( ( pIntroCine->getFlags() & 0x5u ) == 0 && nextIntro.bIsActive &&
                nextIntro.NextGameMode == GM_FreeRide ) {
        if ( ( transitionFlags & 1 ) == 0 && bFlashInstalled ) {
            if ( gpMngFade != nullptr ) {
                gpMngFade->restartTransition(
                    ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                    transitionTime * 0.001f );
            }

            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (postponed pause) /
                                     // VEHICLE (show the display vehicle, bump
                                     // the driver's counter)

            pSavedVehicleCamera = gpCamera->getActiveCamera();
            if ( nextIntro.pSpot == nullptr ) {
                pIntroCine->start( true );
            } else {
                pIntroCine->launchSpotCine( nextIntro.pSpot, 0.0f );
            }
        } else if ( gpMngFade != nullptr ) {
            gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() |
                                           8u );
        }
    }

    GameModeWorld::tick( deltaTime, totalTime );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (pending spot warp / spot cine
                             // launch) / CAMERA (map return) / MngFade
}

void GMFreeride::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007941f0
    if ( gpMngCinematics != nullptr &&
         gpMngCinematics->findFocusedPlayingCinematic() != nullptr ) {
        enterSpotClipTimer = 0.0f;
        return;
    }

    FlashPlayer *pPlayer = mngHudChallenge.getHudPlayer();

    enterSpotClipTimer +=
        ( gpTimer != nullptr ) ? gpTimer->GameDeltaTime : 0.0f;

    if ( !canEnterSpot() ||
         ( gpTimer != nullptr && gpTimer->GameSpeed <= 0.0f ) ) {
        enterSpotClipTimer = kEnterSpotClipDelay;

        if ( bEnterSpotClipVisible && pPlayer != nullptr ) {
            pPlayer->showFrame( "out", "/HUD/enter", true );
            bEnterSpotClipVisible = false;
        }
        return;
    }

    if ( bEnterSpotClipVisible ) {
        return;
    }

    FlashClip clip;
    if ( pPlayer == nullptr || !pPlayer->getClipPlayer( "/HUD/enter", &clip ) ||
         enterSpotClipTimer <= kEnterSpotClipDelay ) {
        return;
    }

    clip.setProperty( eFlashProperty::FP_Visible, true );

    pPlayer->setVariableValue( "/:WhatSpotType",
                               pGameMech->getDisplayTypeName() );
    pPlayer->setVariableValue( "/:WhatSpot", pGameMech->getDisplayName() );

    populateEnterSpotPrompt( pPlayer );
    markCurrentSpotDiscovered();

    pPlayer->showFrame( "on", "/HUD/enter", true );
    bEnterSpotClipVisible = true;
}

static void SetSpotLockedTextFromReward( FlashPlayer *pPlayer,
                                         const uint64_t rewardHashcode ) {
    // FUN_00793b50 (inlined)
    if ( gpDatabase == nullptr || gpMngFlashLocalize == nullptr ) {
        return;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const DBEntryAchievement *pEntries = gameDB.DBAchivement;
    if ( pEntries == nullptr ) {
        return;
    }

    const DBEntryList &achievementDB = gameDB.Databases[kAchievementDBIndex];
    const uint32_t numEntries = gameDB.pNumItems[kAchievementDBIndex];
    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        if ( achievementDB.getHashcodeForPointer(
                 pEntries[i].Reward.pAsRawPointer ) != rewardHashcode ) {
            continue;
        }

        gpMngFlashLocalize->setVarLocalized(
            pPlayer, pEntries[i].FailedExplanation.pAsString,
            "/:SpotLockedtext" );
    }
}

static CarDesc *GetCurrentGarageCar() {
    // FUN_008978d0
    if ( gpPlayerData == nullptr || gpPlayerData->getLoadedSave() == nullptr ) {
        return nullptr;
    }

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    Driver *pDriver = pSave->getDriver();
    Garage *pGarage = pSave->getGarage();
    if ( pDriver == nullptr || pGarage == nullptr ||
         pDriver->CurrentCar >= Garage::kNumCarSlots ) {
        return nullptr;
    }

    return &pGarage->pCars[pDriver->CurrentCar];
}

static bool IsSpotRewardLocked( const uint64_t rewardHashcode ) {
    // FUN_00793b50 (inlined)
    if ( gpPlayerData == nullptr || gpPlayerData->getLoadedSave() == nullptr ) {
        return false;
    }

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    return pAchievements != nullptr &&
           pAchievements->isRewardLocked( rewardHashcode );
}

void GMFreeride::populateEnterSpotPrompt( FlashPlayer *pPlayer ) {
    // FUN_00793b50
    if ( pPlayer == nullptr ) {
        return;
    }

    pPlayer->setVariableValue( "/:reward_type", 0 );
    pPlayer->setVariableValue( "/:IsChaLocked", 0 );
    pPlayer->setVariableValue( "/:IsSpotLocked", 0 );

    SpotMap *pSpot =
        ( pGameMech != nullptr ) ? pGameMech->getCurrentSpot() : nullptr;
    if ( pSpot == nullptr ) {
        refreshChallengeLockState( pPlayer );
        return;
    }

    CarDesc *pCurrentCar = GetCurrentGarageCar();
    DBEntryCarPhysicsData *pCurrentCarData = nullptr;
    if ( pCurrentCar != nullptr && gpDatabase != nullptr ) {
        pCurrentCarData =
            gpDatabase->getCarDBEntry( pCurrentCar->getCarConfigID() );
    }

    const bool bRentedCar = pCurrentCar != nullptr &&
                            pCurrentCar->state == CarDesc::eState::CDS_Rental;

    // Bike dealerships
    if ( pSpot->Type == 2 && IsSpotRewardLocked( kRewardBikeDealership ) ) {
        pPlayer->setVariableValue( "/:reward_type", 1 );
        pPlayer->setVariableValue( "/:SpotLockedtext",
                                   "_No idea how to unlock..." );
        SetSpotLockedTextFromReward( pPlayer, kRewardBikeDealership );
        refreshChallengeLockState( pPlayer );
        return;
    }

    if ( pSpot->Type == ST_Unknown8 ) {
        if ( pSpot->SubType == 9 &&
             IsSpotRewardLocked( kRewardCarPaintDeluxe ) ) {
            pPlayer->setVariableValue( "/:reward_type", 1 );
            pPlayer->setVariableValue( "/:SpotLockedtext",
                                       "_No idea how to unlock..." );
            SetSpotLockedTextFromReward( pPlayer, kRewardCarPaintDeluxe );
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( ( pSpot->SubType == 9 || pSpot->SubType == 1 ) && bRentedCar ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x330bd32ull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( pSpot->SubType == 9 && gpDatabase != nullptr &&
             gpDatabase->isBike( pCurrentCarData ) ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x3310f3aull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }

        if ( pSpot->SubType == 9 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: DATABASE
        }

        if ( pSpot->SubType == 1 && pCurrentCar != nullptr &&
             gpDatabase != nullptr &&
             gpDatabase->getNumColors( pCurrentCar->getCarConfigID() ) < 2 ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x22d50512ull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }
    }

    // Tuning
    if ( pSpot->Type == 4 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: DATABASE

        if ( bRentedCar ) {
            pPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            if ( gpMngFlashLocalize != nullptr ) {
                gpMngFlashLocalize->setVarLocalized(
                    pPlayer, "/:SpotLockedtext", 0x3778a32ull );
            }
            refreshChallengeLockState( pPlayer );
            return;
        }
    }

    // Challenge
    if ( pSpot->Type == ST_Challenge ) {
        bool bCarOK = true;
        bool bChallengeLocked = false;

        bool bLevelTooLow = false;
        bool bHasEligibleCar = false;
        GetChallengeCarAvailability( pSpot, &bLevelTooLow, &bHasEligibleCar );

        bChallengeLocked = !bHasEligibleCar;
        if ( bChallengeLocked ) {
            bCarOK = false;
        }
        if ( bLevelTooLow ) {
            bChallengeLocked = true;
        }

        pPlayer->setVariableValue( "/:IsCarOK", bCarOK ? 1 : 0 );
        pPlayer->setVariableValue( "/:IsChaLocked", bChallengeLocked ? 1 : 0 );
    }

    refreshChallengeLockState( pPlayer );
}

void GMFreeride::refreshChallengeLockState( FlashPlayer *pPlayer ) {
    // FUN_00792f30
    Entity *pExtraChallenge = ( pGameMech != nullptr )
                                  ? pGameMech->getCurrentExtraChallenge()
                                  : nullptr;
    if ( pExtraChallenge == nullptr ) {
        return;
    }

    (void)pPlayer;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC/PLAYER_DATA
}

void GMFreeride::markCurrentSpotDiscovered() {
    // FUN_007941c0
    SpotMap *pSpot =
        ( pGameMech != nullptr ) ? pGameMech->getCurrentSpot() : nullptr;
    if ( pSpot == nullptr || gpPlayerData == nullptr ||
         gpPlayerData->getLoadedSave() == nullptr ) {
        return;
    }

    if ( gpPlayerData->getLoadedSave()->getRoadDiscovery() == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYER_DATA
}

void GMFreeride::pause() {
    // FUN_00794900
    OTDU_UNIMPLEMENTED; // TODO: GSAI (bot/cop pools) / PLAYER_DATA (rental
                        // fee) / SCENARIC

    if ( gpTutorial != nullptr ) {
        gpTutorial->bEnabled = false;
    }

    GameModeWorld::pause();
}

void GMFreeride::destroy() {
    // FUN_00794860
    GameModeWorld::destroy();

    OTDU_UNIMPLEMENTED; // TODO: GSAI (bots/cops) / SCENARIC (spot cine slot
                        // release) / FLASH (ad banners)

    const ScenaricBank::eBankState bankState = spotBank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed && gpScenaric != nullptr ) {
        gpScenaric->unloadBank( &spotBank );
    }
}

void GMFreeride::onResourceStreamed() {
    // FUN_007940f0
    GameModeWorld::onResourceStreamed();

    if ( mngHudChallenge.getHudPlayer() == nullptr ) {
        mngHudChallenge.initializeFlash();
    }

    FlashPlayer *pPlayer = mngHudChallenge.getHudPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    mngBookmark.setFlashPlayer( pPlayer );
    mngFRMeetingIC.setFlashPlayer( pPlayer );
    mngInGameMenu.setFlashPlayer( pPlayer );

    if ( gpMngFlashLocalize != nullptr ) {
        mngInGameMenu.refreshDrivingHelpLabel();
    }

    pPlayer->setVariableValue(
        "/:nb_houses",
        gpPlayerData->getLoadedSave()->getGarage()->getNumOwnedHouses() );

    mngFlash.addWidget( "spot", &flashListSpots );
    flashListSpots.setPlayer( pPlayer, "/menus/ss_menus/array/spots" );
    flashListSpots.setFlashPlayer( pPlayer );

    if ( gpTutorial != nullptr ) {
        gpTutorial->bEnabled = true;
    }

    mngPhotoMode.setFlashPlayer( pPlayer );
}

bool GMFreeride::executeAction( int32_t param_2, void * /*param_3*/ ) {
    // FUN_00794ff0
    if ( param_2 != CE_Ended ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC / MngFade / PHYSICS /
                                 // DATABASE (CE_Started, CE_StartEndFade and
                                 // the flash-message case)
        return false;
    }

    if ( nextIntro.bIsActive ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL (FUN_007fcd40 — advance the
                                 // scenario tutorial past
                                 // STS_AirplaneCinematic, or relaunch the
                                 // current state's dialog)
        nextIntro.bIsActive = false;
        bIntroLevelLaunched = false;
        bIntroCineDone = true;
        gpWorld->setAreaStreamingEnabled( true );
    }

    if ( !bIntroLevelLaunched ) {
        gpCamera->restoreCamera();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI (wake the bot/cop pools and
                                 // unpause the traffic)
    }

    pIntroCine = nullptr;
    bIntroLevelLaunched = false;
    return true;
}

void GMFreeride::gatherExtraChallenges() {
    // FUN_00792e60
    numExtraChallenges = 0;

    if ( gpScenaric == nullptr ) {
        return;
    }

    ScenaricBank *pBank = gpScenaric->getWorldExtraChallengesBank();
    for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
        Entity *pEntity = pBank->getEntity( i );
        if ( pEntity == nullptr ) {
            continue;
        }

        const eEntityType type = pEntity->getType();
        if ( type != eEntityType::ET_SHOG && type != eEntityType::ET_HIHI &&
             type != eEntityType::ET_TRAN && type != eEntityType::ET_CVOY ) {
            continue;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYER_DATA — that per-challenge state
                                 // array is not ported.

        if ( !gbSimuModeEnabled ) {
            mngActor.registerActor( pEntity );
        }

        if ( numExtraChallenges < kMaxNumExtraChallenges ) {
            pExtraChallenges[numExtraChallenges] = pEntity;
            numExtraChallenges++;
        }
    }
}

bool GMFreeride::onMessage( FlashMessage *pMessage ) {
    // FUN_00796f40
    if ( GameModeWorld::onMessage( pMessage ) ||
         mngPhotoMode.onMessage( pMessage ) ||
         mngBookmark.onMessage( pMessage ) ) {
        return true;
    }

    if ( gpMngInGameMenu != nullptr &&
         gpMngInGameMenu->onMessage( pMessage ) ) {
        return true;
    }

    switch ( *pMessage ) {
    case FlashMessage( "inic" ):
    case FlashMessage( "goic" ):
    case FlashMessage( "icbo" ):
    case FlashMessage( "noic" ):
        return mngFRMeetingIC.onMessage( pMessage );
    default:
        break;
    }

    OTDU_UNIMPLEMENTED; // TODO: the "gogp"/"goho"/"home"/"gosh"/"gosp"/"goid"/
                        // "goch"/"acex"/"acti"/"caex"/"info"/"noch"/"ssmn"
                        // handlers (spot entry + extra challenges)

    return false;
}

void GMFreeride::onPauseMenuRequested( bool bOpened ) {
    // FUN_007934f0
    GameModeWorld::onPauseMenuRequested( bOpened );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

void GMFreeride::onMenuPauseExit( bool bRestoreState ) {
    // FUN_00793460
    GameModeWorld::onMenuPauseExit( bRestoreState );

    bResumedFromPauseMenu = true;

    OTDU_UNIMPLEMENTED; // TODO: ONLINE/PHYSICS
}

#ifdef OTDU_USE_IMGUI
void GMFreeride::drawDebug( float deltaTime ) {
    if ( gpDatabase != nullptr ) {
        const GameDatabase &gdb = gpDatabase->getGameDB();
        const int32_t numCars = static_cast<int32_t>( gdb.pNumItems[1] );
        if ( numCars > 0 && gdb.DBCarData != nullptr ) {
            auto &carList = gpDatabase->getCarList();

            CarState *pPaintTarget =
                ( gpScenaric != nullptr )
                    ? gpScenaric->getLocalPlayer().getCarState()
                    : nullptr;
            VehicleBase *pPaintVehicle =
                ( pPaintTarget != nullptr ) ? pPaintTarget->getVehicleInstance()
                                            : nullptr;
            const bool bCanRepaint =
                ( pPaintVehicle != nullptr &&
                  pPaintTarget->getCarID() == gRequestedPlayerCarID );

            static int32_t deluxeType = 0;
            static float deluxeMain[4] = { 0.8f, 0.1f, 0.1f, 1.0f };
            static float deluxeSecond[4] = { 0.1f, 0.1f, 0.8f, 1.0f };
            static float deluxeLogo[4] = { 0.9f, 0.9f, 0.9f, 1.0f };
            static float deluxeSecond2[4] = { 0.1f, 0.6f, 0.2f, 1.0f };
            const bool bDeluxe = ( deluxeType != 0 );

            auto applySelection = [&]() {
                if ( !bCanRepaint ) {
                    return;
                }

                auto toColor = []( const float *pRGBA ) {
                    return ColorRGBA( (uint8_t)( pRGBA[0] * 255.0f ),
                                      (uint8_t)( pRGBA[1] * 255.0f ),
                                      (uint8_t)( pRGBA[2] * 255.0f ),
                                      (uint8_t)( pRGBA[3] * 255.0f ) );
                };

                const ColorRGBA mainColor = toColor( deluxeMain );
                const ColorRGBA secondColor = toColor( deluxeSecond );
                const ColorRGBA logoColor = toColor( deluxeLogo );
                const ColorRGBA secondColor2 = toColor( deluxeSecond2 );

                pPaintVehicle->applyPaint(
                    (uint32_t)gRequestedPlayerCarID, (ePaintType)deluxeType,
                    gRequestedPlayerCarColorID, gRequestedPlayerCarRimID,
                    gRequestedPlayerCarInteriorID,
                    bDeluxe ? &mainColor : nullptr,
                    bDeluxe ? &secondColor : nullptr,
                    bDeluxe && pPaintVehicle->shouldUseDeluxeTwoTones()
                        ? &logoColor
                        : nullptr,
                    bDeluxe && pPaintVehicle->shouldUseDeluxeTwoTones()
                        ? &secondColor2
                        : nullptr );
            };

            ImGui::SeparatorText( "Vehicle" );
            if ( ImGui::BeginListBox( "##DbgVehicleList" ) ) {
                for ( auto &car : carList ) {
                    const bool bSelected =
                        ( car.Hash == gRequestedPlayerCarID );

                    std::string carIDAndName = std::to_string( car.Hash );
                    carIDAndName += " - ";
                    carIDAndName += car.FullName;

                    if ( ImGui::Selectable( carIDAndName.c_str(),
                                            bSelected ) ) {
                        gRequestedPlayerCarID = car.Hash;
                        gRequestedPlayerCarColorID = 0;
                        gRequestedPlayerCarRimID = -1;
                        gRequestedPlayerCarInteriorID = 0;
                        gRequestedCarChange = true;
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            const int32_t numColors =
                gpDatabase->getNumColors( gRequestedPlayerCarID );
            if ( gRequestedPlayerCarColorID >= numColors ) {
                gRequestedPlayerCarColorID = 0;
            }

            ImGui::SeparatorText( "Paint" );
            if ( ImGui::BeginListBox( "##DbgPaintList" ) ) {
                if ( ImGui::Selectable( "Deluxe Paint",
                                        gRequestedPlayerCarColorID == -1 ) ) {
                    gRequestedPlayerCarColorID = -1;
                    deluxeType = 1;
                    applySelection();
                }
                if ( gRequestedPlayerCarColorID == -1 )
                    ImGui::SetItemDefaultFocus();

                for ( int32_t i = 0; i < numColors; i++ ) {
                    auto color =
                        gpDatabase->getColorByIndex( gRequestedPlayerCarID, i );

                    const bool bSelected = ( gRequestedPlayerCarColorID == i );

                    const char *pName = color->ColorName.pAsString;
                    if ( ImGui::Selectable( ( pName != nullptr ) ? pName
                                                                 : "<unnamed>",
                                            bSelected ) ) {
                        deluxeType = 0;
                        gRequestedPlayerCarColorID = i;

                        applySelection();
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            if ( gRequestedPlayerCarColorID == -1 ) {
                static const char *kPaintTypeNames[] = { "None", "Default",
                                                         "Metal" };

                ImGui::SeparatorText( "Deluxe paint" );
                if ( ImGui::Combo( "##DbgDeluxePaintType", &deluxeType,
                                   kPaintTypeNames,
                                   IM_ARRAYSIZE( kPaintTypeNames ) ) ) {
                    applySelection();
                }

                if ( bDeluxe ) {
                    bool bColorChanged = false;
                    bColorChanged |= ImGui::ColorEdit4( "Primary", deluxeMain );
                    bColorChanged |=
                        ImGui::ColorEdit4( "Highlights", deluxeSecond );
                    if ( pPaintVehicle->shouldUseDeluxeTwoTones() ) {
                        bColorChanged |=
                            ImGui::ColorEdit4( "Secondary", deluxeLogo );
                        bColorChanged |= ImGui::ColorEdit4(
                            "Secondary Hightlights", deluxeSecond2 );
                    }

                    if ( bColorChanged ) {
                        applySelection();
                    }
                }
            }

            DBEntryCarColor *pSelectedColor =
                ( numColors > 0 && gRequestedPlayerCarColorID >= 0 )
                    ? gpDatabase->getColorByIndex( gRequestedPlayerCarID,
                                                   gRequestedPlayerCarColorID )
                    : nullptr;

            int32_t numInteriors = 0;
            while ( gpDatabase->getInteriorByIndex(
                        pSelectedColor, numInteriors ) != nullptr ) {
                numInteriors++;
            }
            if ( gRequestedPlayerCarInteriorID >= numInteriors ) {
                gRequestedPlayerCarInteriorID = 0;
            }

            ImGui::SeparatorText( "Interior" );
            if ( numInteriors == 0 ) {
                ImGui::TextDisabled( "No interior for this paint." );
            } else if ( ImGui::BeginListBox( "##DbgInteriorList" ) ) {
                for ( int32_t i = 0; i < numInteriors; i++ ) {
                    DBEntryInterior *pInterior =
                        gpDatabase->getInteriorByIndex( pSelectedColor, i );

                    const bool bSelected =
                        ( gRequestedPlayerCarInteriorID == i );
                    const char *pName = pInterior->InteriorName.pAsString;

                    if ( ImGui::Selectable( ( pName != nullptr ) ? pName
                                                                 : "<unnamed>",
                                            bSelected ) ) {
                        gRequestedPlayerCarInteriorID = i;
                        applySelection();
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }

            ImGui::SeparatorText( "Rims" );
            if ( ImGui::BeginListBox( "##DbgRimList" ) ) {
                if ( ImGui::Selectable( "Default",
                                        gRequestedPlayerCarRimID == -1 ) )
                    gRequestedPlayerCarRimID = -1;
                if ( gRequestedPlayerCarRimID == -1 )
                    ImGui::SetItemDefaultFocus();

                const int32_t numRims =
                    gpDatabase->getNumRims( gRequestedPlayerCarID );
                for ( int32_t i = 0; i < numRims; i++ ) {
                    DBEntryRim *pRim =
                        gpDatabase->getRim( gRequestedPlayerCarID, i );
                    if ( pRim == nullptr ) {
                        continue;
                    }

                    const bool bSelected = ( gRequestedPlayerCarRimID == i );
                    const char *pName = pRim->FriendlyName.pAsString;

                    if ( ImGui::Selectable( ( pName != nullptr ) ? pName
                                                                 : "<unnamed>",
                                            bSelected ) ) {
                        gRequestedPlayerCarRimID = i;
                        applySelection();
                    }

                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }
        }
    }

    ImGui::SeparatorText( "Teleport" );
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pCar == nullptr ) {
        ImGui::TextDisabled( "No local-player car spawned." );
    } else {
        // Absolute world position (row 3 of the body's world matrix).
        const Eigen::Vector4f cur = pCar->getBody()->WorldMatrix.row( 3 );
        ImGui::Text( "Current: (%.1f, %.1f, %.1f)", cur.x(), cur.y(), cur.z() );

        // Seed the input fields from the current position on first display.
        static float target[3] = { 0.0f, 0.0f, 0.0f };
        static bool bSeeded = false;
        if ( !bSeeded ) {
            target[0] = cur.x();
            target[1] = cur.y();
            target[2] = cur.z();
            bSeeded = true;
        }

        ImGui::SeparatorText( "Bookmarks" );
        if ( ImGui::Button( "Manoa Falls" ) ) {
            target[0] = 41464.73f;
            target[1] = 88.72f;
            target[2] = 25543.82f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Secret Island Teleporter" ) ) {
            target[0] = 63519.45f;
            target[1] = 223.50f;
            target[2] = 46305.20f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Oahu Raceway" ) ) {
            target[0] = 34569.15f;
            target[1] = 3.06f;
            target[2] = 45154.43f;
        }

        if ( ImGui::Button( "Secret Island" ) ) {
            target[0] = 64285.047f;
            target[1] = 37.758f;
            target[2] = 44133.129f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Japanese Temple" ) ) {
            target[0] = 44802.07f;
            target[1] = 88.87f;
            target[2] = 32675.04f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Observatory" ) ) {
            target[0] = 10643.63f;
            target[1] = 864.36f;
            target[2] = 23641.65f;
        }

        if ( ImGui::Button( "AI Trainer Sector" ) ) {
            target[0] = 9122.41f;
            target[1] = 0.65f;
            target[2] = 9847.77f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Tunnel" ) ) {
            target[0] = 50037.879f;
            target[1] = 356.729f;
            target[2] = 40454.828f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Diamond Head" ) ) {
            target[0] = 48442.840f;
            target[1] = 190.627f;
            target[2] = 52137.379f;
        }

        if ( ImGui::Button( "Over lighthouse car park" ) ) {
            target[0] = 548.4f;
            target[1] = 38.7f;
            target[2] = 17767.62f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Pearl City" ) ) {
            target[0] = 31846.57f;
            target[1] = 20.22f;
            target[2] = 37179.66f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "HMap LOD Bug Repro" ) ) {
            target[0] = 48413.344f;
            target[1] = 309.431f;
            target[2] = 41670.789f;
        }

        ImGui::SetNextItemWidth( 300.0f );
        ImGui::DragFloat3( "World XYZ", target, 1000.0f );
        if ( ImGui::Button( "Use current" ) ) {
            target[0] = cur.x();
            target[1] = cur.y();
            target[2] = cur.z();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Teleport" ) ) {
            gRequestedPlayerTeleportPos =
                Eigen::Vector3f( target[0], target[1], target[2] );
            gRequestedPlayerTeleport = true;
        }
    }
}
#endif
