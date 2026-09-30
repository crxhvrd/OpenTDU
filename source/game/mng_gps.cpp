#include "shared.h"
#include "mng_gps.h"

#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_base.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "player_data/gs_playerdata.h"
#include "tdu_instance.h"

#include <cmath>

static constexpr float kMetersToHectometers = 0.01f; // DAT_00f416d4
static constexpr float kHectometersToKm = 0.1f;      // FLOAT_00f41668
static constexpr float kKmToMiles = 0.621504f;       // DAT_00f418c0

MngGPS::MngGPS() : Manager(), mutex( "MngGPS" ) {
    // FUN_006c2280
    lastObjectivePosition.setZero();
    lastSecondaryObjectivePosition.setZero();
    hudTargetPosition.setZero();
}

MngGPS::~MngGPS() {}

bool MngGPS::initialize( TestDriveGameInstance * ) {
    // FUN_006c1d50
    mutex.initialize( "MngGPS" );
    cachedSessionPlayerCount = 0;
    return true;
}

void MngGPS::tick( float deltaTime, float ) {
    // FUN_006c38d0
    updateAccumulator += deltaTime;

    OTDU_UNIMPLEMENTED; // TODO: P2P/AI ("/:nb_FRplayers")

    Eigen::Vector4f objectivePosition = Eigen::Vector4f::Zero();
    const char *pObjectiveType = "";
    bool bHasObjective = false;

    if ( gpScenaric != nullptr ) {
        auto isSet = []( const ObjectiveMarker &marker ) {
            return marker.Type != nullptr && marker.Type[0] != '\0';
        };

        if ( gpTestDriveInstance != nullptr &&
             gpTestDriveInstance->getActiveGameMode() == GM_Mission ) {
            const ObjectiveMarker &primary = gpScenaric->getObjective( 3 );
            objectivePosition = primary.Position;
            pObjectiveType = primary.Type;
            bHasObjective = isSet( primary );
        } else {
            for ( int32_t slot = 2; slot >= 0; slot-- ) {
                const ObjectiveMarker &marker =
                    gpScenaric->getObjective( slot );
                objectivePosition = marker.Position;
                pObjectiveType = marker.Type;
                bHasObjective = isSet( marker );
                if ( bHasObjective ) {
                    break;
                }
            }
        }
    }

    bHasValidObjective = bHasObjective;

    CarState *pLocalCar = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;

    if ( !bHasObjective || pLocalCar == nullptr ) {
        // No target: drop the cached route and fold the track panel away.
        currentRoadParam = -1.0f;
        gpsTargetVector.w() = -1.0f;
        stateTimer = 0.0f;
        bRerouteAnnounced = 0;
        field_0x1ce4 = 0.0f;
        field_0x1cd4 = 0;

        if ( pHudPlayer != nullptr ) {
            FlashClip trackClip;
            if ( pHudPlayer->getClipPlayer( "/HUD/minimap/gps_track",
                                            &trackClip ) ) {
                trackClip.goToLabelAndPlay( "off", false );
            }
        }

        stateTimer += deltaTime;
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: AI

    const float routeDistance = 0.0f;
    lastObjectivePosition = objectivePosition;

    if ( pHudPlayer != nullptr ) {
        const bool bImperial =
            ( gpPlayerData != nullptr ) && gpPlayerData->isUsingImperialUnits();
        const float scaledDistance =
            bImperial ? ( routeDistance * kKmToMiles * kMetersToHectometers )
                      : ( routeDistance * kMetersToHectometers );
        pHudPlayer->setVariableValue(
            "obj_Distance", std::ceil( scaledDistance ) * kHectometersToKm );
        pHudPlayer->setVariableValue( "gps_Unit", bImperial ? "mi." : "km" );

        FlashClip trackClip;
        if ( pHudPlayer->getClipPlayer( "/HUD/minimap/gps_track",
                                        &trackClip ) ) {
            trackClip.setProperty( eFlashProperty::FP_Visible, true );
            trackClip.goToLabelAndPlay( "on", false );
        }

        FlashClip objectiveClip;
        if ( pHudPlayer->getClipPlayer( "HUD/minimap/gps_track/objectif_gps",
                                        &objectiveClip ) ) {
            objectiveClip.goToLabelAndPlay( pObjectiveType, false );
        }

        OTDU_UNIMPLEMENTED; // TODO: FUN_006c31e0

        if ( bBigArrowDisabled == 0 ) {
            FlashClip bigArrowClip;
            if ( pHudPlayer->getClipPlayer(
                     "HUD/minimap/gps_track/arrow_gps/_big", &bigArrowClip ) ) {
                bigArrowClip.goToLabelAndPlay( "empty", false );
            }
        }
    }

    updateAccumulator = 0.0f;
    stateTimer += deltaTime;
}

void MngGPS::reset() {
    // FUN_006c1d40
    // NOP
}

void MngGPS::draw() {
    // FUN_0063f2b0
    // NOP
}

void MngGPS::pause() {
    // FUN_006c1d30
    // NOP
}

void MngGPS::terminate() {
    // FUN_006c1d20
    mutex.terminate();
}
