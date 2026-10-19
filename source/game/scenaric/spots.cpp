#include "shared.h"
#include "spots.h"

#include "tdu_instance.h"

#include "ai/ai_road_query.h"

#include "filesystem/bank.h"
#include "world/gs_world.h"
#include "world/mng_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

#include "core/maths_helper.h"
#include "game/gm_base.h"
#include "game/gm_world.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_spot_mesh.h"
#include "game/game_cheats.h"
#include "game/game_mechanism_player.h"
#include "database/gs_database.h"
#include "debug/mng_debug_hud.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/achievements_data.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_ranking.h"
#include "flash/mng_flash_localize.h"

#include <algorithm>
#include <cstring>
#include <vector>

constexpr uint32_t kSpotFileMagic = 0x5450532e;  // ".SPT"
constexpr uint32_t kSpotArrayMagic = 0x41545053; // "SPTA"
constexpr uint32_t kSpotHeaderSize = 0x20;
constexpr uint32_t kSpotSubHeaderSize = 0x10;

bool gbParseLogChallengesInfos = false;         // DAT_010e7f50
uint8_t gMapActiveFilterMode = 1;               // DAT_00fa98c8
uint32_t gMapChallengeFilterMask = 0xffffffffu; // DAT_00fa98cc
uint32_t gMapServiceFilterMask = 0xffffffffu;   // DAT_00fa98d0
uint32_t gMapPlayerFilterMask = 0xffffffffu;    // DAT_00fa98d4
uint32_t gMapBookmarkFilterMask = 0xffffffffu;  // DAT_00fa98d8
uint8_t gMinimapFilterMode = 1;                 // DAT_00f96bc0

bool ParseSpotCellCoords( const std::string &filename, int32_t outCoords[4] ) {
    const size_t slash = filename.find_last_of( "/\\" );
    const char *p =
        filename.c_str() + ( slash == std::string::npos ? 0 : slash + 1 );

    // Skip the name prefix up to the first '-'.
    while ( *p != '\0' && *p != '-' ) {
        p++;
    }

    for ( int32_t i = 0; i < 4; i++ ) {
        if ( *p != '-' ) {
            return false;
        }
        p++; // step over the '-'
        const char *pField = p;
        while ( *p != '\0' && *p != '-' && *p != '.' ) {
            p++;
        }
        // The original treats a single-character field as its digit value
        // and any longer field as 10 (only the area X coordinate ever
        // reaches 10).
        outCoords[i] = ( p - pField == 1 ) ? ( *pField - '0' ) : 10;
    }
    return true;
}

int32_t Spots::getSpotMapIndex( uint64_t hashcode ) const {
    // FUN_007d6ce0

    // Binary search over the hashcode-sorted world spot list.
    const uint32_t numSpots = spotsWorld.NumSpots;
    if ( numSpots == 0 ) {
        return -1;
    }

    uint32_t lo = 0;
    uint32_t hi = numSpots;
    uint32_t mid = numSpots >> 1;
    while ( mid != lo ) {
        if ( spotsWorld.pSpotsMapOrSector.pWorld[mid].Hashcode <= hashcode ) {
            lo = mid;
        } else {
            hi = mid;
        }
        mid = ( ( hi - lo ) >> 1 ) + lo;
    }

    if ( spotsWorld.pSpotsMapOrSector.pWorld[lo].Hashcode == hashcode ) {
        return static_cast<int32_t>( lo );
    }
    return -1;
}

SpotMap *Spots::getSpotMap( uint64_t hashcode ) {
    // FUN_007d7030
    const int32_t index = getSpotMapIndex( hashcode );
    if ( index >= 0 && index < static_cast<int32_t>( spotsWorld.NumSpots ) ) {
        return &spotsWorld.pSpotsMapOrSector.pWorld[index];
    }
    return nullptr;
}

SpotMap *Spots::findSpotMapByType( const uint8_t type,
                                   const uint8_t subType,
                                   const int32_t occurrence,
                                   int32_t *pOutIndex ) {
    // FUN_007d6f40
    int32_t numMatches = 0;
    for ( int32_t i = 0; i < static_cast<int32_t>( spotsWorld.NumSpots );
          i++ ) {
        SpotMap *pSpot = &spotsWorld.pSpotsMapOrSector.pWorld[i];
        if ( pSpot == nullptr || pSpot->Type != type ||
             pSpot->SubType != subType ) {
            continue;
        }

        if ( numMatches == occurrence ) {
            *pOutIndex = i;
            return pSpot;
        }
        numMatches++;
    }

    *pOutIndex = -1;
    return nullptr;
}

SpotSector *Spots::getSpotSector( uint64_t hashcode ) {
    // FUN_007d6fb0
    for ( int32_t list = 0; list < 7; list++ ) {
        const auto &info = spotsSectors[list];
        SpotSector *pSectors = info.pSpotsMapOrSector.pSector;
        for ( uint32_t i = 0; i < info.NumSpots; i++ ) {
            if ( pSectors[i].Hashcode == hashcode ) {
                return &pSectors[i];
            }
        }
    }
    return nullptr;
}

