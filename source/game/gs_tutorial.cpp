#include "shared.h"
#include "gs_tutorial.h"

#include "tdu_instance.h"
#include "tutorial_hint.h"
#include "audio/gs_audio.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "audio/implementation/ed_audio_sample.h"
#include "config/gs_config.h"
#include "core/ed_store.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "flash/implementation/flash_player.h"
#include "game/gm_world.h"
#include "game/mng_hud.h"
#include "game/freeride/gm_freeride.h"
#include "game/lobby/gm_lobby.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/shop/gm_shop.h"
#include "game/shop/shop_logic.h"
#include "game/vehicle/car_state.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "render/camera/gs_camera.h"

GSTutorial *gpTutorial = nullptr;
bool gbTutorialEnabled = true;         // DAT_00f77da8
bool gbScenarioTutorialEnabled = true; // DAT_00f8b208

static constexpr float kDefaultHintTimerCondition = 60.0f; // FLOAT_00f41744
static constexpr float kTimeCentToSeconds = 0.01f;         // DAT_00f416d4
static constexpr float kHintSearchDistance = 100.0f;       // DAT_00f41708
static constexpr float kHintMaxDistanceSq = 22500.0f;      // DAT_00f41cd8
static constexpr float kHintMaxAngle = 0.62831855f;        // DAT_00f41cd4
static constexpr int32_t kNumHelpTips = 0x20;
static constexpr int32_t kFreeRideHelpContext = 0xe;
static constexpr uint64_t kClubMemberTipHashcode = 0x56b65full;
static constexpr const char *kTutorialClientName = "ClientVoiceTutorial";

static const char *GetHelpTipName( const int32_t context ) {
    // FUN_00980fb0
    static constexpr const char *kHelpTips[kNumHelpTips] = {
        "tip_tuner",    "tip_shop",     "tip_",        "tip_cardeal",
        "tip_club1",    "tip_paintstd", "tip_carrent", "tip_",
        "tip_drive",    "tip_",         "tip_realt",   "tip_time",
        "tip_speed",    "tip_race",     "tut_fr4_pc",  "tip_",
        "tip_",         "tip_",         "tip_",        "tut_map2",
        "tip_",         "tip_",         "tip_",        "tut_home4",
        "tip_paintlux", "tip_carclas",  "tip_club2",   "tip_raceelim",
        "tut_home5",    "tip_speed2",   "tip_drivIn1", "tip_drivIn2" };

    if ( context != -1 && context < kNumHelpTips ) {
        return kHelpTips[context];
    }

    return nullptr;
}

static int32_t GetShopHelpContext( const SpotMap *pSpot ) {
    // FUN_009812c0 (inlined)
    switch ( pSpot->Type ) {
    case ST_House:
        return 9;
    case ST_CarDealer:
        if ( gpActiveGameMode->getCurrentGameMode() == GM_Shop ) {
            ShopLogic *pShopLogic =
                static_cast<GMShop *>( gpActiveGameMode )->getShopLogic();
            if ( pShopLogic != nullptr && pShopLogic->usesClassicCarStock() ) {
                return 0x19;
            }
        }
        return 3;
    case ST_BikeDealer:
        return 2;
    case ST_Goodies:
        return 1;
    case ST_AfterMarket:
        return 0;
    case ST_Club: {
        Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
        if ( pDriver->TeamID == 0u ) {
            return 4;
        }

        if ( pDriver->TeamSpotID == uint32_t( pSpot->Hashcode ) &&
             pDriver->field4_0x24 == uint32_t( pSpot->Hashcode >> 32 ) ) {
            return 0x1a;
        }
        return -1;
    }
    case ST_DriveIn:
        return 8;
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 0:
            return 7;
        case 1:
            return 5;
        case 2:
        case 3:
            return 6;
        case 4:
            return 10;
        case 9:
            return 0x18;
        default:
            return -1;
        }
    default:
        return -1;
    }
}