ScenaricBank *FindStreamedSpotBank( const SpotSector *pSpotSector ) {
    // FUN_006fbeb0
    StreamingManager *pStreaming =
        ( gpWorld != nullptr ) ? gpWorld->getStreamingManager() : nullptr;
    if ( pStreaming == nullptr ) {
        return nullptr;
    }

    for ( SpotCell &cell : pStreaming->spotStream.cells ) {
        if ( cell.pSpotSector != pSpotSector ) {
            continue;
        }

        ScenaricBank *pBank = cell.spot.pSCBank;
        if ( pBank == nullptr || !pBank->isLoaded() ) {
            return nullptr;
        }
        return pBank;
    }

    return nullptr;
}

EntityCinematic *FindStreamedSpotCinematic( const SpotSector *pSpotSector,
                                            const char *pName,
                                            ScenaricBank **ppOutBank ) {
    // FUN_007d78a0
    ScenaricBank *pBank = FindStreamedSpotBank( pSpotSector );
    if ( pBank == nullptr ) {
        return nullptr;
    }

    if ( ppOutBank != nullptr ) {
        *ppOutBank = pBank;
    }

    return static_cast<EntityCinematic *>(
        pBank->findEntity( eEntityType::ET_CINE, pName ) );
}

Matrix4x4f_t BuildSpotSectorRotation( const SpotSector *pSector ) {
    // FUN_00503c80 w/ "XYZ" ordering
    Matrix4x4f_t rotation = Matrix4x4f_t::Identity();
    if ( pSector == nullptr ) {
        return rotation;
    }

    MatrixRotateX( rotation, pSector->EulerX );
    MatrixRotateY( rotation, pSector->EulerY );
    MatrixRotateZ( rotation, pSector->EulerZ );
    return rotation;
}

bool IsSpotAvailableInCurrentMode( const SpotMap *pSpot ) {
    // FUN_007d6a40
    const uint8_t type = pSpot->Type;

    if ( type == ST_Challenge && ( pSpot->SubType & 0xf ) == 1 &&
         ( gpPlayerData == nullptr || !gpPlayerData->isOnline() ) ) {
        return false;
    }

    if ( gbSimuModeEnabled ) {
        if ( type == ST_Challenge && ( pSpot->SubType & 0xf ) == 0 ) {
            return false;
        }
        if ( type == ST_Unknown8 ) {
            const uint8_t subType = pSpot->SubType;
            if ( subType == 5 || subType == 6 || subType == 7 ||
                 subType == 8 ) {
                return false;
            }
        }
    }

    return true;
}

bool IsSpotHidden( const uint64_t spotHashcode ) {
    // FUN_00747850
    if ( std::find( gRemovedSpots.begin(), gRemovedSpots.end(),
                    spotHashcode ) != gRemovedSpots.end() ) {
        return true;
    }

    const SpotMap *pSpot = gpScenaric->getSpotList().getSpotMap( spotHashcode );
    if ( pSpot == nullptr ) {
        return false;
    }

    switch ( pSpot->Type ) {
    case ST_CarDealer:
    case ST_BikeDealer:
        if ( gpMngDebugHUD == nullptr || !gbInstallCheatCars ) {
            DBEntryCarShop *pCarShop =
                gpDatabase->getCarShopBySpotName( pSpot->Hashcode );
            if ( pCarShop == nullptr ||
                 gpDatabase->getNumCarsAvailableInShop( pCarShop ) == 0 ) {
                return true;
            }
        }
        break;
    case ST_Club:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        return true;
    case ST_DriveIn:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        return true;
    case ST_Challenge: {
        static constexpr const char *kHiddenChallenges[] = {
            "RMFE0076", "TSFE0049", "RSFE0044",
            "TSFE0050", "RMFE0077", "RSFE0043",
        }; // PTR_s_RMFE0076_00f8b3f8
        for ( const char *pChallenge : kHiddenChallenges ) {
            if ( GetIdentifier64bit( pChallenge ) == spotHashcode ) {
                return true;
            }
        }

        if ( GetSpotGameModeID( pSpot ) == 0xc0c2 ) {
            const int32_t gameMode = gpActiveGameMode->getCurrentGameMode();
            if ( gameMode != GM_Club && gameMode != GM_Lobby ) {
                return true;
            }
        }

        if ( pSpot->MinLevelRequired != 0 ) {
            const AchievementsData *pAchievements =
                gpPlayerData->getLoadedSave()->getAchievements();
            if ( pAchievements->getPlayerLevel() + 1 <
                 int32_t( pSpot->MinLevelRequired ) ) {
                return true;
            }
        }
    } break;
    case ST_Unknown8:
        if ( pSpot->SubType == 0 ) {
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

uint32_t GetCarGroupIconFlag( const DBEntryCarPhysicsData *pCar ) {
    // FUN_007d6ab0
    const uint64_t carGroupHashcode =
        gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
            pCar->CarGroup.pAsRawPointer );
    switch ( carGroupHashcode ) {
    case 0x467a0e8:
        return 0x1000;
    case 0x476e328:
        return 0x2000;
    case 0x4862568:
        return 0x4000;
    case 0x49567a8:
        return 0x8000;
    case 0x4a4a9e8:
        return 0x10000;
    case 0x4b3ec28:
        return 0x20000;
    case 0x4c32e68:
        return 0x40000;
    case 0x493e108:
        return 0x80000;
    case 0x4a32348:
        return 0x100000;
    default:
        return 0u;
    }
}

static DBEntryBrand *FindBrandByHashcode( const uint64_t hashcode ) {
    // FUN_009a8a90
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gameDB.pNumItems[6]; i++ ) {
        if ( gameDB.DBBrand[i].Hashcode == hashcode ) {
            return &gameDB.DBBrand[i];
        }
    }
    return nullptr;
}

static inline bool IsSpotHashcode( const SpotMap *pSpot, const char *pName ) {
    return memcmp( &pSpot->Hashcode, pName, sizeof( uint64_t ) ) == 0;
}

void GetSpotIconInfo( const SpotMap *pSpot, SpotIconInfo &outInfo ) {
    // FUN_007d72a0
    outInfo.Unknown0x10 = 0u;
    outInfo.Unknown0xC = 0u;
    outInfo.ChallengeFlags = 0u;
    outInfo.SpotFlags = 0u;

    switch ( pSpot->Type ) {
    case ST_House: {
        outInfo.Kind = 2;
        const Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
        outInfo.SpotFlags = pGarage->isHouseOwned( pSpot->Hashcode ) ? 1u : 2u;
        return;
    }
    case ST_CarDealer:
        outInfo.Kind = 2;
        outInfo.SpotFlags = 8u;
        return;
    case ST_BikeDealer:
        outInfo.Kind = 2;
        outInfo.SpotFlags = 0x10u;
        return;
    case ST_Goodies:
        outInfo.Kind = 2;
        outInfo.SpotFlags = 0x100u;
        return;
    case ST_Club:
        outInfo.Kind = 2;
        outInfo.SpotFlags = 0x400u;
        return;
    case ST_DriveIn:
        outInfo.Kind = 2;
        outInfo.SpotFlags = 0x200u;
        return;
    case ST_Challenge:
        break;
    case ST_Unknown8:
        outInfo.Kind = 2;
        switch ( pSpot->SubType ) {
        case 0:
            outInfo.SpotFlags = 0x40u;
            break;
        case 1:
        case 9:
            outInfo.SpotFlags = 0x20u;
            break;
        case 4:
            outInfo.SpotFlags = 4u;
            break;
        default:
            outInfo.SpotFlags = 0x800u;
            break;
        }
        return;
    default:
        outInfo.Kind = 2;
        outInfo.SpotFlags = 0x80u;
        return;
    }

    outInfo.Kind = 1;
    if ( !IsSpotHashcode( pSpot, "HACK_HVK" ) ) {
        if ( IsSpotHashcode( pSpot, "HACK_DRV" ) ) {
            outInfo.ChallengeFlags = 4u;
        } else {
            switch ( GetSpotChallengeKind( pSpot ) ) {
            case 1:
                outInfo.ChallengeFlags = 2u;
                break;
            case 3:
                outInfo.ChallengeFlags = 8u;
                break;
            default:
                outInfo.ChallengeFlags = 1u;
                break;
            }
        }
    }

    switch ( GetSpotGameModeID( pSpot ) ) {
    case 0xc0c1:
        outInfo.ChallengeFlags |= 0x400u;
        break;
    case 0xc0c2:
        outInfo.ChallengeFlags |= 0x800u;
        break;
    default: {
        outInfo.ChallengeFlags |= 0x200u;
        const SavedRanking *pRanking =
            gpPlayerData->getLoadedSave()->getRanking();
        outInfo.ChallengeFlags |=
            ( pRanking->getChallengeResult( pSpot->Hashcode ) == 1 ) ? 0x20u
                                                                     : 0x40u;
    } break;
    }

    const bool bAllowsCars = pSpot->VehicleType == 0 || pSpot->VehicleType == 1;
    const bool bAllowsBikes =
        pSpot->VehicleType == 0 || pSpot->VehicleType == 2;

    switch ( pSpot->VehicleRestriction ) {
    case 0:
        outInfo.ChallengeFlags |= 0x100u;
        if ( bAllowsCars ) {
            outInfo.ChallengeFlags |= 0x7f000u;
        }
        if ( bAllowsBikes ) {
            outInfo.ChallengeFlags |= 0x180000u;
        }
        break;
    case 1: {
        const DBEntryBrand *pBrand = FindBrandByHashcode( pSpot->VehicleBrand );
        if ( pBrand == nullptr ) {
            break;
        }

        const GameDatabase &gameDB = gpDatabase->getGameDB();
        for ( uint32_t i = 0; i < gameDB.pNumItems[1]; i++ ) {
            const DBEntryCarPhysicsData &car = gameDB.DBCarData[i];
            if ( car.Brand->BrandID.pAsString == pBrand->BrandID.pAsString ) {
                outInfo.ChallengeFlags |= GetCarGroupIconFlag( &car );
            }
        }
        if ( gpDatabase->brandHasCarOfCategory( pBrand, true ) ) {
            outInfo.ChallengeFlags |= 0x100u;
        }
        if ( gpDatabase->brandHasCarOfCategory( pBrand, false ) ) {
            outInfo.ChallengeFlags |= 0x80u;
        }
    } break;
    case 2: {
        DBEntryCarPhysicsData *pCar =
            gpDatabase->getCarDBEntry( int32_t( pSpot->VehicleModel ) );
        if ( pCar == nullptr ) {
            break;
        }
        outInfo.ChallengeFlags |= gpDatabase->isBike( pCar ) ? 0x100u : 0x80u;
        outInfo.ChallengeFlags |= GetCarGroupIconFlag( pCar );
    } break;
    case 3:
        if ( bAllowsCars ) {
            outInfo.ChallengeFlags |= 0x80u;
            outInfo.ChallengeFlags |= ( pSpot->VehicleCategory <= 6 )
                                          ? 0x1000u << pSpot->VehicleCategory
                                          : 0x1ff000u;
        }
        if ( bAllowsBikes ) {
            const uint32_t flags = outInfo.ChallengeFlags;
            outInfo.ChallengeFlags = flags | 0x100u;
            if ( pSpot->VehicleCategory == 0 ) {
                outInfo.ChallengeFlags = flags | 0x80100u;
            } else if ( pSpot->VehicleCategory == 1 ) {
                outInfo.ChallengeFlags = flags | 0x100100u;
            } else if ( pSpot->VehicleType == 2 ) {
                outInfo.ChallengeFlags = flags | 0x1ff100u;
            }
        }
        break;
    case 4:
        if ( bAllowsCars ) {
            outInfo.ChallengeFlags |= 0x80u;
            const int32_t lastCategory =
                Min( int32_t( pSpot->VehicleCategoryMax ), 6 );
            for ( int32_t category = pSpot->VehicleCategoryMin;
                  category <= lastCategory; category++ ) {
                outInfo.ChallengeFlags |= 0x1000u << category;
            }
        }
        if ( bAllowsBikes ) {
            outInfo.ChallengeFlags |= 0x100u;
            const int32_t lastCategory =
                ( pSpot->VehicleCategoryMax != 0 ) ? 1 : 0;
            for ( int32_t category = pSpot->VehicleCategoryMin;
                  category <= lastCategory; category++ ) {
                outInfo.ChallengeFlags |= 0x80000u << category;
            }
        }
        break;
    default:
        break;
    }
}

bool IsSpotIconVisible( const SpotIconInfo &info, const bool bMinimap ) {
    // FUN_00742ee0
    uint8_t filterMode = gMapActiveFilterMode;
    if ( ( filterMode & 0x10 ) != 0 ) {
        if ( !bMinimap ) {
            return false;
        }
        filterMode = gMinimapFilterMode;
    }

    uint32_t flags = 0u;
    uint32_t filterMask = gMapServiceFilterMask;
    uint32_t groupMasks[4] = {};
    int32_t numGroups = 0;

    if ( ( filterMode & 1 ) != 0 ) {
        flags = info.ChallengeFlags;
        const bool bSolo = ( flags & 0x200u ) != 0;
        numGroups = 4;
        groupMasks[0] = 0xe00u;
        groupMasks[1] = 0x1fu;
        groupMasks[2] =
            ( bSolo || ( gMapChallengeFilterMask & 0x60u ) != 0x60u ) ? 0x60u
                                                                      : 0u;
        groupMasks[3] = 0x1ff000u;
        if ( !gpPlayerData->isOnline() && !bSolo ) {
            return false;
        }
        filterMask = gMapChallengeFilterMask;
        if ( gbSimuModeEnabled && bSolo ) {
            return false;
        }
    } else if ( ( filterMode & 2 ) != 0 ) {
        flags = info.SpotFlags;
        numGroups = 1;
        groupMasks[0] = 0xffffffffu;
        if ( ( flags & 0x200u ) != 0 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
            return false;
        }
        if ( ( flags & 0x400u ) != 0 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
            return false;
        }
    } else if ( ( filterMode & 4 ) != 0 ) {
        if ( ( info.Kind & filterMode ) != info.Kind ) {
            return false;
        }
        return ( info.Unknown0xC & gMapPlayerFilterMask ) != 0u;
    } else if ( ( filterMode & 8 ) != 0 ) {
        flags = info.Unknown0x10;
        filterMask = gMapBookmarkFilterMask;
    } else {
        return false;
    }

    if ( ( info.Kind & filterMode ) != info.Kind ) {
        return false;
    }

    for ( int32_t i = 0; i < numGroups; i++ ) {
        const uint32_t groupMask = groupMasks[i];
        if ( ( flags & groupMask & filterMask ) != 0u ) {
            continue;
        }
        if ( ( groupMask & filterMask ) != 0u ||
             groupMask != ( groupMask & flags ) ) {
            return false;
        }
    }

    return true;
}

void GetChallengeCarAvailability( const SpotMap *pSpot,
                                  bool *pOutLevelTooLow,
                                  bool *pOutHasEligibleCar ) {
    // FUN_007d7900
    *pOutLevelTooLow = false;
    *pOutHasEligibleCar = true;

    if ( pSpot == nullptr || gpPlayerData == nullptr ||
         gpPlayerData->getLoadedSave() == nullptr ) {
        return;
    }

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( pAchievements != nullptr ) {
        const int32_t playerLevel = pAchievements->getPlayerLevel();
        *pOutLevelTooLow = pSpot->MinLevelRequired != 0 &&
                           ( playerLevel + 1 ) < pSpot->MinLevelRequired;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYER_DATA
}

static bool IsSpotID( const SpotMap *pSpot, const char *pID ) {
    return std::memcmp( &pSpot->Hashcode, pID, sizeof( pSpot->Hashcode ) ) == 0;
}

static void LaunchHardcodedMission( const char *pMissionName ) {
    // FUN_007d8050 (inlined)
    (void)pMissionName;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
    gpTestDriveInstance->setNextGameMode( GM_Mission );
}

static constexpr float kSpotApproachDistance = 10.0f;

bool LaunchSpotLevel( SpotMap *pSpot, const bool bAllowFreeRide ) {
    // FUN_007d8050
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI

    const eGameMode previousGM = gpTestDriveInstance->getActiveGameMode();
    bool bJoiningLobby = false;
    if ( previousGM == GM_FreeRide ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL
    }

    switch ( pSpot->Type ) {
    case ST_House: {
        Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
        const bool bOwned = pGarage->isHouseOwned( pSpot->Hashcode );
        gpTestDriveInstance->setNextGameMode( bOwned ? GM_Living : GM_Estates );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL (FUN_007fbe80)
        break;
    }
    case ST_CarDealer:
    case ST_BikeDealer:
        gpTestDriveInstance->setNextGameMode( GM_Shop );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL
        break;
    case ST_Goodies:
        gpTestDriveInstance->setNextGameMode( GM_Goodies );
        break;
    case ST_AfterMarket:
        gpTestDriveInstance->setNextGameMode( GM_Shop );
        break;
    case ST_Club:
        gpTestDriveInstance->setNextGameMode( GM_Club );
        break;
    case ST_DriveIn:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
        gpTestDriveInstance->setNextGameMode( GM_DriveIn );
        break;
    case ST_Challenge:
        if ( IsSpotID( pSpot, "HACK_HVK" ) ) {
            LaunchHardcodedMission( "Test_Havock" );
        } else if ( IsSpotID( pSpot, "HACK_WKS" ) ) {
            LaunchHardcodedMission( "walkstreet" );
        } else if ( IsSpotID( pSpot, "HACK_DRV" ) ) {
            LaunchHardcodedMission( "drive001" );
        } else if ( IsSpotID( pSpot, "LED_STRP" ) ) {
            gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        } else {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC / ONLINE
            if ( gpActiveGameMode->getCurrentGameMode() != GM_Lobby ||
                 bJoiningLobby ) {
                gpTestDriveInstance->setNextGameMode( GM_Lobby );
            } else {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL
                gpTestDriveInstance->setNextGameMode( GM_Mission );
            }
        }
        break;
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 0:
            if ( !bAllowFreeRide ) {
                return false;
            }
            gpTestDriveInstance->setNextGameMode( GM_FreeRide );
            break;
        case 1:
        case 2:
        case 3:
        case 9:
            gpTestDriveInstance->setNextGameMode( GM_Shop );
            break;
        case 4:
            gpTestDriveInstance->setNextGameMode( GM_Estates );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL (FUN_007fba70)
            break;
        default:
            return false;
        }
        break;
    default:
        return false;
    }

    if ( previousGM != gpTestDriveInstance->getActiveGameMode() ||
         bJoiningLobby ) {
        Matrix4x4f_t approachFrame;
        ComputeRoadApproachFrame(
            Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ),
            approachFrame, kSpotApproachDistance );
        gpWorld->setPreviousTransform( approachFrame );
        gpActiveGameMode->requestExit();
    }

    return true;
}