static int32_t GetLobbyHelpContext() {
    // FUN_009812c0 (inlined)
    if ( static_cast<GMLobby *>( gpActiveGameMode )->isSpectatorMode() ) {
        return -1;
    }

    EventParameters &eventParameters = gpScenaric->getSelectedEventParameters();
    switch ( eventParameters.getGameTypeParameter().getValue() ) {
    case 0:
        return 0xb;
    case 1:
        return ( eventParameters.getStartChallenge().getValue() != 0 ) ? 0xc
                                                                       : 0x1d;
    case 3:
        if ( eventParameters.getLoop().getValue() > 0 &&
             eventParameters.getNumLaps().getValue() == 0 ) {
            return 0x1b;
        }
        return 0xd;
    default:
        return -1;
    }
}

static int32_t GetHelpContext( const int32_t context ) {
    // FUN_009812c0
    if ( gpActiveGameMode == nullptr ) {
        return -1;
    }

    const SpotMap *pSpot = nullptr;
    const int32_t currentSpotIndex = gpScenaric->getCurrentSpotIndex();
    if ( currentSpotIndex != -1 ) {
        pSpot =
            gpScenaric->getSpotList().getWorldSpotChecked( currentSpotIndex );
    }

    switch ( gpActiveGameMode->getCurrentGameMode() ) {
    case GM_FreeRide:
        return kFreeRideHelpContext;
    case GM_Map:
        return ( context == 1 ) ? 0x14 : 0x13;
    case GM_Garage:
        return 0x17;
    case GM_Living:
        return 9;
    case GM_DriveIn:
        if ( context == 1 ) {
            return 0x1e;
        }
        if ( context == 2 ) {
            return 0x1f;
        }
        return -1;
    case GM_Dressing:
        return 0x1c;
    case GM_Estates:
        if ( pSpot == nullptr || pSpot->Type == ST_House ) {
            return -1;
        }
        return GetShopHelpContext( pSpot );
    case GM_Shop:
    case GM_Goodies:
    case GM_AfterMarket:
    case GM_Club:
        return ( pSpot != nullptr ) ? GetShopHelpContext( pSpot ) : -1;
    case GM_Lobby:
        return GetLobbyHelpContext();
    default:
        return -1;
    }
}

static bool IsInHintRange( const Eigen::Vector4f &position,
                           float *pOutDistance ) {
    // FUN_00981e90
    const Matrix4x4f_t &carMatrix =
        gpScenaric->getLocalPlayer().getCarState()->getBody()->WorldMatrix;

    Eigen::Vector4f delta = position - Eigen::Vector4f( carMatrix.row( 3 ) );
    delta.y() = 0.0f;

    const float distanceSq = delta.squaredNorm();
    if ( kHintMaxDistanceSq <= distanceSq ) {
        return false;
    }

    const float distance = std::sqrt( distanceSq );
    *pOutDistance = distance;

    const Eigen::Vector4f direction = delta / distance;
    const float cosAngle = Clamp(
        direction.dot( Eigen::Vector4f( carMatrix.row( 2 ) ) ), -1.0f, 1.0f );
    return std::abs( std::acos( cosAngle ) ) < kHintMaxAngle;
}

static FlashPlayer *GetGeneralMoviePlayer() {
    MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, 0 );
    if ( pMovie == nullptr || !pMovie->MovieBank.isLoaded() ) {
        return nullptr;
    }

    return pMovie->MovieBank.getFlashPlayer();
}

GSTutorial::GSTutorial()
    : GameSystem(),
      bEnabled( false ),
      bPopupDisplayed( false ),
      pSoundBuffer( nullptr ),
      SoundBufferSize( 0u ),
      pSoundSample(),
      pInstallGuard( std::make_shared<bool>( true ) ),
      pAudioClient( nullptr ),
      pSoundInstance( nullptr ),
      bLoadingSound( false ) {
    // FUN_00981790 / FUN_00981210
    gpTutorial = this;
}

GSTutorial::~GSTutorial() { terminate(); }