bool Spots::isSpotValid( const SpotMap *pSpot ) const {
    // FUN_007d6b80 (inlined)
    if ( !bUseOnlyValid ) {
        return true;
    }
    for ( const SpotMap *pValidSpot : pValidSpots ) {
        if ( pValidSpot == pSpot ) {
            return true;
        }
    }
    return false;
}

void Spots::updateSpotMeshValidity() {
    // FUN_007d6b80
    showSpotMeshes( true );
}

void Spots::setSingleValidSpot( SpotMap *pSpot ) {
    // FUN_007d6cb0
    if ( pSpot != nullptr ) {
        bUseOnlyValid = true;
        pValidSpots[0] = pSpot;
    } else {
        bUseOnlyValid = false;
    }
    updateSpotMeshValidity();
}

void Spots::showSpotMeshes( bool bShow ) {
    // FUN_007e7a00
    // NOTE: the original re-checks both globals on every sector iteration; they
    // cannot change mid-loop, so the checks are hoisted here.
    if ( gpMngWorld == nullptr || gpWorld == nullptr ) {
        return;
    }

    StreamingManager *pStreamingManager = gpWorld->getStreamingManager();
    if ( pStreamingManager == nullptr ) {
        return;
    }

    // NOTE: the original walks 8 sectors (loop bound 0x87480 == 8 * 0x10e90)
    // even though SectorManager only ever constructs 7 (FUN_0042f450 loops 7,
    // FUN_00704a60 searches 7), so retail reads one Sector past the end of the
    // array. Reproducing that overrun is not possible here; we stop at 7.
    for ( Sector &sector : pStreamingManager->sectorManager.sectors ) {
        if ( sector.state != 1 ) {
            continue;
        }

        MngActor &mngActor = sector.geometry.mngActor;
        for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
            Entity *pActor = mngActor.getActor( i );
            if ( pActor == nullptr ||
                 pActor->getType() != eEntityType::ET_SPTM ) {
                continue;
            }

            EntitySpotMesh *pSpotMesh = static_cast<EntitySpotMesh *>( pActor );
            pSpotMesh->setSpotValid( bShow &&
                                     isSpotValid( pSpotMesh->getSpotWorld() ) );
        }
    }
}

bool Spots::installWorldSpots( void *pResource ) {
    // FUN_007d83d0
    if ( pResource == nullptr ) {
        return false;
    }

    // The buffer is owned by the level spots bank; keep a raw pointer to it.
    spotsWorld.pFile = pResource;

    const uint32_t *pHeader = static_cast<const uint32_t *>( pResource );
    if ( pHeader[0] == kSpotFileMagic && pHeader[1] == 1 &&
         ( pHeader[2] == 6 || pHeader[2] == 7 ) &&
         pHeader[4] == kSpotArrayMagic ) {
        const uint32_t payloadSize = pHeader[5];
        spotsWorld.pSpotsMapOrSector.pWorld = reinterpret_cast<SpotMap *>(
            static_cast<uint8_t *>( pResource ) + kSpotHeaderSize );
        spotsWorld.NumSpots =
            ( payloadSize - kSpotSubHeaderSize ) / sizeof( SpotMap );
    }

    if ( gbParseLogChallengesInfos ) {
        OTDU_UNIMPLEMENTED; // TODO: dump challenge spot info
    }

    return false;
}

void Spots::resetWorldSpots() {
    // FUN_00709790
    spotsWorld.pFile = nullptr;
    spotsWorld.NumSpots = 0;
    spotsWorld.pSpotsMapOrSector.pWorld = nullptr;
}