bool GSTutorial::initialize( TestDriveGameInstance * ) {
    // FUN_00982080
    terminate();

    if ( gpDatabase == nullptr ) {
        return true;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numEntries = gameDB.pNumItems[kTutorialDatabaseIndex];
    if ( gameDB.DBTutorial != nullptr ) {
        for ( uint32_t i = 0u; i < numEntries; i++ ) {
            const DBEntryTutorial &entry = gameDB.DBTutorial[i];

            TutorialHint *pMessage = CreateTutorialHint(
                GetTutorialHintID( entry.TextID.pAsString ) );
            if ( pMessage == nullptr ) {
                continue;
            }

            pMessage->TimerCondition =
                ( entry.TimeCent > 0 )
                    ? float( entry.TimeCent ) * kTimeCentToSeconds
                    : kDefaultHintTimerCondition;
            pMessage->DBLine = int32_t( i );

            if ( numTutorialMsgs >= kMaxTutorialHints ) {
                OTDU_LOG_WARN( "More than %d tutorial message(s) in the "
                               "database; the rest is dropped\n",
                               kMaxTutorialHints );
                delete pMessage;
                break;
            }

            pTutorialHints[numTutorialMsgs] = pMessage;
            numTutorialMsgs++;
        }
    }

    OTDU_LOG_DEBUG( "Built %d tutorial message(s) out of %u database row(s)\n",
                    numTutorialMsgs, numEntries );

    pAudioClient = ( gpAudio != nullptr )
                       ? gpAudio->createClient( kTutorialClientName )
                       : nullptr;
    if ( pAudioClient != nullptr &&
         pAudioClient->getStereoPipeline() != nullptr ) {
        pSoundInstance = new edAudioStreamEntry();
        pAudioClient->getStereoPipeline()->addStereoEntry( pSoundInstance );
        pSoundInstance->debugSetDescription( "Tutorial Instance" );
    }

    return true;
}

void GSTutorial::tick( float totalTime, float deltaTime ) {
    // FUN_00982430
    if ( bEnabled && !gbTutorialEnabled && gpMngHud != nullptr &&
         gpFlash->FUN_0099b240( "HUD", 3 ) &&
         !gpScenaric->isDemoModeActive() ) {
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar != nullptr &&
             gpCamera->getActiveVehicleCamera() != nullptr &&
             !pLocalCar->getPoliceState().isArrested() ) {
            if ( gpPlayerData->getLocalPlayerData().bHelpSystem ) {
                updateHints( deltaTime );
            } else {
                for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
                    TutorialHint *pHint = pTutorialHints[i];
                    if ( pHint != nullptr && pHint->bOnScreen ) {
                        hideHint( pHint );
                    }
                }
            }
        }
    }

    if ( pSoundInstance != nullptr && pSoundInstance->isStopped() &&
         pSoundBuffer != nullptr && !bLoadingSound ) {
        releaseVoice();
    }
}

void GSTutorial::updateHints( const float deltaTime ) {
    // FUN_00982430 (inlined)
    SpotSector *pNearestSector = nullptr;
    Entity *pNearestExtraChallenge = nullptr;
    float nearestDistance = kHintSearchDistance;

    const Spots &spotList = gpScenaric->getSpotList();
    for ( int32_t sectorIndex = 0; sectorIndex < Spots::kNumSectorLists;
          sectorIndex++ ) {
        const SpotsList &sectorList = spotList.getSectorList( sectorIndex );
        for ( uint32_t i = 0u; i < sectorList.NumSpots; i++ ) {
            SpotSector *pSector = &sectorList.pSpotsMapOrSector.pSector[i];
            if ( IsSpotHidden( pSector->Hashcode ) ) {
                continue;
            }

            float distance = 0.0f;
            if ( IsInHintRange( Eigen::Vector4f( pSector->X, pSector->Y,
                                                 pSector->Z, 1.0f ),
                                &distance ) &&
                 distance < nearestDistance ) {
                pNearestSector = pSector;
                nearestDistance = distance;
            }
        }
    }

    if ( gpActiveGM != nullptr && !gpScenaric->getIntroTutorial().isActive() &&
         gpTestDriveInstance->getActiveGameMode() == GM_FreeRide ) {
        EntityExtraChallenge *pChallenge =
            static_cast<GMFreeride *>( gpActiveGM )->getNearestExtraChallenge();
        float distance = 0.0f;
        if ( pChallenge != nullptr &&
             IsInHintRange( pChallenge->getModelMatrix().row( 3 ),
                            &distance ) &&
             distance < nearestDistance ) {
            pNearestExtraChallenge = pChallenge;
            pNearestSector = nullptr;
        }
    }

    SpotMap *pNearestSpot = nullptr;
    if ( pNearestSector != nullptr ) {
        pNearestSpot =
            gpScenaric->getSpotList().getSpotMap( pNearestSector->Hashcode );
        if ( pNearestSpot != nullptr &&
             ( gpScenaric->getIntroTutorial().hidesSpot( pNearestSpot ) ||
               !IsSpotAvailableInCurrentMode( pNearestSpot ) ) ) {
            pNearestSpot = nullptr;
        }
    }

    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        TutorialHint *pHint = pTutorialHints[i];
        if ( pHint == nullptr || ( pHint->bCancel && !pHint->bOnScreen ) ) {
            continue;
        }

        pHint->update( pNearestSpot, pNearestExtraChallenge );

        if ( pHint->bSeen ) {
            if ( pHint->canRearm() ) {
                pHint->bSeen = false;
            }
            continue;
        }

        if ( pHint->bOnScreen ) {
            pHint->CurrentTimer += deltaTime;
            if ( pHint->TimerOnScreen < pHint->CurrentTimer ||
                 ( gpActiveGM != nullptr && gpActiveGM->isPaused() ) ) {
                hideHint( pHint );
            }

            if ( pHint->bCancel ) {
                continue;
            }
        } else if ( pHint->isConditionMet() ) {
            pHint->CurrentTimer += deltaTime;
            if ( pHint->TimerCondition < pHint->CurrentTimer &&
                 pHint->canDisplay() && !bFlashPopupOn &&
                 pSoundBuffer == nullptr ) {
                showHint( pHint );
                continue;
            }
        } else {
            pHint->CurrentTimer = 0.0f;
        }

        if ( pHint->isCompleted() ) {
            pHint->bCancel = true;
        }
    }
}

void GSTutorial::pause() {
    // FUN_00982040
    if ( !bPopupDisplayed ) {
        return;
    }

    FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pGeneral != nullptr ) {
        pGeneral->showFrame( "close", "/tutorial", false );
    }
}

void GSTutorial::terminate() {
    // FUN_009821d0
    if ( pSoundInstance != nullptr ) {
        pSoundInstance->stop( 0.0f );
        if ( pAudioClient != nullptr &&
             pAudioClient->getStereoPipeline() != nullptr ) {
            pAudioClient->getStereoPipeline()->removeStereoEntry(
                pSoundInstance );
        }
        delete pSoundInstance;
        pSoundInstance = nullptr;
    }

    if ( pAudioClient != nullptr && gpAudio != nullptr ) {
        gpAudio->releaseClient( pAudioClient );
    }
    pAudioClient = nullptr;

    if ( bLoadingSound && gpFile != nullptr &&
         !gpFile->cancelAsyncRequest( this ) ) {
        // The worker thread is still writing into the buffer; leak it rather
        // than free memory that is in use.
        pSoundBuffer = nullptr;
    }
    pInstallGuard = std::make_shared<bool>( true );
    pSoundSample.reset();
    if ( pSoundBuffer != nullptr ) {
        TestDrive::Free( pSoundBuffer );
        pSoundBuffer = nullptr;
    }
    bLoadingSound = false;

    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        delete pTutorialHints[i];
        pTutorialHints[i] = nullptr;
    }

    numTutorialMsgs = 0;
}

void GSTutorial::resetProgress() {
    // FUN_009810e0
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        TutorialHint *pMessage = pTutorialHints[i];
        if ( pMessage != nullptr ) {
            pMessage->bCancel = false;
            pMessage->bSeen = false;
            pMessage->CurrentTimer = 0.0f;
        }
    }

    memset( bHelpLaunched, 0, sizeof( bHelpLaunched ) );
}

void GSTutorial::onMapRequested() {
    // FUN_00981140
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        if ( pTutorialHints[i] != nullptr ) {
            pTutorialHints[i]->onMapRequested();
        }
    }
}

void GSTutorial::onSpotLaunched( SpotMap *pSpot ) {
    // FUN_00981710
    if ( gpScenaric->getIntroTutorial().isActive() ) {
        return;
    }

    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        if ( pTutorialHints[i] != nullptr ) {
            pTutorialHints[i]->onSpotLaunched( pSpot );
        }
    }
}