bool Spots::installSectorSpots( Sector *pSector, Bank *pSpotsBank ) {
    // FUN_007d7f20
    if ( pSector == nullptr || pSpotsBank == nullptr ) {
        return false;
    }

    if ( pSector->pSpotBlob != nullptr ) {
        return false;
    }

    const int32_t targetCoords[4] = {
        pSector->installedArea.posX, pSector->installedArea.posY,
        pSector->installedSector.posX, pSector->installedSector.posY };

    // Locate the sector list blob in the spots bank by matching its filename
    // grid cell. The first group-8/subtype-0x13 entry is the world list; the
    // rest are per-sector lists keyed by cell.
    void *pBlob = nullptr;
    const int32_t numEntries = pSpotsBank->getNumElements();
    for ( int32_t i = 0; i < numEntries; i++ ) {
        ParsedBankEntry entry;
        if ( pSpotsBank->getEntry( i, entry ) != 0 ) {
            break;
        }
        if ( entry.Type != 8 || entry.SubType != 0x13 ) {
            continue;
        }
        if ( entry.pData == spotsWorld.pFile ) {
            continue;
        }
        int32_t coords[4];
        if ( !ParseSpotCellCoords( entry.Filename, coords ) ) {
            continue;
        }
        if ( coords[0] == targetCoords[0] && coords[1] == targetCoords[1] &&
             coords[2] == targetCoords[2] && coords[3] == targetCoords[3] ) {
            pBlob = entry.pData;
            break;
        }
    }

    if ( pBlob == nullptr ) {
        return false; // no spots in this sector
    }

    // Find a free slot in spotsSectors[7].
    SpotsList *pSlot = nullptr;
    for ( int32_t i = 0; i < kNumSectorLists; i++ ) {
        if ( spotsSectors[i].pFile == nullptr ) {
            pSlot = &spotsSectors[i];
            break;
        }
    }
    if ( pSlot == nullptr ) {
        return false; // all 7 slots busy
    }

    const uint32_t *pHeader = static_cast<const uint32_t *>( pBlob );
    if ( pHeader[0] != kSpotFileMagic || pHeader[1] != 1 ||
         ( pHeader[2] != 6 && pHeader[2] != 7 ) ) {
        return false;
    }

    pSlot->pFile = pBlob;
    pSlot->pSector = pSector;
    if ( pHeader[4] == kSpotArrayMagic ) {
        const uint32_t payloadSize = pHeader[5];
        // Per-sector records are SpotSector (240 bytes).
        pSlot->NumSpots =
            ( payloadSize - kSpotSubHeaderSize ) / sizeof( SpotSector );
        pSlot->pSpotsMapOrSector.pSector = reinterpret_cast<SpotSector *>(
            static_cast<uint8_t *>( pBlob ) + kSpotHeaderSize );
    }
    pSector->pSpotBlob = pBlob;

    if ( getNumWorldSpots() > 0 ) {
        spawnSectorSpotMeshes( pSector, *pSlot );
    }
    pSlot->bInstalled = true;
    return true;
}