void GSTutorial::onExtraChallengeLaunched( Entity *pExtraChallenge ) {
    // FUN_009816d0
    if ( gpScenaric->getIntroTutorial().isActive() ) {
        return;
    }

    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        if ( pTutorialHints[i] != nullptr ) {
            pTutorialHints[i]->onExtraChallengeLaunched( pExtraChallenge );
        }
    }
}

void GSTutorial::displayHelp( FlashPlayer *pPlayer, const int32_t context ) {
    // FUN_009827b0
    if ( gpScenaric->getIntroTutorial().isActive() ) {
        return;
    }

    FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pGeneral == nullptr || pPlayer != GetGeneralMoviePlayer() ||
         pGeneral->getVariableAsInt( "/:IsTutorialOpen" ) == 1 ) {
        return;
    }

    pPlayer->setVariableValue( "/:IsHelpSystem", int32_t( 0 ) );

    const int32_t helpContext = GetHelpContext( context );
    const bool bValidContext =
        helpContext >= 0 && helpContext < kNumHelpContexts;
    if ( helpContext == kFreeRideHelpContext ) {
        if ( bHelpLaunched[kFreeRideHelpContext] ) {
            return;
        }
    } else if ( helpContext == -1 ) {
        return;
    }

    if ( ( !bValidContext || bHelpLaunched[helpContext] ) &&
         !gpPlayerData->getLocalPlayerData().bHelpSystem ) {
        return;
    }

    if ( bValidContext ) {
        bHelpLaunched[helpContext] = true;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numEntries = gameDB.pNumItems[kTutorialDatabaseIndex];
    const char *pTipName = GetHelpTipName( helpContext );
    pPlayer->setVariableValue( "/:IsHelpSystem", int32_t( 1 ) );
    if ( gameDB.DBTutorial == nullptr || pTipName == nullptr ||
         pTipName[0] == '\0' ) {
        return;
    }

    for ( uint32_t i = 0u; i < numEntries; i++ ) {
        const DBEntryTutorial &entry = gameDB.DBTutorial[i];
        if ( strcmp( entry.TextID.pAsString, pTipName ) != 0 ) {
            continue;
        }

        int32_t userData = 0;
        if ( helpContext == 3 ) {
            userData = 1;
        } else if ( helpContext == 2 ) {
            userData = 2;
        }

        const char *pMessage =
            ( strcmp( pTipName, "tip_club2" ) == 0 )
                ? gpDatabase->getStringByHashcode( kTutorialDatabaseIndex,
                                                   kClubMemberTipHashcode )
                : entry.Libelle.pAsString;
        TestDrive::DisplayModalPrompt(
            false, 0ull, kTutorialDatabaseIndex, nullptr, nullptr, nullptr,
            userData, pMessage, true, false, MPT_Tutorial, gpFlashMessageBox );

        if ( entry.File.pAsString != nullptr &&
             strncmp( entry.File.pAsString, "none", 5 ) != 0 ) {
            loadVoice( entry.File.pAsString );
        }
        return;
    }
}

void GSTutorial::stopVoice() {
    // FUN_009811e0
    if ( pSoundInstance != nullptr && !pSoundInstance->isStopped() ) {
        pSoundInstance->stop( 0.0f );
    }
}

TutorialHint *GSTutorial::findMessage( const uint32_t id ) {
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        TutorialHint *pMessage = pTutorialHints[i];
        if ( pMessage != nullptr && pMessage->ID == id ) {
            return pMessage;
        }
    }

    return nullptr;
}

void GSTutorial::deserialize( edStore &stream ) {
    // FUN_00981630
    for ( int32_t i = 0; i < kMaxTutorialHints; i++ ) {
        const uint32_t id = uint32_t( stream.readInteger() );

        TutorialHint *pMessage = findMessage( id );
        if ( pMessage == nullptr ) {
            stream.readInteger();
            stream.readInteger();
            continue;
        }

        pMessage->bCancel = stream.readInteger() != 0;
        pMessage->bSeen = stream.readInteger() != 0;
    }

    for ( bool &bLaunched : bHelpLaunched ) {
        bLaunched = stream.readByte() != 0u;
    }
}