void Spots::spawnPendingSectorSpotMeshes() {
    // FUN_007d7fb0
    if ( getNumWorldSpots() == 0 ) {
        return;
    }
    for ( int32_t i = 0; i < kNumSectorLists; i++ ) {
        SpotsList &slot = spotsSectors[i];
        if ( slot.pFile != nullptr && slot.pSector != nullptr &&
             !slot.bMeshesSpawned ) {
            spawnSectorSpotMeshes( slot.pSector, slot );
        }
    }
}

void Spots::despawnSectorSpotMeshes() {
    // FUN_007d7ff0
    for ( int32_t i = 0; i < kNumSectorLists; i++ ) {
        SpotsList &slot = spotsSectors[i];
        if ( slot.pFile == nullptr || slot.pSector == nullptr ) {
            continue;
        }

        // FUN_007d7a60 (inlined)
        MngActor &mngActor = slot.pSector->geometry.mngActor;
        for ( int32_t j = 0; j < mngActor.getActorCount(); j++ ) {
            Entity *pActor = mngActor.getActor( j );
            if ( pActor->getType() == eEntityType::ET_SPTM ) {
                MngActor::Uninstanciate( &mngActor, pActor );
                j--;
            }
        }
        slot.bMeshesSpawned = false;
    }
}

int32_t Spots::countWorldSpots( const int32_t type, const int32_t subType ) {
    // FUN_007d7170
    int32_t count = 0;
    for ( int32_t i = 0; i < static_cast<int32_t>( getNumWorldSpots() ); i++ ) {
        const SpotMap *pSpot = getWorldSpot( i );
        if ( pSpot->Type != type || type < 1 ) {
            continue;
        }

        if ( type <= 4 ) {
            count++;
            continue;
        }

        if ( type != ST_Unknown8 ) {
            continue;
        }

        switch ( subType ) {
        case 0:
        case 1:
        case 4:
        case 9:
            if ( pSpot->SubType == subType ) {
                count++;
            }
            break;
        case 2:
            if ( pSpot->SubType == 2 || pSpot->SubType == 3 ) {
                count++;
            }
            break;
        default:
            break;
        }
    }
    return count;
}

SpotMap *Spots::getWorldSpotOfType( int32_t index,
                                    const int32_t type,
                                    const int32_t subType ) {
    // FUN_007d7060
    const int32_t numSpots = static_cast<int32_t>( getNumWorldSpots() );
    if ( numSpots < 1 ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < numSpots; i++ ) {
        SpotMap *pSpot = getWorldSpot( i );
        if ( pSpot->Type != type || type < 1 ) {
            continue;
        }

        bool bMatch = false;
        if ( type <= 4 ) {
            bMatch = true;
        } else if ( type == ST_Unknown8 ) {
            switch ( subType ) {
            case 0:
            case 1:
            case 4:
            case 9:
                bMatch = pSpot->SubType == subType;
                break;
            case 2:
                bMatch = pSpot->SubType == 2 || pSpot->SubType == 3;
                break;
            default:
                break;
            }
        }

        if ( !bMatch ) {
            continue;
        }

        if ( index == 0 ) {
            return pSpot;
        }
        index--;
    }

    return getWorldSpot( 0 );
}