void GSTutorial::serialize( edStore &stream ) const {
    // FUN_009815b0
    for ( int32_t i = 0; i < kMaxTutorialHints; i++ ) {
        const TutorialHint *pMessage = pTutorialHints[i];
        if ( pMessage == nullptr ) {
            stream.writeInteger( 0 );
            stream.writeInteger( 0 );
            stream.writeInteger( 0 );
            continue;
        }

        stream.writeInteger( int32_t( pMessage->ID ) );
        stream.writeInteger( pMessage->bCancel ? 1 : 0 );
        stream.writeInteger( pMessage->bSeen ? 1 : 0 );
    }

    for ( const bool bLaunched : bHelpLaunched ) {
        stream.writeByte( bLaunched ? 1u : 0u );
    }
}

void GSTutorial::showHint( TutorialHint *pHint ) {
    // FUN_00982310
    if ( pHint->DBLine == -1 ) {
        return;
    }

    pHint->bOnScreen = true;
    pHint->CurrentTimer = 0.0f;
    pHint->onDisplayed();

    const DBEntryTutorial &entry =
        gpDatabase->getGameDB().DBTutorial[pHint->DBLine];
    if ( strncmp( entry.Libelle.pAsString, "text", 5 ) == 0 ) {
        pHint->bSeen = true;
    } else {
        FlashPlayer *pHudPlayer = gpMngHud->getHudPlayer();
        const char *pButton =
            ( strncmp( entry.TextIndex.pAsString, "none", 5 ) == 0 )
                ? "empty"
                : entry.TextIndex.pAsString;
        pHudPlayer->setVariableValueNonLinked( "/:WhatButton", pButton );
        bFlashPopupOn = true;
        pHudPlayer->showFrame( "on", "/HUD/tutorial", true );
        pHudPlayer->setVariableValue( "/:TutorialText",
                                      entry.Libelle.pAsString );
    }

    if ( entry.File.pAsString != nullptr ) {
        strncpy( SoundName, entry.File.pAsString, kSoundNameLength - 1 );
        loadVoice( SoundName );
    }
}

void GSTutorial::hideHint( TutorialHint *pHint ) {
    // FUN_00981e50
    pHint->bOnScreen = false;
    pHint->bSeen = true;
    pHint->CurrentTimer = 0.0f;
    bFlashPopupOn = false;
    gpMngHud->getHudPlayer()->showFrame( "close", "/HUD/tutorial", true );
}

void GSTutorial::loadVoice( const char *pSoundName ) {
    // FUN_00982270
    if ( pSoundName == nullptr || pSoundBuffer != nullptr ) {
        return;
    }

    bLoadingSound = true;
    pSoundBuffer = nullptr;

    char path[0x104] = {};
    snprintf( path, sizeof( path ), "%sSound/Voices/%s/%s.wav",
              gpConfig->getResRootPath(), gpPlayerData->getLanguage(),
              pSoundName );

    std::weak_ptr<bool> guard = pInstallGuard;
    gpFile->loadBank( path, &pSoundBuffer, &SoundBufferSize, this,
                      [this, guard]( int32_t type, int32_t bSuccess ) {
                          // LAB_00981750
                          if ( !guard.expired() && type == 4 && bSuccess ) {
                              onVoiceLoaded();
                          }
                      } );
}

void GSTutorial::onVoiceLoaded() {
    // FUN_00981170
    if ( pSoundInstance == nullptr ) {
        return;
    }

    pSoundSample = std::make_unique<edAudioSample>();
    if ( pSoundBuffer != nullptr &&
         pSoundSample->loadFromMemory( pSoundBuffer,
                                       int32_t( SoundBufferSize ) ) ) {
        pSoundInstance->setSample( pSoundSample.get() );
        pSoundInstance->playSubtitled( SoundName, 0.0f );
    }

    bLoadingSound = false;
}

void GSTutorial::releaseVoice() {
    // FUN_00982430 (inlined)
    pSoundInstance->setSample( nullptr );
    pSoundSample.reset();
    TestDrive::Free( pSoundBuffer );
    pSoundBuffer = nullptr;
}