Entity *Spots::selectSpotTemplate( ScenaricBank &meshBank,
                                   int32_t spotType,
                                   int32_t ownedFlag ) {
    // FUN_007d7b00
    Entity *pFallback = nullptr;
    const int32_t numEntities = meshBank.getEntityCount();
    for ( int32_t i = 0; i < numEntities; i++ ) {
        Entity *pEntity = meshBank.getEntity( i );
        if ( pEntity == nullptr ||
             pEntity->getType() != eEntityType::ET_SPTM ) {
            continue;
        }
        EntitySpotMesh *pMesh = static_cast<EntitySpotMesh *>( pEntity );
        if ( pFallback == nullptr ) {
            pFallback = pEntity; // first ET_SPTM as last resort
        }
        if ( pMesh->getTemplateType() == spotType ) {
            if ( pMesh->getTemplateSubType() == ownedFlag ) {
                return pEntity; // exact match
            }
            pFallback = pEntity; // type match (ignore owned)
        }
    }
    return pFallback;
}

void Spots::spawnSectorSpotMeshes( Sector *pSector, SpotsList &slot ) {
    // FUN_007d7b00
    if ( gpActiveGameMode == nullptr ||
         gpActiveGameMode->getCurrentGameMode() != GM_FreeRide ) {
        slot.bMeshesSpawned = false;
        return;
    }
    if ( gpActiveGM == nullptr ) {
        return;
    }
    if ( gpScenaric == nullptr || slot.pSpotsMapOrSector.pSector == nullptr ) {
        slot.bMeshesSpawned = true;
        return;
    }

    ScenaricBank &meshBank = gpScenaric->getSpotMeshBank();
    MngActor &mngActor = pSector->geometry.mngActor;
    SpotSector *pSpots = slot.pSpotsMapOrSector.pSector;

    for ( uint32_t i = 0; i < slot.NumSpots; i++ ) {
        SpotSector &spot = pSpots[i];

        SpotMap *pWorld = getSpotMap( spot.Hashcode );
        if ( pWorld == nullptr ) {
            continue;
        }

        // Skip removed spots
        if ( std::find( gRemovedSpots.begin(), gRemovedSpots.end(),
                        pWorld->Hashcode ) != gRemovedSpots.end() ) {
            continue;
        }

        const int32_t spotType = pWorld->Type;
        int32_t ownedFlag = static_cast<int32_t>( pWorld->SubType );
        if ( spotType == 0 ) {
            ownedFlag =
                gpPlayerData->getLoadedSave()->getGarage()->isHouseOwned(
                    pWorld->Hashcode )
                    ? 0
                    : 1;
        }

        Entity *pTemplate = selectSpotTemplate( meshBank, spotType, ownedFlag );
        if ( pTemplate == nullptr ) {
            continue;
        }

        Entity *pInstance = mngActor.instanciateActor( pTemplate );
        if ( pInstance == nullptr ) {
            continue;
        }
        EntitySpotMesh *pSpotMesh = static_cast<EntitySpotMesh *>( pInstance );
        pSpotMesh->bindSpot( pWorld, &spot );
        pSpotMesh->setupZoneVolumes();

        // World transform: RotateY(angle) at (X,Y,Z) from +0x20 (verified
        // against HawaiSpt.bnk). Mirrors the PMI prop placement pattern.
        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        MatrixRotateY( transform, spot.Angle );
        transform.row( 3 ) = Eigen::Vector4f( spot.X, spot.Y, spot.Z, 1.0f );
        pInstance->setTransform( transform, true );
        pInstance->onWorldStreamed();

        if ( !isSpotValid( pWorld ) ) {
            pSpotMesh->setSpotValid( false );
        }
    }

    slot.bMeshesSpawned = true;
}

void Spots::removeSectorSpots( void *pBlob ) {
    // FUN_00705cb0
    if ( pBlob == nullptr ) {
        return;
    }
    for ( int32_t i = 0; i < kNumSectorLists; i++ ) {
        if ( spotsSectors[i].pFile == pBlob ) {
            spotsSectors[i].bInstalled = false;
            spotsSectors[i].bMeshesSpawned = false;
            spotsSectors[i].pFile = nullptr;
            spotsSectors[i].NumSpots = 0;
            spotsSectors[i].pSpotsMapOrSector.pSector = nullptr;
            spotsSectors[i].pSector = nullptr;
            break;
        }
    }
}

static constexpr uint32_t kSpotAchievementDBIndex = 0xfu;

void SetSpotLockedTextFromReward( FlashPlayer *pPlayer,
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

    const DBEntryList &achievementDB =
        gameDB.Databases[kSpotAchievementDBIndex];
    const uint32_t numEntries = gameDB.pNumItems[kSpotAchievementDBIndex];
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

bool IsSpotRewardLocked( const uint64_t rewardHashcode ) {
    // FUN_00793b50 (inlined)
    if ( gpPlayerData == nullptr || gpPlayerData->getLoadedSave() == nullptr ) {
        return false;
    }

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    return pAchievements != nullptr &&
           pAchievements->isRewardLocked( rewardHashcode );
}
