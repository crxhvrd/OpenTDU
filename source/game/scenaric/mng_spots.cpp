#include "shared.h"
#include "mng_spots.h"

#include "tdu_instance.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "config/gs_config.h"
#include "database/gs_database.h"
#include "game/game_mechanism_player.h"
#include "game/mng_hud.h"
#include "game/mng_logo_display.h"
#include "game/vehicle/car_desc.h"
#include "core/format.h"
#include "flash/mng_flash_localize.h"
#include "flash/modal_message_box.h"
#include "filesystem/gs_file.h"
#include "game/dressing/avatar_catalog_scene.h"
#include "game/scenaric/entity/entity_convoy.h"
#include "game/scenaric/entity/entity_hitch_hiker.h"
#include "game/scenaric/entity/entity_shopping_girl.h"
#include "game/scenaric/entity/entity_transporter.h"
#include "player_data/driver.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_ranking.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "game/map/gm_map.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_save.h"
#include "player_data/saved_item_table.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/mng_sector_name.h"
#include "world/streaming_manager.h"

MngSpots *gpMngSpots = nullptr; // DAT_010e7d28

static constexpr int32_t kGPSCameraMode = 3;
static constexpr int32_t kNumOnlineRecords = 16;
static constexpr int32_t kNumExtraVisibleRecords = 16;
static constexpr int32_t kSpotHeightQueryVertices = 0x80;
static constexpr float kSpotHeightQueryHalfSize = 200.0f; // DAT_00f4170c
static constexpr float kSpotHeightQueryMinY = -1000.0f;   // DAT_00f419c4
static constexpr float kSpotHeightQueryMaxY = 1000.0f;    // DAT_00f41718
static constexpr uint8_t kSpotTypeCarDealer = 1;
static constexpr uint8_t kSpotTypeBikeDealer = 2;
static constexpr uint8_t kSpotTypeChallenge = 7;
static constexpr int32_t kMaxPlayerLevel = 5;
static constexpr int32_t kNoRecordID = -1;
static constexpr int32_t kIconVerticesPerRecord = 12;
static constexpr uint8_t kSpotTypeHouse = 0;
static constexpr uint8_t kSpotTypeGoodies = 3;
static constexpr uint8_t kSpotTypeAfterMarket = 4;
static constexpr uint8_t kSpotTypeClub = 5;
static constexpr uint8_t kSpotTypeDriveIn = 6;
static constexpr uint8_t kSpotTypeService = 8;
static constexpr uint8_t kServicePaint = 1;
static constexpr uint8_t kServiceCarRental = 2;
static constexpr uint8_t kServiceBikeRental = 3;
static constexpr uint8_t kServiceEstates = 4;
static constexpr uint8_t kServicePaintDeluxe = 9;
static constexpr uint64_t kRewardBikeDealer = 0x5f2d20ull;
static constexpr uint64_t kRewardDeluxePaint = 0x5da680ull;
static constexpr uint64_t kLockedTextRentedCar = 0x330bd32ull;
static constexpr uint64_t kLockedTextSingleColor = 0x22d50512ull;
static constexpr uint64_t kLockedTextBikePaint = 0x3310f3aull;
static constexpr uint64_t kLockedTextNoDeluxePaint = 0x338b05aull;
static constexpr uint64_t kLockedTextNotTunable = 0x33729baull;
static constexpr uint64_t kLockedTextNotTunableHere = 0x3b90002ull;
static constexpr uint64_t kLockedTextRentedCarTuning = 0x3778a32ull;
static constexpr uint64_t kConvoyRestriction = 0x38ff432ull;
static constexpr float kChallengeInfoDelay = 0.5f;       // FLOAT_00f41674
static constexpr float kOneThird = 0.33333334f;          // DAT_00f41764
static constexpr float kKilometersToMiles = 0.62150407f; // DAT_00f418c0
static constexpr const char *kSpotDisplayNameBookmark =
    "bookmark"; // DAT_00f970d4
static constexpr uint64_t kBikeCategoryHashcode = 0x35da5b5;
static constexpr int32_t kBrandDatabaseIndex = 6;
static constexpr int32_t kHouseDatabaseIndex = 7;
static constexpr int32_t kCarShopDatabaseIndex = 8;

static constexpr float kNoNeighbourScore = 3.4028235e+38f; // DAT_00f4171c
static constexpr float kPadRepeatFirstDelay = 0.2f;        // DAT_00f41730
static constexpr float kPadMoveDuration = 0.5f;
static constexpr float kNeighbourAlignment = 0.9f;     // DAT_00f416a0
static constexpr float kWooshDistanceSq = 25000000.0f; // DAT_00f71e60

struct NeighbourList {
    static constexpr int32_t kNumSlots = 4;

    MapSpotRecord *pRecords[kNumSlots];
    float Scores[kNumSlots];

    NeighbourList() {
        // FUN_00742580
        for ( int32_t i = 0; i < kNumSlots; i++ ) {
            pRecords[i] = nullptr;
            Scores[i] = kNoNeighbourScore;
        }
    }

    void insert( MapSpotRecord *pRecord, const float score ) {
        // FUN_00742510
        int32_t worst = 0;
        for ( int32_t i = 0; i < kNumSlots; i++ ) {
            if ( pRecords[i] == nullptr ) {
                pRecords[i] = pRecord;
                Scores[i] = score;
                return;
            }
            if ( Scores[i] > Scores[worst] ) {
                worst = i;
            }
        }
        if ( Scores[worst] > score ) {
            pRecords[worst] = pRecord;
            Scores[worst] = score;
        }
    }

    int32_t findBest() const {
        int32_t best = 0;
        for ( int32_t i = 0; i < kNumSlots; i++ ) {
            if ( Scores[i] < Scores[best] ) {
                best = i;
            }
        }
        return best;
    }
};

static float ScoreNeighbour( const Eigen::Vector4f &candidate,
                             const int32_t direction,
                             const Eigen::Vector4f &origin ) {
    // FUN_00743690
    switch ( direction ) {
    case MngSpots::PD_Up:
        if ( candidate.z() > origin.z() ) {
            return kNoNeighbourScore;
        }
        break;
    case MngSpots::PD_Down:
        if ( origin.z() > candidate.z() ) {
            return kNoNeighbourScore;
        }
        break;
    case MngSpots::PD_Right:
        if ( origin.x() > candidate.x() ) {
            return kNoNeighbourScore;
        }
        break;
    case MngSpots::PD_Left:
        if ( candidate.x() > origin.x() ) {
            return kNoNeighbourScore;
        }
        break;
    default:
        break;
    }

    static const Eigen::Vector4f kAxes[MngSpots::kNumPadDirections] = {
        Eigen::Vector4f( 0.0f, 0.0f, -1.0f, 0.0f ),
        Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 0.0f ),
        Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ),
        Eigen::Vector4f( -1.0f, 0.0f, 0.0f, 0.0f ),
    };
    const Eigen::Vector4f delta = origin - candidate;
    const float distance = delta.norm();
    const float alignment =
        fabsf( ( delta / distance ).dot( kAxes[direction] ) );
    return ( 1.0f - alignment * 0.5f ) * distance;
}

// Filter checkboxes of the map Flash page
struct MapFilterCheckbox {
    const char *pVariable;
    uint8_t Mode;
    uint32_t Mask;
};

// DAT_00fa98e8
static constexpr MapFilterCheckbox kMapFilterCheckboxes[] = {
    {"f_time",        1, 0x1  },
    {"f_speed",       1, 0x2  },
    {"f_driving",     1, 0x4  },
    {"f_race",        1, 0x8  },
    {"f_extra",       1, 0x10 },
    {"f_challDone",   1, 0x20 },
    {"f_challUndone", 1, 0x40 },
    {"f_challCars",   1, 0x80 },
    {"f_challBikes",  1, 0x100},
    {"f_challSolo",   1, 0x200},
    {"f_challMulti",  1, 0x400},
    {"f_home",        2, 0x1  },
    {"f_forsale",     2, 0x2  },
    {"f_estates",     2, 0x4  },
    {"f_carDealer",   2, 0x8  },
    {"f_bikeDealer",  2, 0x10 },
    {"f_carPaint",    2, 0x20 },
    {"f_tuning",      2, 0x80 },
    {"f_goodies",     2, 0x100},
    {"f_driveIn",     2, 0x200},
    {"f_club",        2, 0x400},
    {"f_carRental",   2, 0x800},
    {"f_rookie",      4, 0x1  },
    {"f_novice",      4, 0x2  },
    {"f_pro",         4, 0x4  },
    {"f_expert",      4, 0x8  },
    {"f_master",      4, 0x10 },
    {"f_playCars",    4, 0x20 },
    {"f_playBikes",   4, 0x40 },
    {"f_friends",     4, 0x80 },
    {"f_clubMbrs",    4, 0x200},
    {"f_others",      4, 0x100},
};

struct MapCarGroup {
    uint64_t Hashcode;
    const char *pLabel;
};

static constexpr MapCarGroup kMapCarGroups[] = {
    {0x467a0e8, "groupA" },
    {0x476e328, "groupB" },
    {0x4862568, "groupC" },
    {0x49567a8, "groupD" },
    {0x4a4a9e8, "groupE" },
    {0x4b3ec28, "groupF" },
    {0x4c32e68, "groupG" },
    {0x493e108, "groupMA"},
    {0x4a32348, "groupMB"},
};
static constexpr int32_t kNumMapCarGroups =
    sizeof( kMapCarGroups ) / sizeof( kMapCarGroups[0] );
static constexpr int32_t kChallengeGroupFirstBit = 12;
static constexpr int32_t kPlayerGroupFirstBit = 10;
static constexpr uint32_t kGroupBitsMask = 0x1ffu;
static constexpr uint64_t kGroupAllText = 0x38f82ea;
static constexpr uint64_t kGroupNoneText = 0x38ff432;
static constexpr uint64_t kGroupText = 0x3b44512;
static constexpr uint64_t kPlayerGroupAllText = 0x537c0f;

static constexpr float kDensityHeight = 400.0f;      // DAT_00f41ca0
static constexpr float kDensityHalfSizeScale = 1.5f; // DAT_00f416b8
static constexpr float kDensityAlphaScale = 0.75f;   // DAT_00f416a4
static constexpr float kOctagonDiagonal = 1.414f;    // DAT_00f41da4
static constexpr float kPopularityRadius = 1000.0f;  // DAT_00f41718
static constexpr uint32_t kPopularityColor = 0xff4040u;

// DAT_00f96bc8
static constexpr uint32_t kDensityPalette[256] = {
    0xf2fafc, 0xf2f9fc, 0xf1f8fb, 0xf1f7fa, 0xf0f6fa, 0xeff5f9, 0xeff4f9,
    0xeef3f9, 0xedf2f8, 0xecf1f8, 0xecf0f7, 0xeceff6, 0xebeef6, 0xeaeef5,
    0xe9ecf5, 0xe9ebf4, 0xe8ebf4, 0xe7eaf4, 0xe7e8f3, 0xe6e7f3, 0xe5e6f2,
    0xe5e6f2, 0xe5e4f1, 0xe4e3f1, 0xe3e3f0, 0xe3e1f0, 0xe2e1ef, 0xe2dfef,
    0xe1dfee, 0xe1deee, 0xe0dced, 0xdfdced, 0xdfdbec, 0xdddaec, 0xddd9eb,
    0xdcd8ea, 0xdcd6ea, 0xdcd6ea, 0xdad5e9, 0xdad4e9, 0xdad3e8, 0xd9d2e8,
    0xd8d1e7, 0xd8d0e6, 0xd7cfe6, 0xd7cee6, 0xd6cce5, 0xd5cce4, 0xd5cbe4,
    0xd4cae3, 0xd4c9e3, 0xd3c8e2, 0xd2c7e2, 0xd2c6e2, 0xd1c5e2, 0xd0c4e0,
    0xd0c3e0, 0xd0c2e0, 0xcec1df, 0xcdc0de, 0xcebfde, 0xcdbfdd, 0xccbedd,
    0xccbddd, 0xcbbbdc, 0xcabbdc, 0xcabadb, 0xc9b9da, 0xc8b8db, 0xc8b6da,
    0xc8b6d9, 0xc6b4d8, 0xc6b4d8, 0xc5b2d7, 0xc5b1d8, 0xc4b1d7, 0xc3b0d6,
    0xc3afd6, 0xc2add5, 0xc2acd5, 0xc1acd4, 0xc0abd3, 0xc0aad3, 0xc0a8d2,
    0xbfa8d3, 0xbea7d2, 0xbda5d1, 0xbda5d0, 0xbca4d0, 0xbca3cf, 0xbba2cf,
    0xbaa0cf, 0xb9a0ce, 0xb99ecd, 0xb99dcd, 0xb89dcd, 0xb89ccc, 0xb79acb,
    0xb69acb, 0xb599cb, 0xb598cb, 0xb497ca, 0xb396ca, 0xb395c9, 0xb394c9,
    0xb294c8, 0xb192c8, 0xb191c7, 0xb090c6, 0xaf8fc6, 0xaf8ec5, 0xae8dc5,
    0xad8dc4, 0xad8bc4, 0xac8ac4, 0xac89c2, 0xab88c3, 0xaa87c2, 0xaa87c2,
    0xaa85c1, 0xa885c1, 0xa884bf, 0xa783bf, 0xa782bf, 0xa680be, 0xa680be,
    0xa57ebd, 0xa47ebd, 0xa47cbc, 0xa37bbc, 0xa37abc, 0xa27abb, 0xa178ba,
    0xa077ba, 0xa077b9, 0x9f76b9, 0x9f74b9, 0x9e73b8, 0x9d72b8, 0x9d72b6,
    0x9c71b6, 0x9c70b6, 0x9b6fb5, 0x9b6eb4, 0x9a6db4, 0x9a6cb4, 0x996ab3,
    0x986ab2, 0x9768b2, 0x9768b1, 0x9667b1, 0x9566b1, 0x9565b0, 0x9464b0,
    0x9463b0, 0x9462af, 0x9262ae, 0x9260ae, 0x925fae, 0x905fad, 0x905dad,
    0x905cac, 0x8f5bab, 0x8f5aaa, 0x8e59aa, 0x8d58aa, 0x8c58a9, 0x8c56a9,
    0x8c55a8, 0x8a54a7, 0x8a53a7, 0x8953a7, 0x8852a6, 0x8851a5, 0x874fa5,
    0x874fa5, 0x874da4, 0x864ca4, 0x864ba3, 0x854aa2, 0x8449a2, 0x8348a1,
    0x8348a1, 0x8246a0, 0x8146a0, 0x81449f, 0x80439f, 0x80429f, 0x7f419e,
    0x7e419e, 0x7d409d, 0x7d3e9d, 0x7d3e9c, 0x7c3c9c, 0x7b3b9c, 0x7b3b9a,
    0x7a3a9a, 0x7a399a, 0x79389a, 0x783799, 0x773698, 0x773598, 0x763497,
    0x763397, 0x753297, 0x753195, 0x743095, 0x732f95, 0x732e94, 0x722d93,
    0x722c94, 0x712b92, 0x702a92, 0x6f2a91, 0x6f2892, 0x6f2791, 0x6e2690,
    0x6d2690, 0x6d2490, 0x6c238f, 0x6c238e, 0x6b228e, 0x6b208d, 0x691f8d,
    0x691f8d, 0x681e8c, 0x681c8b, 0x671c8b, 0x661a8a, 0x661a8a, 0x651889,
    0x641789, 0x641788, 0x631688, 0x631488, 0x621487, 0x621386, 0x611286,
    0x601086, 0x5f1085, 0x5f0f84, 0x5f0e83, 0x5e0d83, 0x5d0c83, 0x5c0b82,
    0x5c0a82, 0x5b0981, 0x5a0881, 0x5a0781, 0x5a0680, 0x59057f, 0x58047f,
    0x58037e, 0x57027e, 0x57017e, 0x56007d,
};

static Matrix4x4f_t ComputeWorldShiftMatrix() {
    Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        matrix.row( 3 ) =
            Eigen::Vector4f( -shift.x(), -shift.y(), -shift.z(), 1.0f )
                .transpose();
    }
    return matrix;
}

static void PushOctagonFan( DrawList &drawList,
                            const float centerX,
                            const float height,
                            const float centerZ,
                            const float halfSizeX,
                            const float halfSizeZ,
                            const uint32_t centerColor,
                            const uint32_t ringColor ) {
    if ( !drawList.beginPrimitive( PT_TriangleFan, 10 ) ) {
        return;
    }

    const float diagonalX = halfSizeX * kOctagonDiagonal;
    const float diagonalZ = halfSizeZ * kOctagonDiagonal;
    drawList.setDiffuse( 0, ColorRGBA( centerColor ) );
    drawList.pushVertex( centerX, height, centerZ );
    drawList.setDiffuse( 0, ColorRGBA( ringColor ) );
    drawList.pushVertex( centerX - halfSizeX, height, centerZ - halfSizeZ );
    drawList.pushVertex( centerX, height, centerZ - diagonalZ );
    drawList.pushVertex( centerX + halfSizeX, height, centerZ - halfSizeZ );
    drawList.pushVertex( centerX + diagonalX, height, centerZ );
    drawList.pushVertex( centerX + halfSizeX, height, centerZ + halfSizeZ );
    drawList.pushVertex( centerX, height, centerZ + diagonalZ );
    drawList.pushVertex( centerX - halfSizeX, height, centerZ + halfSizeZ );
    drawList.pushVertex( centerX - diagonalX, height, centerZ );
    drawList.pushVertex( centerX - halfSizeX, height, centerZ - halfSizeZ );
    drawList.commitPrimitive();
}

static uint32_t CycleGroupBits( const uint32_t value,
                                const int32_t firstBit,
                                const bool bHasNone,
                                const bool bNext ) {
    const uint32_t all = kGroupBitsMask << firstBit;
    const uint32_t first = 1u << firstBit;
    const uint32_t last = 0x100u << firstBit;
    if ( ( value & all ) == all ) {
        if ( !bNext ) {
            return ( value & ~all ) | last;
        }
        return bHasNone ? ( value & ~all ) : ( ( value & ~all ) | first );
    }
    if ( ( value & all ) == 0 ) {
        if ( !bHasNone ) {
            return value;
        }
        return bNext ? ( value | first ) : ( value | all );
    }

    int32_t lowest = firstBit;
    while ( ( value & ( 1u << lowest ) ) == 0 ) {
        lowest++;
    }
    if ( bNext ) {
        if ( lowest == firstBit + 8 ) {
            return value | all;
        }
        return ( value & ~( 1u << lowest ) ) | ( 1u << ( lowest + 1 ) );
    }
    if ( lowest == firstBit ) {
        return bHasNone ? ( value & ~( 1u << lowest ) ) : ( value | all );
    }
    return ( value & ~( 1u << lowest ) ) | ( 1u << ( lowest - 1 ) );
}

static bool IsRoadUnreachable( const SavedRoadDiscovery *pRoads,
                               const int32_t roadID,
                               const int32_t roadSegment ) {
    // FUN_0040aae0
    return pRoads->isRoadLocked( roadID, roadSegment ) ||
           !IsRoadAvailable( roadID );
}

static CameraGPS *FindGPSCamera() {
    return static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
}

static float ComputeSpotIconSize( const CameraGPS *pCamera ) {
    // FUN_008dbf60
    auto sizeForZoom = []( const int32_t zoom ) {
        switch ( zoom ) {
        case CameraGPS::GZL_Island:
            return 600.0f; // DAT_00f7c788
        case CameraGPS::GZL_Far:
            return 375.0f; // DAT_00f7c78c
        case CameraGPS::GZL_Medium:
            return 150.0f; // DAT_00f7c790
        case CameraGPS::GZL_Close:
            return 40.0f; // DAT_00f7c794
        default:
            return 10.0f; // FLOAT_00f4166c
        }
    };

    const float size = sizeForZoom( pCamera->getZoom() );
    if ( !pCamera->isInTransition() ) {
        return size;
    }

    const float progress = pCamera->getTransitionProgress();
    return ( 1.0f - progress ) * sizeForZoom( pCamera->getPreviousZoom() ) +
           progress * size;
}

static void ComputeIconCorners( const Eigen::Vector4f &position,
                                const bool bAlreadyShifted,
                                Eigen::Vector4f outCorners[4] ) {
    // FUN_00745030
    Eigen::Vector4f center = position;
    if ( !bAlreadyShifted && gpWorldStreaming != nullptr &&
         gpWorldStreaming->isEnabled() ) {
        center -= gpWorldStreaming->getPosition();
    }

    const Matrix4x4f_t &camToWorld =
        gpCamera->getDefaultCamera()->getCamToWorld();
    const Eigen::Vector4f up = camToWorld.row( 1 ).transpose();
    const Eigen::Vector4f right = camToWorld.row( 0 ).transpose();

    const CameraGPS *pCameraGPS = FindGPSCamera();
    const Eigen::Vector4f upOffset =
        up / up.norm() * ComputeSpotIconSize( pCameraGPS );
    const Eigen::Vector4f rightOffset =
        right / right.norm() * ComputeSpotIconSize( pCameraGPS );

    outCorners[0] = center - rightOffset;
    outCorners[1] = outCorners[0] + upOffset + upOffset;
    outCorners[2] = center + rightOffset;
    outCorners[3] = outCorners[2] + upOffset + upOffset;
}

static int CompareRecordsByDepth( const void *pA, const void *pB ) {
    // FUN_00742a30
    const MapSpotRecord *pRecordA = static_cast<const MapSpotRecord *>( pA );
    const MapSpotRecord *pRecordB = static_cast<const MapSpotRecord *>( pB );
    return ( pRecordA->Position.z() - pRecordB->Position.z() > 0.0f ) ? 1 : -1;
}

static int CompareRecordsByVisibility( const void *pA, const void *pB ) {
    // FUN_00744040
    if ( gpMngSpots == nullptr ) {
        return 1;
    }

    const MapSpotRecord *pRecordA = static_cast<const MapSpotRecord *>( pA );
    const MapSpotRecord *pRecordB = static_cast<const MapSpotRecord *>( pB );
    const int32_t visibilityDelta =
        static_cast<int32_t>( IsMapRecordVisible( pRecordA, false ) ) -
        static_cast<int32_t>( IsMapRecordVisible( pRecordB, false ) );
    if ( visibilityDelta != 0 ) {
        return visibilityDelta;
    }
    return CompareRecordsByDepth( pA, pB );
}

bool IsMapRecordVisible( const MapSpotRecord *pRecord, const bool bMinimap ) {
    // FUN_007435f0
    const GameIntroTutorial &tutorial = gpScenaric->getIntroTutorial();
    if ( tutorial.isActive() ) {
        if ( !bMinimap && ( gMapActiveFilterMode & 0x10 ) != 0 ) {
            return false;
        }
        return !tutorial.hidesSpot( pRecord->pSpot );
    }
    return pRecord != nullptr && IsSpotIconVisible( pRecord->Info, bMinimap );
}

SpotScreensBank::SpotScreensBank()
    : Bank(),
      p2DMData( nullptr ),
      pRender2DM( nullptr ),
      num2DB( 0 ),
      state( SSS_Idle ) {
    // FUN_00742e90
}

SpotScreensBank::~SpotScreensBank() {
    delete pRender2DM;
    unload();
}

void SpotScreensBank::load() {
    // FUN_0074b6e0
    if ( state != SSS_Idle ) {
        return;
    }

    pRender2DM = new Render2DM();

    char bankPath[512];
    snprintf( bankPath, sizeof( bankPath ), "%sFrontEnd/AllRes/SpotsScr.bnk",
              gpConfig->getResRootPath() );
    if ( !isLoaded() &&
         !loadBankWithMemory( gpFile, bankPath, nullptr, 0, true ) ) {
        return;
    }

    state = SSS_Loading;
}

void SpotScreensBank::loadContent() {
    // FUN_00743380
    num2DB = 0;
    ParsedBankEntry entry;
    const int32_t numEntries = getNumElements();
    for ( int32_t i = 0; i < numEntries; i++ ) {
        if ( getEntry( i, entry ) != 0 || entry.Type != 1 ) {
            continue;
        }
        if ( entry.SubType == 2 ) {
            p2DMData = entry.pData;
            pRender2DM->initialize( p2DMData );
        } else if ( entry.SubType == 3 ) {
            num2DB++;
        }
    }

    FileCollection2D &collection = gpFlash->getCollection2D();
    int32_t index2DB = 0;
    for ( int32_t i = 0; i < numEntries; i++ ) {
        if ( getEntry( i, entry ) != 0 || entry.Type != 1 ||
             entry.SubType != 3 ) {
            continue;
        }
        // NOTE: retail has no bound on the 2DB count (one inline slot).
        if ( index2DB >= kMax2DB ) {
            break;
        }
        render2DBs[index2DB].initialize( entry.pData );
        collection.register2DB( &render2DBs[index2DB] );
        index2DB++;
    }
    num2DB = index2DB;
    collection.register2DM( pRender2DM );

    for ( int32_t i = 0; i < num2DB; i++ ) {
        Material *pMaterial = pRender2DM->getMaterialAtIndex( i );
        if ( pMaterial != nullptr ) {
            pMaterial->OT = 0x2a;
            pMaterial->DepthTest = 1;
            pMaterial->DepthWrite = 2;
            pMaterial->AlphaTest = CF_Never;
        }
    }

    state = SSS_Ready;
}

void SpotScreensBank::release() {
    // FUN_00742900
    if ( state != SSS_Ready ) {
        state = SSS_Idle;
        return;
    }

    FileCollection2D &collection = gpFlash->getCollection2D();
    if ( pRender2DM != nullptr ) {
        collection.unregister2DM( pRender2DM );
        pRender2DM->destroy();
        delete pRender2DM;
        pRender2DM = nullptr;
    }
    for ( int32_t i = 0; i < num2DB; i++ ) {
        collection.unregister2DB( &render2DBs[i] );
        render2DBs[i].destroy();
    }
    state = SSS_Idle;
}

Material *SpotScreensBank::getMaterial( const char *pMaterialName ) {
    // FUN_00743590
    if ( state != SSS_Ready ) {
        return nullptr;
    }
    return pRender2DM->getMaterial( pMaterialName );
}

MngSpots::MngSpots()
    : Manager(),
      pOwner( nullptr ),
      bIconsEnabled( true ),
      bPadNavigationEnabled( true ),
      densityMap(),
      bDrawDensityMap( false ),
      bDrawChallengeCones( false ),
      bShowCursor( true ),
      bResortRecords( true ),
      bWasFreeMode( false ),
      spotKindLabel(),
      pPicture( nullptr ),
      padRepeatElapsed( 0.0f ),
      padRepeatNext( 0.0f ),
      padRepeatPrevious( 0.0f ),
      pNeighbours(),
      requestedBookmarkPictureID( -1 ),
      loadedBookmarkPictureID( -1 ),
      pictureMutex( "BK_MP" ),
      vehicleRestriction(),
      brandsList(),
      challengeInfoTimer( 0.0f ),
      bChallengeInfoRequested( false ),
      bChallengeInfoFromCache( false ),
      pictureTimer( 0.0f ),
      bPictureRequested( false ),
      bPictureResolved( false ),
      pPictureSource( nullptr ),
      bPictureDrawn( false ),
      bPictureHidden( false ),
      pChallengeInfoSpot( nullptr ),
      pRouteWaypoints( nullptr ),
      numBookmarks( 0 ),
      bookmarks(),
      records(),
      numRecords( 0 ),
      numWorldSpotRecords( 0 ),
      brandLogoClock( 0.0f ),
      bBrandLogoSecondTicked( false ),
      garagePanel(),
      onlineRecords(),
      numOnlineRecords( 0 ),
      visibleRecords(),
      numVisibleRecords( 0 ),
      bookmarkScreenshots(),
      pSelectedRecord( nullptr ),
      bInfoCartShown( false ),
      bInfoCartRequested( false ),
      focusPosition( Eigen::Vector4f::Zero() ),
      iconDrawList(),
      densityDrawList(),
      challengeConeDrawList(),
      spot2DM(),
      pSpotMaterialData( nullptr ),
      pSpotMaterial( nullptr ),
      currentCarNumColors( 0 ),
      bCurrentCarIsBike( false ),
      bCurrentCarDeluxePaint( false ),
      pMapPlayer( nullptr ),
      mapRect(),
      freeModeRect(),
      bMapRectsLatched( false ),
      whatSpot(),
      spotScreensBank(),
      housePictureBank(),
      mapScreens() {
    // FUN_00744210
    OTDU_UNIMPLEMENTED; // TODO: EventParameters
    gpMngSpots = this;
}

MngSpots::~MngSpots() {
    // FUN_00744130
    if ( gpMngSpots == this ) {
        gpMngSpots = nullptr;
    }
}

bool MngSpots::initialize( TestDriveGameInstance * ) {
    // FUN_0074cdb0
    pRouteWaypoints = new AiRoadLocation[MngTracRoad::kMaxNumLegs];
    pChallengeInfoSpot = nullptr;
    pPictureSource = nullptr;
    buildRecords();
    spotScreensBank.load();
    bookmarkScreenshots.create( 0x100, 0x80, VF_X8R8G8B8 );
    housePictureBank.setBuffer(
        TestDrive::AllocAligned( HousePictureBank::kBufferSize, 0x10 ) );
    mapScreens.setBuffer(
        TestDrive::AllocAligned( MapScreenshotBank::kBufferSize, 0x10 ) );
    return true;
}

void MngSpots::reset() {
    // FUN_00743d10
    if ( gpScenaric->getIntroTutorial().isActive() ) {
        return;
    }

    const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    const int32_t carID =
        ( pCurrentCar != nullptr ) ? int32_t( pCurrentCar->getCarID() ) : 0;
    currentCarNumColors = gpDatabase->getNumColors( carID );

    DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( carID );
    bCurrentCarIsBike =
        pCarEntry != nullptr &&
        gpDatabase->getStringByHashcode( 1, kBikeCategoryHashcode ) ==
            pCarEntry->Category.pAsString;
    bCurrentCarDeluxePaint =
        pCarEntry != nullptr && ( ( pCarEntry->BitField >> 6 ) & 1 ) != 0;
}

void MngSpots::draw() {
    // FUN_00746150
    if ( bDrawDensityMap ) {
        gpRender->getRenderPass<RP_Near>().pScene->enqueueDynamicDrawList(
            &densityDrawList );
    }
    if ( bDrawChallengeCones ) {
        gpRender->getRenderPass<RP_Near>().pScene->enqueueDynamicDrawList(
            &challengeConeDrawList );
    }

    if ( !bIconsEnabled ) {
        return;
    }

    FlashClip cursorClip;
    if ( pMapPlayer != nullptr &&
         pMapPlayer->getClipPlayer( "/curseur", &cursorClip ) ) {
        const CameraGPS *pCameraGPS = FindGPSCamera();
        const bool bSpaceView =
            pCameraGPS->getZoom() == CameraGPS::GZL_Space ||
            ( pCameraGPS->getPreviousZoom() == CameraGPS::GZL_Space &&
              pCameraGPS->isInTransition() );
        const bool bOwnerBusy =
            pOwner != nullptr &&
            ( pOwner->getExitState() != 0 || pOwner->hasRequestedExit() );
        const bool bHidden =
            bSpaceView ||
            ( gpMngMapViewer != nullptr &&
              gpMngMapViewer->isPlayerListOpen() ) ||
            ( gpMapView != nullptr && gpMapView->isMoving() ) || bOwnerBusy ||
            !bShowCursor;

        bool bVisible = false;
        if ( !bHidden ) {
            float x = 0.0f;
            float y = 0.0f;
            if ( computeSelectionMoviePosition( x, y ) ) {
                cursorClip.setProperty( eFlashProperty::FP_Visible, true );
                cursorClip.setProperty( eFlashProperty::FP_X, x );
                cursorClip.setProperty( eFlashProperty::FP_Y, y );

                FlashClip popupClip;
                if ( pMapPlayer->getClipPlayer( "/popup_in_out",
                                                &popupClip ) ) {
                    popupClip.setProperty( eFlashProperty::FP_X, x );
                    popupClip.setProperty( eFlashProperty::FP_Y, y );
                }
                gpRender->getRenderPass<RP_SpotMap2D>()
                    .pScene->enqueueDynamicDrawList( &iconDrawList );
                return;
            }

            const eGameMode activeGameMode =
                gpTestDriveInstance->getActiveGameMode();
            bVisible =
                gMapActiveFilterMode == 0x10 &&
                ( ( activeGameMode == GM_Map && !bOwnerBusy ) ||
                  activeGameMode == GM_Lobby || activeGameMode == GM_Club );
        }
        cursorClip.setProperty( eFlashProperty::FP_Visible, bVisible );
    }

    gpRender->getRenderPass<RP_SpotMap2D>().pScene->enqueueDynamicDrawList(
        &iconDrawList );
}

void MngSpots::pause() {
    // FUN_007425b0
}

void MngSpots::terminate() {
    // FUN_00745f90
    delete[] pRouteWaypoints;
    pRouteWaypoints = nullptr;
    releaseSpotMaterial();
    pPicture = nullptr;
    gFlashCallbacksRegister.removeAllSynonyms( &MngSpots::PictureDrawCallback );
    mapScreens.release();
    TestDrive::FreeAligned( mapScreens.getBuffer() );
    mapScreens.setBuffer( nullptr );
    housePictureBank.release();
    TestDrive::FreeAligned( housePictureBank.getBuffer() );
    housePictureBank.setBuffer( nullptr );
    spotScreensBank.release();
    bookmarkScreenshots.destroy();
    onlineRecords.clear();
    records.clear();
    visibleRecords.clear();
    pSelectedRecord = nullptr;
    for ( DrawList *pDrawList :
          { &iconDrawList, &densityDrawList, &challengeConeDrawList } ) {
        if ( pDrawList->isInitialized() ) {
            pDrawList->destroy();
        }
    }
}

void MngSpots::tick( float deltaTime, float /*totalTime*/ ) {
    // FUN_0074c9d0
    if ( pMapPlayer == nullptr || spotScreensBank.isLoading() ) {
        return;
    }

    latchMapRects();
    iconDrawList.reset();
    densityDrawList.reset();
    challengeConeDrawList.reset();

    CameraGPS *pCameraGPS = FindGPSCamera();
    bDrawDensityMap = false;
    bool bUpdateSpots = pCameraGPS != nullptr;
    if ( pCameraGPS != nullptr ) {
        const bool bDensityFilter =
            ( gMapActiveFilterMode & 4 ) != 0 ||
            ( ( gMapActiveFilterMode & 0x10 ) != 0 && gMinimapFilterMode == 4 );
        if ( bDensityFilter &&
             pCameraGPS->getZoom() == CameraGPS::GZL_Island ) {
            bDrawDensityMap = true;
        }
        if ( pCameraGPS->getZoom() == CameraGPS::GZL_Space &&
             !pCameraGPS->isInTransition() ) {
            bUpdateSpots = false;
        }
    }

    if ( bUpdateSpots ) {
        updatePadNavigation( deltaTime );
        if ( bDrawDensityMap ) {
            pushDensityMap();
        }
        resetChallengeConeList();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (player name tags,
                                 // FUN_007463e0)
        updateSelection();
        updateChallengeInfoRequest( deltaTime );
        updatePicture( deltaTime );

        if ( gpMngLogoDisplay != nullptr ) {
            const int32_t atlas =
                ( pCameraGPS->getZoom() == CameraGPS::GZL_Island ) ? 3 : 1;
            Material *pAtlas = MngLogoDisplay::GetIconAtlasMaterial( atlas );
            if ( pAtlas != nullptr ) {
                iconDrawList.setActiveMaterial( pAtlas );
                if ( numVisibleRecords > 0 &&
                     iconDrawList.beginPrimitive(
                         PT_TriangleStrip,
                         numVisibleRecords * kIconVerticesPerRecord ) ) {
                    for ( int32_t i = 0; i < numVisibleRecords; i++ ) {
                        if ( visibleRecords[i] != nullptr ) {
                            pushIconQuads( visibleRecords[i] );
                        }
                    }
                    iconDrawList.commitPrimitive();
                }
            }
        }

        if ( pSelectedRecord != nullptr &&
             isInsideMapRect( pSelectedRecord->Position ) ) {
            openInfoCart();
        }

        refreshBrandLogos( false );
    }

    const float previousBrandClock = brandLogoClock;
    brandLogoClock += deltaTime;
    bBrandLogoSecondTicked = static_cast<int32_t>( previousBrandClock ) !=
                             static_cast<int32_t>( brandLogoClock );

    const float previousPanelClock = garagePanel.Clock;
    garagePanel.Clock += deltaTime;
    if ( static_cast<int32_t>( previousPanelClock ) !=
         static_cast<int32_t>( garagePanel.Clock ) ) {
        refreshGaragePanelText();
    }
}

void MngSpots::setFlashPlayer() {
    // FUN_0074a820
    const char *pPageName = nullptr;
    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Map:
        pPageName = "MAP";
        break;
    case GM_Club:
        pPageName = "CLUB";
        break;
    case GM_Lobby:
        pPageName = "CHALLENG";
        break;
    default:
        break;
    }
    if ( pPageName != nullptr ) {
        pMapPlayer = gpFlash->getFlashPlayer( pPageName );
    }

    refreshFilterCheckboxes();
    refreshSelectionCart();
    computeNeighbours( false );
    garagePanel.pPlayer = pMapPlayer;
}

void MngSpots::refreshDensityMap() {
    // FUN_00743aa0
    memset( densityMap, 0, sizeof( densityMap ) );
    const StreamingManager *pStreaming = gpWorld->getStreamingManager();
    const float cellWidth =
        static_cast<float>( static_cast<int32_t>(
            pStreaming->areaGrid.numAreas *
            pStreaming->sectorManager.numSectors * 0x300 ) ) *
        ( 1.0f / kNumDensityColumns );
    const float cellHeight =
        static_cast<float>( static_cast<int32_t>(
            pStreaming->areaGrid.numSubAreas *
            pStreaming->sectorManager.numSubSectors * 0x300 ) ) *
        ( 1.0f / kNumDensityRows );
    if ( pOwner != nullptr ) {
        pOwner->updateSpotFilterIcons();
    }

    if ( ( gMapActiveFilterMode & 4 ) != 0 ||
         ( ( gMapActiveFilterMode & 0x10 ) != 0 && gMinimapFilterMode == 4 ) ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        return;
    }
    if ( records.empty() ) {
        return;
    }

    for ( int32_t i = 0; i < numRecords; i++ ) {
        const MapSpotRecord &record = records[i];
        if ( !IsMapRecordVisible( &record, false ) ) {
            continue;
        }

        int32_t column = -1;
        for ( int32_t x = 0; x < kNumDensityColumns; x++ ) {
            if ( static_cast<float>( x ) * cellWidth > record.Position.x() ) {
                column = x;
                break;
            }
        }
        int32_t row = -1;
        for ( int32_t z = 0; z < kNumDensityRows; z++ ) {
            if ( static_cast<float>( z ) * cellHeight > record.Position.z() ) {
                row = z;
                break;
            }
        }
        if ( column != -1 && row != -1 ) {
            densityMap[column * kNumDensityRows + row]++;
        }
    }
}

void MngSpots::refreshFilterCheckboxes() {
    // FUN_00747a10
    if ( pMapPlayer == nullptr ) {
        return;
    }

    for ( const MapFilterCheckbox &checkbox : kMapFilterCheckboxes ) {
        const uint32_t mask = GetMapFilterMask( checkbox.Mode );
        pMapPlayer->setVariableValue(
            checkbox.pVariable,
            ( ( mask & checkbox.Mask ) == checkbox.Mask ) ? 1 : 0 );
    }

    auto setGroupRow = [&]( const uint32_t filterMask, const int32_t firstBit,
                            const char *pTextVariable,
                            const char *pImageVariable ) {
        char label[8] = {};
        for ( int32_t i = 0; i < kNumMapCarGroups; i++ ) {
            if ( ( filterMask & ( 1u << ( firstBit + i ) ) ) != 0 ) {
                gpMngFlashLocalize->setSysVar(
                    "GROUP", gpDatabase->getStringByHashcode(
                                 1, kMapCarGroups[i].Hashcode ) );
                strcpy( label, kMapCarGroups[i].pLabel );
                break;
            }
        }
        pMapPlayer->setVariableValueNonLinked( pImageVariable, label );
        gpMngFlashLocalize->setVarLocalized( pMapPlayer, pTextVariable,
                                             kGroupText );
        gpMngFlashLocalize->clearAllSysVar();
    };

    const uint32_t challengeAll = kGroupBitsMask << kChallengeGroupFirstBit;
    const uint32_t challengeGroups = gMapChallengeFilterMask & challengeAll;
    if ( challengeGroups == challengeAll ) {
        gpMngFlashLocalize->setVarLocalized(
            pMapPlayer, "/:f_t_ChallRestriction", kGroupAllText );
        pMapPlayer->setVariableValueNonLinked( "/:f_i_ChallRestriction",
                                               "all_groups" );
    } else if ( challengeGroups == 0 ) {
        gpMngFlashLocalize->setVarLocalized(
            pMapPlayer, "/:f_t_ChallRestriction", kGroupNoneText );
        pMapPlayer->setVariableValueNonLinked( "/:f_i_ChallRestriction",
                                               "all_groups" );
    } else {
        setGroupRow( gMapChallengeFilterMask, kChallengeGroupFirstBit,
                     "/:f_t_ChallRestriction", "/:f_i_ChallRestriction" );
    }

    const uint32_t playerAll = kGroupBitsMask << kPlayerGroupFirstBit;
    if ( ( gMapPlayerFilterMask & playerAll ) == playerAll ) {
        gpMngFlashLocalize->setVarLocalized( pMapPlayer, "/:f_t_PlayVtype",
                                             kPlayerGroupAllText );
        pMapPlayer->setVariableValueNonLinked( "/:f_i_PlayVtype",
                                               "all_groups" );
    } else {
        setGroupRow( gMapPlayerFilterMask, kPlayerGroupFirstBit,
                     "/:f_t_PlayVtype", "/:f_i_PlayVtype" );
    }

    const uint8_t filterMode = GetEffectiveMapFilterMode();
    int32_t menuIndex = 0;
    if ( ( filterMode & 1 ) != 0 ) {
        menuIndex = 1;
    } else if ( ( filterMode & 2 ) != 0 ) {
        menuIndex = 2;
    } else if ( ( filterMode & 4 ) != 0 ) {
        menuIndex = 3;
    } else if ( ( filterMode & 8 ) != 0 ) {
        menuIndex = 4;
    }
    if ( menuIndex != 0 ) {
        pMapPlayer->setVariableValue( "/:num_menu", menuIndex );
    }
}

void MngSpots::applyFilterCheckboxes() {
    // FUN_00744b40
    for ( const MapFilterCheckbox &checkbox : kMapFilterCheckboxes ) {
        const bool bChecked =
            pMapPlayer->getVariableAsInt( checkbox.pVariable ) != 0;
        const uint32_t mask = GetMapFilterMask( checkbox.Mode );
        SetMapFilterMask( checkbox.Mode, bChecked ? ( mask | checkbox.Mask )
                                                  : ( mask & ~checkbox.Mask ) );
    }
    refreshDensityMap();
    bResortRecords = true;
}

void MngSpots::cycleFilterRow( const bool bNext ) {
    // FUN_00748490
    char row[0x20] = {};
    if ( pMapPlayer->getVariableAsString( "/:WhatFilter", row ) ) {
        if ( strcmp( row, "chall_rest" ) == 0 ) {
            gMapChallengeFilterMask = CycleGroupBits(
                gMapChallengeFilterMask, kChallengeGroupFirstBit, true, bNext );
        } else if ( strcmp( row, "playVtype" ) == 0 ) {
            gMapPlayerFilterMask = CycleGroupBits(
                gMapPlayerFilterMask, kPlayerGroupFirstBit, false, bNext );
        }
    }
    refreshFilterCheckboxes();
}

void MngSpots::createSpotMaterial() {
    // FUN_007427d0
    static constexpr uint64_t kSpotMaterialHashcode = 0x73078865ca37336dull;

    pSpotMaterialData = TestDrive::Alloc( Render2DM::CalcSize( 0, 0 ) );
    if ( pSpotMaterialData == nullptr ) {
        return;
    }
    pSpotMaterial = Render2DM::Create( pSpotMaterialData, kSpotMaterialHashcode,
                                       0, 0, 0, 0 );
    pSpotMaterial->DepthTest = 2;
    pSpotMaterial->DepthWrite = 2;
    pSpotMaterial->SrcBlend = (eBlendMode)5;
    pSpotMaterial->DstBlend = (eBlendMode)6;
    pSpotMaterial->BlendOP = (eBlendOP)1;
    pSpotMaterial->AlphaSource = 0;
    pSpotMaterial->AlphaTest = (eComparisonFunction)0;
    ( (MaterialShaderParameterArray *)pSpotMaterial->getParameterByIndex( 0 ) )
        ->Flags.NumPrelight = 1;
    spot2DM.initialize( pSpotMaterialData );
    gpWorld->pushGlobal2DM( &spot2DM );
}

void MngSpots::releaseSpotMaterial() {
    // FUN_00742780
    if ( spot2DM.getHeader() != nullptr ) {
        gpWorld->removeGlobal2DM( &spot2DM );
        spot2DM.destroy();
    }
    if ( pSpotMaterialData != nullptr ) {
        TestDrive::Free( pSpotMaterialData );
        pSpotMaterialData = nullptr;
    }
    pSpotMaterial = nullptr;
}

void MngSpots::pushDensityMap() {
    // FUN_00745250
    if ( !bDrawDensityMap || pSpotMaterial == nullptr ) {
        return;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    const bool bOnline = false;
    if ( !bOnline ) {
        return;
    }

    if ( ( gMapActiveFilterMode & 4 ) != 0 ||
         ( ( gMapActiveFilterMode & 0x10 ) != 0 && gMinimapFilterMode == 4 ) ) {
        refreshDensityMap();
    }

    const StreamingManager *pStreaming = gpWorld->getStreamingManager();
    const float cellWidth =
        static_cast<float>( static_cast<int32_t>(
            pStreaming->areaGrid.numAreas *
            pStreaming->sectorManager.numSectors * 0x300 ) ) *
        ( 1.0f / kNumDensityColumns );
    const float cellHeight =
        static_cast<float>( static_cast<int32_t>(
            pStreaming->areaGrid.numSubAreas *
            pStreaming->sectorManager.numSubSectors * 0x300 ) ) *
        ( 1.0f / kNumDensityRows );

    Matrix4x4f_t shiftMatrix = ComputeWorldShiftMatrix();
    densityDrawList.reset();
    densityDrawList.setActiveMaterial( pSpotMaterial );
    densityDrawList.setActiveTransformMatrix( shiftMatrix );
    for ( int32_t x = 0; x < kNumDensityColumns; x++ ) {
        for ( int32_t z = 0; z < kNumDensityRows; z++ ) {
            const uint8_t density = densityMap[x * kNumDensityRows + z];
            if ( density == 0 ) {
                continue;
            }

            const uint32_t alpha = static_cast<uint8_t>(
                static_cast<uint32_t>( density * kDensityAlphaScale ) );
            PushOctagonFan( densityDrawList,
                            ( static_cast<float>( x ) + 0.5f ) * cellWidth,
                            static_cast<float>( density ) + kDensityHeight,
                            ( static_cast<float>( z ) + 0.5f ) * cellHeight,
                            cellWidth * kDensityHalfSizeScale,
                            cellHeight * kDensityHalfSizeScale,
                            ( alpha << 24 ) | kDensityPalette[density],
                            kDensityPalette[density] );
        }
    }
}

void MngSpots::resetChallengeConeList() {
    // FUN_007431e0
    if ( pSpotMaterial == nullptr ) {
        return;
    }
    Matrix4x4f_t shiftMatrix = ComputeWorldShiftMatrix();
    challengeConeDrawList.reset();
    challengeConeDrawList.setActiveMaterial( pSpotMaterial );
    challengeConeDrawList.setActiveTransformMatrix( shiftMatrix );
}

void MngSpots::pushChallengePopularity( const MapSpotRecord *pRecord ) {
    // FUN_00743840
    if ( !bDrawChallengeCones || pSpotMaterial == nullptr ||
         pRecord == nullptr || pRecord->pSpot == nullptr ) {
        return;
    }

    const GameIntroTutorial &tutorial = gpScenaric->getIntroTutorial();
    const bool bVisible = tutorial.isActive()
                              ? !tutorial.hidesSpot( pRecord->pSpot )
                              : IsSpotIconVisible( pRecord->Info, true );
    if ( !bVisible ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    const uint8_t popularity = 0;
    if ( popularity == 0 ) {
        return;
    }

    const float radius =
        ( static_cast<float>( popularity ) * ( 1.0f / 256.0f ) + 0.5f ) *
        kPopularityRadius;
    const uint32_t alpha = static_cast<uint8_t>( ( popularity >> 1 ) + 0x80 );
    PushOctagonFan( challengeConeDrawList, pRecord->pSpot->X,
                    static_cast<float>( popularity ) + kDensityHeight,
                    pRecord->pSpot->Z, radius, radius,
                    ( alpha << 24 ) | kPopularityColor, kPopularityColor );
}

static const DBEntryHouse *FindHouseBySpotHashcode( const uint64_t hashcode ) {
    // FUN_00742890
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gameDB.pNumItems[kHouseDatabaseIndex]; i++ ) {
        const DBEntryHouse &house = gameDB.DBHouse[i];
        if ( GetIdentifier64bit( house.SpotName.pAsString ) == hashcode ) {
            return &house;
        }
    }
    return nullptr;
}

void MngSpots::instanciate() {
    // FUN_0074b600
    pictureMutex.lock();
    if ( requestedBookmarkPictureID == -1 ) {
        pictureMutex.unlock();
        return;
    }

    char fileName[0x200];
    snprintf( fileName, sizeof( fileName ), "%d.jpg",
              requestedBookmarkPictureID );
    pictureMutex.unlock();

    Material *pMaterial =
        bookmarkScreenshots.loadFromFile( fileName, "bookmark" );

    pictureMutex.lock();
    showPicture( pMaterial );
    loadedBookmarkPictureID = requestedBookmarkPictureID;
    requestedBookmarkPictureID = -1;
    bPictureResolved = true;
    bPictureHidden = false;
    pictureMutex.unlock();
}

void MngSpots::showPicture( Material *pMaterial ) {
    pPicture = ( pMaterial != nullptr ) ? pMaterial
                                        : spotScreensBank.getMaterial( "undf" );
    if ( pPicture != nullptr ) {
        gFlashCallbacksRegister.add( "picture", &MngSpots::PictureDrawCallback,
                                     this );
    }
}

void MngSpots::updatePicture( const float deltaTime ) {
    // FUN_0074c430
    pictureTimer += deltaTime;
    if ( pSelectedRecord == nullptr ) {
        pictureMutex.lock();
        requestedBookmarkPictureID = -1;
        loadedBookmarkPictureID = -1;
        pictureMutex.unlock();
    }

    if ( pSelectedRecord != nullptr && pSelectedRecord->pSpot == nullptr &&
         pSelectedRecord->pExtraChallenge == nullptr &&
         pSelectedRecord->Info.Kind == MapSpotRecord::kKindBookmark &&
         requestedBookmarkPictureID == -1 && pictureTimer > 0.0f ) {
        const int32_t pictureID =
            bookmarks[pSelectedRecord->BookmarkIndex].PictureID;
        if ( pictureID == loadedBookmarkPictureID ) {
            pMapPlayer->setVariableValue( "/:InfosLoaded", 1 );
        } else {
            gFlashCallbacksRegister.removeAllSynonyms(
                &MngSpots::PictureDrawCallback );
            pictureMutex.lock();
            pMapPlayer->setVariableValue( "/:InfosLoaded", 0 );
            pPicture = nullptr;
            pPictureSource = nullptr;
            bPictureRequested = true;
            bPictureResolved = false;
            requestedBookmarkPictureID = pictureID;
            loadedBookmarkPictureID = -1;
            gpTestDriveInstance->pushResourceToStream( this );
            pictureMutex.unlock();
        }
    }

    if ( pSelectedRecord == nullptr ) {
        return;
    }
    const void *pSource =
        ( pSelectedRecord->pSpot != nullptr )
            ? static_cast<const void *>( pSelectedRecord->pSpot )
            : static_cast<const void *>( pSelectedRecord->pExtraChallenge );
    if ( pSource == nullptr || requestedBookmarkPictureID != -1 ) {
        return;
    }
    if ( pictureTimer <= 0.0f && pSource != pPictureSource &&
         pPicture != nullptr ) {
        return;
    }
    if ( bPictureRequested ) {
        return;
    }
    if ( pSource == pPictureSource && pPicture != nullptr ) {
        return;
    }

    gFlashCallbacksRegister.removeAllSynonyms( &MngSpots::PictureDrawCallback );
    pPicture = nullptr;
    loadedBookmarkPictureID = -1;
    if ( bPictureDrawn && !bPictureHidden ) {
        bPictureHidden = true;
        return;
    }
    if ( mapScreens.isLoading() || housePictureBank.isLoading() ) {
        return;
    }

    bPictureRequested = true;
    bPictureResolved = false;
    pPictureSource = pSource;

    bool bShowFallback = false;
    const uint8_t kind = pSelectedRecord->Info.Kind;
    if ( kind == MapSpotRecord::kKindSpot &&
         pSelectedRecord->pSpot->Type == kSpotTypeHouse ) {
        const DBEntryHouse *pHouse =
            FindHouseBySpotHashcode( pSelectedRecord->pSpot->Hashcode );
        if ( pHouse != nullptr ) {
            pPicture = nullptr;
            housePictureBank.release();
            bShowFallback =
                !housePictureBank.load( pHouse->SpotName.pAsString,
                                        &MngSpots::OnHousePictureLoaded, this );
        }
    } else {
        char bankName[0x40] = {};
        MapScreenshotBank::eCategory category = MapScreenshotBank::C_Spots;
        if ( ( kind & 1 ) == 0 ) {
            const SpotMap *pSpot = pSelectedRecord->pSpot;
            char sourceName[9] = {};
            memcpy( sourceName, &pSpot->SourceHashcode, 8 );
            GridPos2D sectorCell;
            GridPos2D areaCell;
            gpWorld->getStreamingManager()->sectorManager.computeCell(
                Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ),
                sectorCell, areaCell );
            snprintf( bankName, sizeof( bankName ), "%s_%d%d%d%d", sourceName,
                      areaCell.posX, areaCell.posY, sectorCell.posX,
                      sectorCell.posY );
        } else if ( !pSelectedRecord->isExtraChallenge() ) {
            memcpy( bankName, &pSelectedRecord->pSpot->Hashcode, 8 );
            category = MapScreenshotBank::C_IGE;
        } else {
            // NOTE: retail also calls getChallengeName() here and discards
            // the result.
            strncpy(
                bankName,
                pSelectedRecord->pExtraChallenge->getInstanceName().c_str(),
                sizeof( bankName ) - 1 );
            category = MapScreenshotBank::C_ExtraChallenges;
        }

        pPicture = nullptr;
        mapScreens.release();
        if ( !mapScreens.load( bankName, category, &MngSpots::OnMapScreenLoaded,
                               this ) ) {
            bPictureResolved = true;
            pPictureSource = pSource;
            bShowFallback = true;
        }
    }

    if ( bShowFallback ) {
        showPicture( nullptr );
    }

    bPictureHidden = pPicture != spotScreensBank.getMaterial( "undf" );
    bPictureDrawn = false;
}

void MngSpots::onMapScreenLoaded() {
    // FUN_00745a50
    if ( !bPictureRequested ) {
        return;
    }

    showPicture( mapScreens.getMaterial() );
    if ( pSelectedRecord != nullptr ) {
        bPictureRequested = true;
        bPictureResolved = true;
        bPictureHidden = false;
        return;
    }
    pPicture = nullptr;
    bPictureHidden = false;
}

void MngSpots::onHousePictureLoaded() {
    // FUN_00745b10
    showPicture( housePictureBank.getMaterial( "IMG_EXT" ) );
    bPictureHidden = false;
    if ( pSelectedRecord != nullptr ) {
        bPictureRequested = true;
        bPictureResolved = true;
        return;
    }
    pPicture = nullptr;
}

bool MngSpots::OnMapScreenLoaded( MapScreenshotBank * /*pBank*/,
                                  void *pUserData ) {
    // FUN_00745af0
    if ( pUserData != nullptr ) {
        static_cast<MngSpots *>( pUserData )->onMapScreenLoaded();
    }
    return true;
}

bool MngSpots::OnHousePictureLoaded( HousePictureBank * /*pBank*/,
                                     void *pUserData ) {
    // FUN_00745b90
    if ( pUserData != nullptr ) {
        static_cast<MngSpots *>( pUserData )->onHousePictureLoaded();
    }
    return true;
}

void MngSpots::PictureDrawCallback( FlashRECT * /*pViewport*/,
                                    float /*depth*/,
                                    DrawList *pDrawList,
                                    void *pUserData,
                                    FlashResource *pResource,
                                    const char * /*pClipName*/ ) {
    // FUN_00744c30
    MngSpots *pSpots = static_cast<MngSpots *>( pUserData );

    const char *pClipPath = "/datas/cart/picture";
    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Lobby:
        pClipPath = "spotname/picture";
        break;
    case GM_Club:
        pClipPath = "ss_menus/datas/cart_datas/picture";
        break;
    default:
        break;
    }

    if ( pSpots->pMapPlayer == nullptr ) {
        return;
    }

    FlashClip pictureClip;
    if ( !pSpots->pMapPlayer->getClipPlayer( pClipPath, &pictureClip ) ) {
        return;
    }

    FlashRECT viewport;
    pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    if ( pSpots->pPicture == nullptr || pSpots->bPictureHidden ) {
        return;
    }

    pSpots->bPictureDrawn = true;
    pDrawList->setActiveMaterial( pSpots->pPicture );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}

void MngSpots::refreshSelectionCart() {
    // FUN_00749830
    pPicture = nullptr;
    gFlashCallbacksRegister.removeAllSynonyms( &MngSpots::PictureDrawCallback );

    if ( pMapPlayer == nullptr ) {
        pictureTimer = 0.0f;
        bPictureRequested = false;
        bPictureResolved = false;
        return;
    }

    bool bCartRefreshed = false;
    if ( pSelectedRecord != nullptr ) {
        if ( ( pSelectedRecord->Info.Kind & 4 ) != 0 ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE
        } else if ( pSelectedRecord->pSpot == nullptr &&
                    pSelectedRecord->pExtraChallenge == nullptr ) {
            if ( pSelectedRecord->Info.Kind == MapSpotRecord::kKindBookmark ) {
                pMapPlayer->setVariableValue(
                    "/:SpotName",
                    bookmarks[pSelectedRecord->BookmarkIndex].pName );
                pMapPlayer->setVariableValueNonLinked(
                    "/:WhatSpot", kSpotDisplayNameBookmark );
                pMapPlayer->setVariableValueNonLinked(
                    "/:WhatSpotType",
                    GetSpotDisplayTypeName( PlayerGameMech::SDT_Solo ) );
                pMapPlayer->setVariableValue( "/:IsRoadLocked", 0 );
                setDistrictAddress();
            } else {
                pMapPlayer->setVariableValueNonLinked( "/:SpotName", "" );
                closeInfoCart();
            }
        } else {
            char spotName[0x20] = { '0', '\0' };
            getSpotDisplayName( pSelectedRecord, spotName );
            pMapPlayer->setVariableValueNonLinked( "/:SpotName", spotName );
            setDistrictAddress();

            int32_t typeIndex = PlayerGameMech::SDT_Solo;
            int32_t nameIndex = -1;
            const char *pWhatSpot = nullptr;
            if ( ( pSelectedRecord->Info.Kind &
                   MapSpotRecord::kKindBookmark ) != 0 ) {
                typeIndex = PlayerGameMech::SDT_Solo;
                pWhatSpot = kSpotDisplayNameBookmark;
            } else {
                if ( pSelectedRecord->pExtraChallenge != nullptr ) {
                    GetExtraChallengeDisplayIndices(
                        pSelectedRecord->pExtraChallenge, &typeIndex,
                        &nameIndex );
                } else {
                    GetSpotDisplayIndices( pSelectedRecord->pSpot, &typeIndex,
                                           &nameIndex );
                }
                pWhatSpot = ( typeIndex != -1 )
                                ? GetSpotDisplayName( nameIndex )
                                : GetSpotDisplayName( 0 );
            }
            strncpy( whatSpot, pWhatSpot, sizeof( whatSpot ) - 1 );
            pMapPlayer->setVariableValueNonLinked( "/:WhatSpot", whatSpot );
            pMapPlayer->setVariableValueNonLinked(
                "/:WhatSpotType", ( typeIndex != -1 )
                                      ? GetSpotDisplayTypeName( typeIndex )
                                      : GetSpotDisplayTypeName( 0 ) );

            const SavedRoadDiscovery *pRoads =
                gpPlayerData->getLoadedSave()->getRoadDiscovery();
            bool bRoadLocked = false;
            if ( pSelectedRecord->pExtraChallenge != nullptr ) {
                EntityExtraChallenge *pChallenge =
                    pSelectedRecord->pExtraChallenge;
                if ( pChallenge->getRoadSegment() == -1 ) {
                    pChallenge->computeRoadLocation();
                }
                if ( pChallenge->getRoadID() == -1 ) {
                    pChallenge->computeRoadLocation();
                }
                bRoadLocked =
                    IsRoadUnreachable( pRoads, pChallenge->getRoadID(),
                                       pChallenge->getRoadSegment() );
            } else if ( pSelectedRecord->pSpot != nullptr ) {
                const SpotMap *pSpot = pSelectedRecord->pSpot;
                bRoadLocked = pSpot->RoadID != 0 &&
                              IsRoadUnreachable( pRoads, pSpot->RoadID,
                                                 pSpot->RoadSegment );
            }
            pMapPlayer->setVariableValue( "/:IsRoadLocked",
                                          bRoadLocked ? 1 : 0 );
            refreshSpotInfoPanel();
        }

        const CameraGPS *pCameraGPS = FindGPSCamera();
        if ( !pCameraGPS->isInTransition() &&
             ( gpMapView == nullptr || !gpMapView->isMoving() ) ) {
            if ( !bInfoCartShown ) {
                openInfoCart();
            } else {
                switch ( gpTestDriveInstance->getActiveGameMode() ) {
                case GM_Map:
                    pMapPlayer->showFrame( "change", "/datas/cart", true );
                    break;
                case GM_Club:
                    pMapPlayer->showFrame( "change",
                                           "ss_menus/datas/cart_datas", true );
                    break;
                case GM_Lobby:
                    pMapPlayer->showFrame( "change", "spotname", true );
                    break;
                default:
                    break;
                }
            }
            bCartRefreshed = true;
        }
    }

    if ( !bCartRefreshed ) {
        closeInfoCart();
    }
    pictureTimer = 0.0f;
    bPictureRequested = false;
    bPictureResolved = false;
}

void MngSpots::updatePadNavigation( const float deltaTime ) {
    // FUN_0074b2f0
    if ( !bPadNavigationEnabled || gpConfig->NbMaxPlayers < 1 ||
         !gpFlash->FUN_0099b1e0( 3, 1 ) || !gpFlash->FUN_0099b1e0( 0, 1 ) ) {
        return;
    }
    if ( gpMngMapViewer != nullptr ) {
        const bool bPanLocked =
            gpMapView != nullptr &&
            gpTestDriveInstance->getActiveGameMode() == GM_Map &&
            !gpMapView->bEnabled;
        if ( bPanLocked || gpMngMapViewer->isPlayerListOpen() ) {
            return;
        }
    }

    const PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    int32_t direction = PD_Right;
    if ( ( pPlayerConfig->pMenuLeft->Flags & 1 ) != 0 ) {
        direction = PD_Left;
    } else if ( ( pPlayerConfig->pMenuRight->Flags & 1 ) == 0 ) {
        if ( ( pPlayerConfig->pMenuUp->Flags & 1 ) != 0 ) {
            direction = PD_Up;
        } else if ( ( pPlayerConfig->pMenuDown->Flags & 1 ) != 0 ) {
            direction = PD_Down;
        } else {
            padRepeatElapsed = 0.0f;
            padRepeatNext = 0.0f;
            return;
        }
    }

    padRepeatElapsed += deltaTime;
    if ( padRepeatElapsed < padRepeatNext ) {
        return;
    }
    if ( padRepeatNext > 0.0f ) {
        const float next = padRepeatNext;
        padRepeatNext = ( next - padRepeatPrevious ) * 0.5f + next;
        padRepeatPrevious = next;
    } else {
        padRepeatPrevious = padRepeatNext;
        padRepeatNext = kPadRepeatFirstDelay;
    }

    if ( pOwner != nullptr ) {
        pOwner->leaveFreeModeIfActive();
    }

    computeNeighbours( false );
    bool bHasNeighbour = false;
    for ( int32_t i = 0; i < kNumPadDirections; i++ ) {
        if ( pNeighbours[i] != nullptr ) {
            bHasNeighbour = true;
        }
    }
    if ( !bHasNeighbour ) {
        computeNeighbours( true );
    }

    MapSpotRecord *pNeighbour = pNeighbours[direction];
    if ( pNeighbour != nullptr ) {
        const Eigen::Vector4f previousPosition =
            ( pSelectedRecord != nullptr ) ? pSelectedRecord->Position
                                           : FindGPSCamera()->getFocus();
        pSelectedRecord = pNeighbour;

        Eigen::Vector4f corners[4];
        ComputeIconCorners( pNeighbour->Position, true, corners );
        if ( !bHasNeighbour && gpMapView != nullptr &&
             !isInsideMapRect( corners[0] ) && !isInsideMapRect( corners[1] ) &&
             !isInsideMapRect( corners[2] ) &&
             !isInsideMapRect( corners[3] ) ) {
            gpMapView->startMove( pNeighbour->Position, kPadMoveDuration );
            const float deltaZ =
                pNeighbour->Position.z() - previousPosition.z();
            const float deltaX =
                pNeighbour->Position.x() - previousPosition.x();
            if ( deltaZ * deltaZ + deltaX * deltaX > kWooshDistanceSq ) {
                gpFlash->getFlashSound()->playFrontEndSound( "woosh", 0,
                                                             false );
            }
        }
        refreshSelectionCart();
        gpFlash->getFlashSound()->stopGroup( "move_spt", -1.0f );
        gpFlash->getFlashSound()->playFrontEndSound( "move:move_spt", 0,
                                                     false );
    }
    computeNeighbours( false );
}

void MngSpots::computeNeighbours( const bool bIgnoreMapRect ) {
    // FUN_00749e70
    Eigen::Vector4f origin;
    if ( pSelectedRecord != nullptr ) {
        origin = pSelectedRecord->Position;
    } else if ( pOwner == nullptr ) {
        origin = focusPosition;
    } else {
        Matrix4x4f_t cursorFrame = Matrix4x4f_t::Identity();
        pOwner->computeCursorWorldFrame( cursorFrame );
        origin = cursorFrame.row( 3 ).transpose();
    }

    NeighbourList lists[kNumPadDirections];
    for ( int32_t direction = 0; direction < kNumPadDirections; direction++ ) {
        if ( ( gMapActiveFilterMode & 0xb ) != 0 ) {
            for ( int32_t i = 0; i < numRecords; i++ ) {
                MapSpotRecord *pRecord = &records[i];
                if ( !IsMapRecordVisible( pRecord, true ) ||
                     pRecord == pSelectedRecord ) {
                    continue;
                }

                Eigen::Vector4f corners[4];
                ComputeIconCorners( pRecord->Position, true, corners );
                if ( !bIgnoreMapRect && !isInsideMapRect( corners[0] ) &&
                     !isInsideMapRect( corners[1] ) &&
                     !isInsideMapRect( corners[2] ) &&
                     !isInsideMapRect( corners[3] ) ) {
                    continue;
                }

                const float score =
                    ScoreNeighbour( pRecord->Position, direction, origin );
                if ( score > 0.0f && score < kNoNeighbourScore ) {
                    lists[direction].insert( pRecord, score );
                }
            }
        }

        const bool bPlayerFilter =
            ( gMapActiveFilterMode & 4 ) != 0 ||
            ( ( gMapActiveFilterMode & 0x10 ) != 0 && gMinimapFilterMode == 4 );
        if ( bPlayerFilter ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        }
    }

    for ( int32_t a = 0; a < kNumPadDirections; a++ ) {
        for ( int32_t b = 0; b < kNumPadDirections; b++ ) {
            if ( a == b ) {
                continue;
            }
            const int32_t bestA = lists[a].findBest();
            const int32_t bestB = lists[b].findBest();
            if ( lists[a].pRecords[bestA] == nullptr ||
                 lists[a].pRecords[bestA] != lists[b].pRecords[bestB] ) {
                continue;
            }

            NeighbourList &loser =
                ( lists[b].Scores[bestB] <= lists[a].Scores[bestA] ) ? lists[a]
                                                                     : lists[b];
            const int32_t loserSlot = loser.findBest();
            loser.pRecords[loserSlot] = nullptr;
            loser.Scores[loserSlot] = kNoNeighbourScore;
            a = 0;
            b = 0;
        }
    }

    for ( int32_t direction = 0; direction < kNumPadDirections; direction++ ) {
        pNeighbours[direction] =
            lists[direction].pRecords[lists[direction].findBest()];
    }

    static constexpr int32_t kOppositeDirection[kNumPadDirections] = {
        PD_Down, PD_Up, PD_Left, PD_Right };
    for ( int32_t a = 0; a < kNumPadDirections; a++ ) {
        for ( int32_t b = 0; b < kNumPadDirections; b++ ) {
            if ( b == kOppositeDirection[a] || a == b ) {
                continue;
            }
            MapSpotRecord *pRecordA = pNeighbours[a];
            MapSpotRecord *pRecordB = pNeighbours[b];
            if ( pRecordA == nullptr || pRecordB == nullptr ) {
                continue;
            }

            Eigen::Vector4f deltaA = origin - pRecordA->Position;
            Eigen::Vector4f deltaB = origin - pRecordB->Position;
            deltaA.y() = 0.0f;
            deltaB.y() = 0.0f;
            if ( ( deltaA / deltaA.norm() ).dot( deltaB / deltaB.norm() ) <=
                 kNeighbourAlignment ) {
                continue;
            }

            const float scoreAA =
                ScoreNeighbour( pRecordA->Position, a, origin );
            const float scoreBB =
                ScoreNeighbour( pRecordB->Position, b, origin );
            const float scoreAB =
                ScoreNeighbour( pRecordA->Position, b, origin );
            const float scoreBA =
                ScoreNeighbour( pRecordB->Position, a, origin );
            if ( scoreAA >= kNoNeighbourScore || scoreBB >= kNoNeighbourScore ||
                 scoreAB >= kNoNeighbourScore ||
                 scoreBA >= kNoNeighbourScore ) {
                continue;
            }
            if ( scoreBB > scoreAA ) {
                pNeighbours[b] = pRecordA;
            } else {
                pNeighbours[a] = pRecordB;
            }
        }
    }
}

void MngSpots::openInfoCart() {
    // FUN_00743080
    if ( bInfoCartShown || !bInfoCartRequested || !bPadNavigationEnabled ) {
        return;
    }

    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Map:
    case GM_Lobby:
        pMapPlayer->showFrame( "datas_on", "/", true );
        break;
    case GM_Club:
        pMapPlayer->showFrame( "open", "ss_menus/datas", true );
        break;
    default:
        break;
    }
    bInfoCartShown = true;
}

void MngSpots::closeInfoCart() {
    // FUN_00743120
    if ( !bInfoCartShown || !bInfoCartRequested || !bPadNavigationEnabled ) {
        return;
    }

    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Map:
    case GM_Lobby:
        pMapPlayer->showFrame( "datas_off", "/", true );
        break;
    case GM_Club:
        pMapPlayer->showFrame( "close", "ss_menus/datas", true );
        break;
    default:
        break;
    }
    bInfoCartShown = false;
}

void MngSpots::refreshSpotInfoPanel() {
    // FUN_00748a60
    resetGaragePanel( pSelectedRecord );
    if ( pSelectedRecord == nullptr ) {
        return;
    }

    pMapPlayer->setVariableValue( "/:InfosLoaded", 1 );
    pMapPlayer->setVariableValue( "/:IsMyClub", 0 );
    pMapPlayer->setVariableValue( "/:reward_type", 0 );
    pMapPlayer->setVariableValue( "/:IsSpotLocked", 0 );
    pMapPlayer->setVariableValue( "/:vehicle_restriction", "ERROR" );
    pMapPlayer->setVariableValueNonLinked( "/:GroupRestriction01", "empty" );
    pMapPlayer->setVariableValueNonLinked( "/:GroupRestriction02", "empty" );

    const bool bExtraChallenge =
        ( pSelectedRecord->Info.ChallengeFlags & 0x10u ) != 0;
    if ( pSelectedRecord->Info.Kind == MapSpotRecord::kKindBookmark ||
         bExtraChallenge ) {
        if ( !bExtraChallenge ) {
            pMapPlayer->setVariableValue( "/:InfosLoaded", 0 );
        } else if ( pSelectedRecord->pExtraChallenge != nullptr ) {
            refreshExtraChallengePanel( pSelectedRecord->pExtraChallenge );
        }
    } else {
        const SpotMap *pSpot = pSelectedRecord->pSpot;
        const bool bChallenge = pSpot->Type == kSpotTypeChallenge;
        if ( bChallenge ) {
            pMapPlayer->getVariableAsString( "/:WhatSpot", whatSpot );
            pMapPlayer->setVariableValueNonLinked( "/:WhatSpot", "noSpot" );
        }
        pMapPlayer->setVariableValue( "/:IsChaLocked", bChallenge ? 1 : 0 );

        IPlayerData *pSave = gpPlayerData->getLoadedSave();
        const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        const bool bRentedCar =
            pCurrentCar != nullptr &&
            pCurrentCar->state == CarDesc::eState::CDS_Rental;
        switch ( pSpot->Type ) {
        case kSpotTypeHouse: {
            const GameDatabase &gameDB = gpDatabase->getGameDB();
            for ( uint32_t i = 0; i < gameDB.pNumItems[kHouseDatabaseIndex];
                  i++ ) {
                const DBEntryHouse &house = gameDB.DBHouse[i];
                if ( GetIdentifier64bit( house.SpotName.pAsString ) !=
                     pSpot->Hashcode ) {
                    continue;
                }

                if ( pSave->getGarage()->isHouseOwned( pSpot->Hashcode ) ) {
                    resetGaragePanel( pSelectedRecord );
                } else {
                    garagePanel.Clock = 0.0f;
                    garagePanel.HouseHashcode = ~0ull;
                    for ( const CarConfig *&pCar : garagePanel.pCars ) {
                        pCar = nullptr;
                    }

                    char price[32];
                    FormatMoneyString( price, house.PriceBuy );
                    pMapPlayer->setVariableValueNonLinked( "/:Price", price );
                    pMapPlayer->setVariableValueNonLinked( "/:garage_list_1",
                                                           "" );
                    pMapPlayer->setVariableValueNonLinked( "/:garage_list_2",
                                                           "" );
                    pMapPlayer->setVariableValueNonLinked( "/:garage_list_3",
                                                           "" );
                }
                pMapPlayer->setVariableValue( "/:Capacity",
                                              house.NumGarageSlot );
                break;
            }
        } break;
        case kSpotTypeBikeDealer:
            if ( IsSpotRewardLocked( kRewardBikeDealer ) ) {
                pMapPlayer->setVariableValue( "/:IsRoadLocked", 1 );
                pMapPlayer->setVariableValue( "/:reward_type", 1 );
                SetSpotLockedTextFromReward( pMapPlayer, kRewardBikeDealer );
            }
            [[fallthrough]];
        case kSpotTypeCarDealer:
            refreshBrandLogos( true );
            pMapPlayer->setVariableValue(
                "/:NbCars",
                gpDatabase->getNumCarsAvailableInShop(
                    gpDatabase->getCarShopBySpotName( pSpot->Hashcode ) ) );
            break;
        case kSpotTypeGoodies: {
            DBEntryCarShop *pShop =
                gpDatabase->getCarShopBySpotName( pSpot->Hashcode );
            if ( pShop == nullptr ) {
                pMapPlayer->setVariableValueNonLinked( "/:WhatBrand", "none" );
            } else {
                pMapPlayer->setVariableValue(
                    "/:WhatBrand",
                    GetBrandVariableName( pShop->Label.pAsString ) );
            }
        } break;
        case kSpotTypeAfterMarket:
            refreshTuningShopPanel( pSpot );
            break;
        case kSpotTypeClub: {
            const Driver *pDriver = pSave->getDriver();
            const uint64_t clubSpotHashcode =
                ( static_cast<uint64_t>( pDriver->field4_0x24 ) << 32 ) |
                pDriver->TeamSpotID;
            if ( clubSpotHashcode == pSpot->Hashcode ) {
                pMapPlayer->setVariableValue( "/:IsMyClub", 1 );
                pMapPlayer->setVariableValueNonLinked( "/:SpotName",
                                                       pDriver->ClubName );
            }
        } break;
        case kSpotTypeDriveIn:
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
            pMapPlayer->setVariableValue( "/:IsSpotLocked", 1 );
            break;
        case kSpotTypeChallenge:
            refreshChallengeInfoPanel( true );
            break;
        case kSpotTypeService:
            switch ( pSpot->SubType ) {
            case kServicePaint:
                if ( bRentedCar ) {
                    setSpotLocked( kLockedTextRentedCar );
                } else if ( currentCarNumColors <= 1 ) {
                    setSpotLocked( kLockedTextSingleColor );
                }
                break;
            case kServiceCarRental:
            case kServiceBikeRental:
                pMapPlayer->setVariableValue(
                    "/:NbCars",
                    gpDatabase->getNumCarsAvailableInShop(
                        gpDatabase->getCarShopBySpotName( pSpot->Hashcode ) ) );
                break;
            case kServiceEstates: {
                const GameDatabase &gameDB = gpDatabase->getGameDB();
                int32_t numHouses = 0;
                for ( uint32_t i = 0; i < gameDB.pNumItems[kHouseDatabaseIndex];
                      i++ ) {
                    if ( GetIdentifier64bit(
                             gameDB.DBHouse[i].RealtorHashcode.pAsString ) ==
                         pSpot->Hashcode ) {
                        numHouses++;
                    }
                }
                pMapPlayer->setVariableValue( "/:NbHouses", numHouses );
            } break;
            case kServicePaintDeluxe:
                if ( IsSpotRewardLocked( kRewardDeluxePaint ) ) {
                    pMapPlayer->setVariableValue( "/:reward_type", 1 );
                    SetSpotLockedTextFromReward( pMapPlayer,
                                                 kRewardDeluxePaint );
                } else if ( bRentedCar ) {
                    setSpotLocked( kLockedTextRentedCar );
                } else if ( bCurrentCarIsBike ) {
                    setSpotLocked( kLockedTextBikePaint );
                } else if ( !bCurrentCarDeluxePaint ) {
                    setSpotLocked( kLockedTextNoDeluxePaint );
                }
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    gFlashCallbacksRegister.removeAllSynonyms( &MngSpots::PictureDrawCallback );
    pPicture = nullptr;
    pictureTimer = 0.0f;
    bPictureRequested = false;
    bPictureResolved = false;
}

void MngSpots::setSpotLocked( const uint64_t lockedTextHashcode ) {
    pMapPlayer->setVariableValue( "/:IsSpotLocked", 1 );
    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pMapPlayer, "/:SpotLockedtext",
                                             lockedTextHashcode );
    }
}

void MngSpots::refreshTuningShopPanel( const SpotMap *pSpot ) {
    // FUN_00748a60 (inlined)
    static constexpr int32_t kMaxNumListedBrands = 8;

    DBEntryCarShop *pShop = gpDatabase->getCarShopBySpotName( pSpot->Hashcode );
    if ( pShop == nullptr ) {
        pMapPlayer->setVariableValueNonLinked( "/:brands_list", "" );
    } else {
        const DBEntryBrand *pListedBrands[kMaxNumListedBrands] = {};
        brandsList[0] = '\0';
        const int32_t numPacks = gpDatabase->getNumShopCarPacks( pShop );
        for ( int32_t i = 0; i < numPacks; i++ ) {
            const DBEntryCarPack *pPack =
                gpDatabase->getShopCarPack( pShop, i );
            const DBEntryCarPhysicsData *pCar =
                reinterpret_cast<const DBEntryCarPhysicsData *>(
                    pPack->CarHashcode );
            if ( !IsCarEntryValid( pCar ) ) {
                continue;
            }

            // FUN_0042de50
            bool bNewBrand = false;
            for ( const DBEntryBrand *&pListedBrand : pListedBrands ) {
                if ( pListedBrand == pCar->Brand ) {
                    break;
                }
                if ( pListedBrand == nullptr ) {
                    pListedBrand = pCar->Brand;
                    bNewBrand = true;
                    break;
                }
            }
            if ( !bNewBrand ) {
                continue;
            }

            if ( brandsList[0] != '\0' ) {
                strncat( brandsList, "\n",
                         sizeof( brandsList ) - strlen( brandsList ) - 1 );
            }
            strncat( brandsList, pCar->Brand->BrandNameID.pAsString,
                     sizeof( brandsList ) - strlen( brandsList ) - 1 );
        }
        pMapPlayer->setVariableValue( "/:brands_list", brandsList );
    }

    const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    if ( pCurrentCar == nullptr ) {
        return;
    }
    const int32_t carID = static_cast<int32_t>( pCurrentCar->getCarConfigID() );
    if ( !gpDatabase->isCarTunable( carID ) ) {
        setSpotLocked( kLockedTextNotTunable );
    } else if ( !gpDatabase->isCarTunableInShop( pSpot, carID ) ) {
        setSpotLocked( kLockedTextNotTunableHere );
    } else if ( pCurrentCar->state == CarDesc::eState::CDS_Rental ) {
        setSpotLocked( kLockedTextRentedCarTuning );
    }
}

void MngSpots::refreshExtraChallengePanel( EntityExtraChallenge *pChallenge ) {
    // FUN_00748a60 (inlined)
    char name[0x20] = {};
    if ( pChallenge->getChallengeName() != nullptr ) {
        strncpy( name, pChallenge->getChallengeName(), sizeof( name ) - 1 );
    }
    pMapPlayer->setVariableValueNonLinked( "/:SpotName", name );

    const SavedItemTable *pItemTable =
        gpPlayerData->getLoadedSave()->getItemTable();
    char reward[64];
    bool bCarOK = true;
    auto setRestriction =
        [&]( const uint32_t conditionType,
             const EntityExtraChallenge::ChallengeCondition &condition ) {
            pChallenge->buildVehicleRestrictionText(
                conditionType, condition, vehicleRestriction,
                sizeof( vehicleRestriction ), false );
            EntityExtraChallenge::SetGroupRestrictionVars(
                pMapPlayer, conditionType, condition );
            pMapPlayer->setVariableValue( "/:vehicle_restriction",
                                          vehicleRestriction );
            bCarOK = pChallenge->isCarAllowed( conditionType, condition );
        };

    switch ( pChallenge->getType() ) {
    case eEntityType::ET_HIHI: {
        const EntityHitchHiker *pHiker =
            static_cast<const EntityHitchHiker *>( pChallenge );
        setRestriction( pHiker->getConditionType(), pHiker->getCondition() );
        pMapPlayer->setVariableValue(
            "/:IsPerfect",
            pItemTable->isExtraChallengePerfect( pChallenge->getIndex() ) ? 1
                                                                          : 0 );
    } break;
    case eEntityType::ET_SHOG: {
        const EntityShoppingGirl *pGirl =
            static_cast<const EntityShoppingGirl *>( pChallenge );
        setRestriction( pGirl->getConditionType(), pGirl->getCondition() );
        pMapPlayer->setVariableValue(
            "/:IsPerfect",
            pItemTable->isExtraChallengePerfect( pChallenge->getIndex() ) ? 1
                                                                          : 0 );
    } break;
    case eEntityType::ET_TRAN: {
        const EntityTransporter *pTransporter =
            static_cast<const EntityTransporter *>( pChallenge );
        setRestriction( pTransporter->getConditionType(),
                        pTransporter->getCondition() );
        FormatMoneyString( reward, pTransporter->getReward() );
        pMapPlayer->setVariableValueNonLinked( "/:reward", reward );
    } break;
    case eEntityType::ET_CVOY: {
        const EntityConvoy *pConvoy =
            static_cast<const EntityConvoy *>( pChallenge );
        strncpy( vehicleRestriction,
                 gpDatabase->getStringByHashcode( 0, kConvoyRestriction ),
                 sizeof( vehicleRestriction ) - 1 );
        pMapPlayer->setVariableValue( "/:vehicle_restriction",
                                      vehicleRestriction );
        pMapPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                               "empty" );
        pMapPlayer->setVariableValueNonLinked( "/:GroupRestriction02",
                                               "empty" );
        FormatMoneyString( reward, pConvoy->getReward() );
        pMapPlayer->setVariableValueNonLinked( "/:reward", reward );
    } break;
    default:
        break;
    }

    pMapPlayer->setVariableValue( "/:IsCarOK", bCarOK ? 1 : 0 );
    pMapPlayer->setVariableValue(
        "/:IsDone",
        pItemTable->isExtraChallengeGold( pChallenge->getIndex() ) ? 1 : 0 );
    pMapPlayer->setVariableValueNonLinked( "/:BestScore", "" );

    Matrix4x4f_t startFrame;
    Matrix4x4f_t endFrame;
    Matrix4x4f_t startFrameCopy;
    float distance = 0.0f;
    pChallenge->computeRoadRoute( startFrame, endFrame, startFrameCopy,
                                  &distance );
    const char *pUnit = "km";
    if ( gpPlayerData->isUsingImperialUnits() ) {
        distance *= kKilometersToMiles;
        pUnit = "mi.";
    }
    char distanceText[64];
    snprintf( distanceText, sizeof( distanceText ), "%.1f %s",
              ceil( static_cast<double>( distance * 0.01f ) ) *
                  static_cast<double>( 0.1f ),
              pUnit );
    pMapPlayer->setVariableValueNonLinked( "/:Distance", distanceText );

    if ( gpMngTracRoad != nullptr ) {
        gpMngTracRoad->setNumWaypoints( 2 );
        ProjectOnRoad( pChallenge->getModelMatrix().row( 3 ).transpose(),
                       nullptr, nullptr, false,
                       gpMngTracRoad->getWaypoint( 0 ) );
        ProjectOnRoad( pChallenge->getGoalPosition(), nullptr, nullptr, false,
                       gpMngTracRoad->getWaypoint( 1 ) );
        gpMngTracRoad->solveLegs( false );
        pChallengeInfoSpot = nullptr;
    }
    pMapPlayer->setVariableValue( "/:IsChaLocked", 0 );
}

void MngSpots::refreshChallengeInfoPanel( const bool bReset ) {
    // FUN_00746910
    if ( pSelectedRecord == nullptr || pSelectedRecord->pSpot == nullptr ||
         pMapPlayer == nullptr ) {
        return;
    }

    if ( bReset ) {
        pMapPlayer->setVariableValue( "/:InfosLoaded", 0 );
        challengeInfoTimer = 0.0f;
        bChallengeInfoRequested = false;
        bChallengeInfoFromCache = false;
        pMapPlayer->setVariableValueNonLinked( "/:vehicle_restriction", "" );
        pMapPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                               "empty" );
        pMapPlayer->setVariableValueNonLinked( "/:GroupRestriction02",
                                               "empty" );
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: EventParameters (+0xe38): SpotName, IsSolo,
                        // requirements (FUN_00745d40), WhatCup, GoldReward,
                        // NbGames, then release the IGE buffer
}

void MngSpots::updateChallengeInfoRequest( const float deltaTime ) {
    // FUN_0074c8e0
    challengeInfoTimer += deltaTime;
    if ( pSelectedRecord == nullptr ) {
        return;
    }
    const SpotMap *pSpot = pSelectedRecord->pSpot;
    if ( pSpot == nullptr || pSpot->Type != kSpotTypeChallenge ) {
        return;
    }
    if ( challengeInfoTimer <= kChallengeInfoDelay &&
         pSpot != pChallengeInfoSpot ) {
        return;
    }

    if ( bChallengeInfoRequested ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        return;
    }
    if ( pSpot == pChallengeInfoSpot ) {
        bChallengeInfoFromCache = true;
        bChallengeInfoRequested = true;
        pMapPlayer->setVariableValueNonLinked( "/:WhatSpot", whatSpot );
        refreshChallengeInfoPanel( false );
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: Async IGE load of the challenge
                             // (FUN_008025e0 into EventParameters +0xe38;
                             // completion callback FUN_00746df0 ->
                             // FUN_00746be0)
}

void MngSpots::resetGaragePanel( const MapSpotRecord *pRecord ) {
    // FUN_007449b0
    garagePanel.Clock = 0.0f;
    garagePanel.HouseHashcode = ~0ull;
    for ( const CarConfig *&pCar : garagePanel.pCars ) {
        pCar = nullptr;
    }

    if ( pRecord != nullptr && pRecord->pSpot != nullptr &&
         pRecord->pSpot->Type == kSpotTypeHouse ) {
        garagePanel.HouseHashcode = pRecord->pSpot->Hashcode;
        fillGaragePanelCars();
        refreshGaragePanelText();
    }
}

void MngSpots::fillGaragePanelCars() {
    // FUN_007448d0
    if ( garagePanel.HouseHashcode == ~0ull ) {
        return;
    }
    for ( const CarConfig *&pCar : garagePanel.pCars ) {
        pCar = nullptr;
    }

    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    int32_t numCars = pGarage->getNumCarsInHouse( garagePanel.HouseHashcode );
    numCars = std::min( numCars, HouseGaragePanel::kMaxNumCars );
    for ( int32_t i = 0; i < numCars; i++ ) {
        const GarageSlot *pSlot =
            pGarage->getCarAtIndex( i, garagePanel.HouseHashcode, false );
        garagePanel.pCars[i] = gpDatabase->getCarConfig(
            static_cast<int32_t>( pSlot->getCarConfigID() ) );
    }
}

void MngSpots::refreshGaragePanelText() {
    // FUN_00744450
    FlashPlayer *pPlayer = garagePanel.pPlayer;
    if ( pPlayer == nullptr ) {
        return;
    }

    static constexpr const char *kLineVariables[] = {
        "/:garage_list1", "/:garage_list2", "/:garage_list3" };
    int32_t numCars = 0;
    for ( const CarConfig *pCar : garagePanel.pCars ) {
        if ( pCar != nullptr ) {
            numCars++;
        }
    }
    if ( numCars == 0 ) {
        for ( const char *pVariable : kLineVariables ) {
            pPlayer->setVariableValueNonLinked( pVariable, "" );
        }
        return;
    }

    const int32_t numPages = static_cast<int32_t>(
        ceil( static_cast<float>( numCars ) * kOneThird ) );
    const int32_t page = static_cast<int32_t>( garagePanel.Clock ) % numPages;
    const int32_t firstCar = page * HouseGaragePanel::kNumCarsPerPage;
    for ( char *pLine : garagePanel.Lines ) {
        pLine[0] = '\0';
    }
    for ( int32_t i = firstCar;
          i < firstCar + HouseGaragePanel::kNumCarsPerPage && i < numCars;
          i++ ) {
        if ( garagePanel.pCars[i] != nullptr ) {
            snprintf( garagePanel.Lines[i - firstCar],
                      HouseGaragePanel::kLineLength, "%d-%s", i + 1,
                      garagePanel.pCars[i]->FullName.c_str() );
        }
    }
    for ( int32_t i = 0; i < HouseGaragePanel::kNumCarsPerPage; i++ ) {
        pPlayer->setVariableValue( kLineVariables[i], garagePanel.Lines[i] );
    }
}

void MngSpots::refreshBrandLogos( const bool bForce ) {
    // FUN_00745bb0
    if ( !bBrandLogoSecondTicked && !bForce ) {
        return;
    }

    for ( int32_t i = 0; i < numRecords; i++ ) {
        MapSpotRecord &record = records[i];
        if ( record.pSpot == nullptr ||
             ( record.pSpot->Type != kSpotTypeCarDealer &&
               record.pSpot->Type != kSpotTypeBikeDealer ) ) {
            continue;
        }
        if ( record.NumBrandLogos < 1 ) {
            record.pCurrentBrandLogo = nullptr;
            continue;
        }
        record.pCurrentBrandLogo =
            record.pBrandLogos[static_cast<int32_t>( brandLogoClock ) %
                               record.NumBrandLogos];
        if ( pSelectedRecord == &record && gpMngLogoDisplay != nullptr &&
             record.pCurrentBrandLogo != nullptr ) {
            gpMngLogoDisplay->setLogo(
                pMapPlayer, record.pCurrentBrandLogo->BrandID.pAsString,
                nullptr );
        }
    }
}

void MngSpots::buildBrandLogoLists() {
    // FUN_00746b10
    DBEntryBrand *pBrands[MngLogoDisplay::kMaxNumCarShopBrands];
    for ( int32_t i = 0; i < numRecords; i++ ) {
        MapSpotRecord &record = records[i];
        if ( record.pSpot == nullptr ||
             ( record.pSpot->Type != kSpotTypeCarDealer &&
               record.pSpot->Type != kSpotTypeBikeDealer ) ) {
            continue;
        }

        MngLogoDisplay::GetCarShopBrands( pBrands, record.pSpot->Hashcode );
        record.NumBrandLogos = 0;
        while ( record.NumBrandLogos < MapSpotRecord::kMaxBrandLogos &&
                pBrands[record.NumBrandLogos] != nullptr ) {
            record.NumBrandLogos++;
        }
        for ( int32_t j = 0; j < record.NumBrandLogos; j++ ) {
            record.pBrandLogos[j] = pBrands[j];
        }
    }
}

void MngSpots::setDistrictAddress() {
    // FUN_00744ea0
    if ( gpMngSectorName == nullptr ) {
        return;
    }
    const char *pDistrict =
        gpMngSectorName->getSectorName( pSelectedRecord->Position );
    if ( pDistrict != nullptr ) {
        pMapPlayer->setVariableValueNonLinked( "/:Adress", pDistrict );
    }
}

const char *MngSpots::getSpotKindLabel( const SpotIconInfo &info ) {
    // FUN_00743e20
    const char *pVariable = nullptr;
    if ( info.Kind == 2 ) {
        switch ( info.SpotFlags ) {
        case 0x8:
            pVariable = "/:m_CarDealer";
            break;
        case 0x10:
            pVariable = "/:m_BikeDealer";
            break;
        case 0x80:
            pVariable = "/:m_AfterMarket";
            break;
        case 0x1:
        case 0x2:
            pVariable = "/:m_Home";
            break;
        case 0x800:
            pVariable = "/:m_CarRental";
            break;
        case 0x400:
            pVariable = "/:m_Club";
            break;
        case 0x200:
            pVariable = "/:m_DriveIn";
            break;
        case 0x4:
            pVariable = "/:m_Estates";
            break;
        case 0x100:
            pVariable = "/:m_Goodies";
            break;
        case 0x40:
            pVariable = "/:m_CarWash";
            break;
        case 0x20:
            pVariable = "/:m_CarPaint";
            break;
        default:
            break;
        }
    } else if ( info.Kind == 1 ) {
        switch ( info.ChallengeFlags ) {
        case 0x4:
            pVariable = "/:m_Drive";
            break;
        case 0x2:
            pVariable = "/:m_speed";
            break;
        case 0x8:
            pVariable = "/:m_race";
            break;
        default:
            break;
        }
    }

    if ( pVariable == nullptr ) {
        return "";
    }
    pMapPlayer->getVariableAsString( pVariable, spotKindLabel );
    return spotKindLabel;
}

void MngSpots::getSpotDisplayName( const MapSpotRecord *pRecord,
                                   char *pOutName ) {
    // FUN_00745770
    static constexpr size_t kNameLength = 0x20;
    strncpy( pOutName, getSpotKindLabel( pRecord->Info ), kNameLength );

    const SpotMap *pSpot = pRecord->pSpot;
    if ( pSpot == nullptr ) {
        if ( pRecord->pExtraChallenge == nullptr ) {
            strncpy( pOutName, bookmarks[pRecord->BookmarkIndex].pName,
                     kNameLength );
        }
        return;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint8_t type = pSpot->Type;
    const uint8_t subType = pSpot->SubType;
    const bool bCarShopLabel =
        type == 1 || type == 2 || type == 4 ||
        ( type == 8 &&
          ( subType == 2 || subType == 3 || subType == 1 || subType == 9 ) );
    if ( bCarShopLabel ) {
        DBEntryCarShop *pCarShop =
            gpDatabase->getCarShopBySpotName( pSpot->Hashcode );
        if ( pCarShop != nullptr ) {
            strncpy( pOutName, pCarShop->Label.pAsString, kNameLength );
        }
        return;
    }

    if ( type == 3 ) {
        const DBEntryCarShop *pCarShop = gameDB.DBCarShop;
        const DBEntryCarShop *pCarShopEnd =
            pCarShop + gameDB.pNumItems[kCarShopDatabaseIndex];
        for ( ; pCarShop < pCarShopEnd; pCarShop++ ) {
            if ( GetIdentifier64bit( pCarShop->SpotName.pAsString ) !=
                 pSpot->Hashcode ) {
                continue;
            }
            const DBEntryBrand *pBrand = gameDB.DBBrand;
            const DBEntryBrand *pBrandEnd =
                pBrand + gameDB.pNumItems[kBrandDatabaseIndex];
            for ( ; pBrand < pBrandEnd; pBrand++ ) {
                if ( strcmp( pBrand->BrandID.pAsString,
                             pCarShop->Label.pAsString ) == 0 ) {
                    strncpy( pOutName, pBrand->BrandNameID.pAsString,
                             kNameLength );
                    break;
                }
            }
        }
        return;
    }

    if ( type == kSpotTypeChallenge ) {
        pOutName[0] = '\0';
        return;
    }
    if ( type != 0 ) {
        return;
    }

    const DBEntryHouse *pHouse = gameDB.DBHouse;
    const DBEntryHouse *pHouseEnd =
        pHouse + gameDB.pNumItems[kHouseDatabaseIndex];
    for ( ; pHouse < pHouseEnd; pHouse++ ) {
        if ( GetIdentifier64bit( pHouse->SpotName.pAsString ) ==
             pSpot->Hashcode ) {
            strncpy( pOutName, pHouse->HouseName.pAsString, kNameLength );
            return;
        }
    }
}

Eigen::Vector4f MngSpots::computeSelectionAnchor() const {
    // FUN_00744ed0
    const Matrix4x4f_t &camToWorld =
        gpCamera->getDefaultCamera()->getCamToWorld();
    const Eigen::Vector4f up = camToWorld.row( 1 ).transpose();
    return pSelectedRecord->Position +
           up / up.norm() * ComputeSpotIconSize( FindGPSCamera() );
}

bool MngSpots::computeSelectionMoviePosition( float &outX, float &outY ) const {
    // FUN_00744fb0
    if ( pSelectedRecord == nullptr ) {
        return false;
    }
    ProjectToMapMovie( FindGPSCamera(), computeSelectionAnchor(), outX, outY );
    return true;
}

uint32_t MngSpots::computeIconColor( const MapSpotRecord *pRecord ) const {
    // FUN_00746e10
    static constexpr uint32_t kColorWhite = 0xffffffffu;
    static constexpr uint32_t kColorHidden = 0x00ffffffu;
    static constexpr uint32_t kColorDimmed = 0x7dffffffu;

    if ( !IsMapRecordVisible( pRecord, true ) ) {
        return kColorHidden;
    }

    const SpotMap *pSpot = pRecord->pSpot;
    if ( pSpot == nullptr && pRecord->pExtraChallenge == nullptr ) {
        if ( pRecord->Info.Kind == 4 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (red when the player
                                     // cannot be joined)
        }
        return kColorWhite;
    }

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    if ( pSpot != nullptr ) {
        if ( pSpot->Type == 2 &&
             pSave->getAchievements()->isRewardLocked( kRewardBikeDealer ) ) {
            return kColorDimmed;
        }
        if ( pSpot->Type == 6 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
            return kColorDimmed;
        }
        if ( pSpot->Type == 8 && pSpot->SubType == 9 &&
             pSave->getAchievements()->isRewardLocked( kRewardDeluxePaint ) ) {
            return kColorDimmed;
        }
        if ( pSpot->Type == 8 &&
             ( ( pSpot->SubType == 9 &&
                 ( bCurrentCarIsBike || !bCurrentCarDeluxePaint ) ) ||
               ( pSpot->SubType == 1 && currentCarNumColors <= 1 ) ) ) {
            return kColorDimmed;
        }
    }

    const SavedRoadDiscovery *pRoads = pSave->getRoadDiscovery();
    int32_t roadID = 0;
    int32_t roadSegment = 0;
    if ( ( pRecord->Info.ChallengeFlags & 0x10u ) == 0 ) {
        roadID = pSpot->RoadID;
        roadSegment = pSpot->RoadSegment;
    } else {
        EntityExtraChallenge *pChallenge = pRecord->pExtraChallenge;
        if ( pChallenge == nullptr ) {
            return kColorWhite;
        }
        if ( pChallenge->getRoadID() == -1 ) {
            pChallenge->computeRoadLocation();
        }
        roadID = pChallenge->getRoadID();
        if ( pChallenge->getRoadSegment() == -1 ) {
            pChallenge->computeRoadLocation();
        }
        roadSegment = pChallenge->getRoadSegment();
    }

    if ( roadID != 0 && IsRoadUnreachable( pRoads, roadID, roadSegment ) &&
         !gpScenaric->getIntroTutorial().isActive() ) {
        return kColorDimmed;
    }
    return kColorWhite;
}

void MngSpots::pushIconQuad( const Eigen::Vector4f screenCorners[4],
                             const uint32_t color,
                             const float u0,
                             const float u1,
                             const float v0,
                             const float v1,
                             const float offsetY ) {
    iconDrawList.setDiffuse( 0, ColorRGBA( color ) );
    iconDrawList.setTexCoords( 0, Eigen::Vector2f( u0, v1 ) );
    iconDrawList.pushVertex( screenCorners[0].x(),
                             screenCorners[0].y() + offsetY,
                             screenCorners[0].z() );
    iconDrawList.setTexCoords( 0, Eigen::Vector2f( u0, v0 ) );
    iconDrawList.pushVertex( screenCorners[1].x(),
                             screenCorners[1].y() + offsetY,
                             screenCorners[1].z() );
    iconDrawList.setTexCoords( 0, Eigen::Vector2f( u1, v1 ) );
    iconDrawList.pushVertex( screenCorners[2].x(),
                             screenCorners[2].y() + offsetY,
                             screenCorners[2].z() );
    iconDrawList.setTexCoords( 0, Eigen::Vector2f( u1, v0 ) );
    iconDrawList.pushVertex( screenCorners[3].x(),
                             screenCorners[3].y() + offsetY,
                             screenCorners[3].z() );
    iconDrawList.breakPrimitive();
}

void MngSpots::pushIconQuads( const MapSpotRecord *pRecord ) {
    // FUN_00747030
    Eigen::Vector4f corners[4];
    ComputeIconCorners( pRecord->Position, true, corners );
    if ( gpMngLogoDisplay == nullptr ) {
        return;
    }

    const CameraGPS *pCameraGPS = FindGPSCamera();
    const int32_t atlas =
        ( pCameraGPS->getZoom() == CameraGPS::GZL_Island ) ? 3 : 1;

    Eigen::Vector4f screenCorners[4];
    for ( int32_t i = 0; i < 4; i++ ) {
        float x = 0.0f;
        float y = 0.0f;
        ProjectToMapScreen( corners[i], nullptr, x, y );
        screenCorners[i] = Eigen::Vector4f( x, y, 0.0f, 0.0f );
    }

    const uint32_t color = computeIconColor( pRecord );
    const SpotMap *pSpot = pRecord->pSpot;
    if ( pSpot != nullptr && pSpot->Type == kSpotTypeChallenge &&
         ( pSpot->SubType & 0xf ) == 1 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    float u0 = 0.0f;
    float u1 = 0.0f;
    float v0 = 0.0f;
    float v1 = 0.0f;
    MinimapSpotIcon icon;
    icon.Position = pRecord->Position;
    icon.Info = pRecord->Info;
    icon.Index = pRecord->ID;
    icon.Unknown0x30 = pRecord->BookmarkIndex;
    icon.pSpot = pSpot;
    icon.pEntity = pRecord->pExtraChallenge;
    icon.pBrand = pRecord->pCurrentBrandLogo;
    MngLogoDisplay::GetSpotIconUVs( &icon, atlas, u0, u1, v0, v1 );
    pushIconQuad( screenCorners, color, u0, u1, v0, v1, 0.0f );

    if ( pSpot == nullptr && pRecord->pExtraChallenge == nullptr ) {
        return;
    }
    const bool bExtraChallenge = ( pRecord->Info.ChallengeFlags & 0x10u ) != 0;
    if ( !bExtraChallenge && pSpot->Type != kSpotTypeChallenge ) {
        return;
    }
    if ( atlas != 1 && pCameraGPS->getZoom() != CameraGPS::GZL_Far ) {
        return;
    }

    int32_t badgeAtlas = atlas;
    int32_t badgeIndex = -1;
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    if ( !bExtraChallenge ) {
        if ( pSpot->Type != 6 && !( pSpot->Type == kSpotTypeChallenge &&
                                    ( pSpot->SubType & 0xf ) == 0 ) ) {
            return;
        }

        switch ( pSave->getRanking()->getChallengeResult( pSpot->Hashcode ) ) {
        case 1:
            badgeIndex = ( atlas == 1 ) ? 0x60 : 0x20;
            break;
        case 2:
            badgeIndex = ( atlas == 1 ) ? 0x5f : 0x1f;
            break;
        case 3:
            badgeIndex = ( atlas == 1 ) ? 0x5e : 0x1e;
            break;
        default:
            return;
        }
    } else {
        EntityExtraChallenge *pChallenge = pRecord->pExtraChallenge;
        if ( pChallenge == nullptr ) {
            return;
        }

        const SavedItemTable *pItemTable = pSave->getItemTable();
        if ( !pItemTable->isExtraChallengeGold( pChallenge->getIndex() ) ) {
            if ( pChallenge->getType() != eEntityType::ET_SHOG &&
                 pChallenge->getType() != eEntityType::ET_HIHI ) {
                return;
            }
            badgeIndex = ( atlas == 1 ) ? 0x61 : 0x21;
        } else if ( !pItemTable->isExtraChallengePerfect(
                        pChallenge->getIndex() ) ) {
            badgeIndex = ( atlas == 1 ) ? 0x62 : 0x22;
        } else {
            badgeIndex = ( atlas == 1 ) ? 0x63 : 0x23;
        }
    }
    if ( badgeIndex == -1 || ( atlas != 1 && atlas != 2 ) ) {
        return;
    }

    if ( MngLogoDisplay::GetIconAtlasUVs( badgeAtlas, badgeIndex, u0, u1, v0,
                                          v1 ) != nullptr ) {
        pushIconQuad( screenCorners, color, u0, u1, v0, v1, 0.0f );
    }
}

bool MngSpots::isInsideMapRect( const Eigen::Vector4f &position ) const {
    // FUN_00743c90
    if ( !bMapRectsLatched ) {
        return false;
    }

    float x = 0.0f;
    float y = 0.0f;
    ProjectToMapScreen( position, nullptr, x, y );
    return freeModeRect.X < x && x < freeModeRect.X + freeModeRect.Width &&
           freeModeRect.Y < y && y < freeModeRect.Y + freeModeRect.Height;
}

bool MngSpots::isCursorOverRecord( const MapSpotRecord *pRecord,
                                   const Eigen::Vector2f &cursor ) const {
    // FUN_00745c90
    Eigen::Vector4f corners[4];
    ComputeIconCorners( pRecord->Position, true, corners );

    Eigen::Vector2f screenCorners[4];
    for ( int32_t i = 0; i < 4; i++ ) {
        ProjectToMapScreen( corners[i], nullptr, screenCorners[i].x(),
                            screenCorners[i].y() );
    }

    const Eigen::Vector2f &topLeft = screenCorners[1];
    const Eigen::Vector2f &bottomRight = screenCorners[2];
    return topLeft.x() <= cursor.x() && cursor.x() <= bottomRight.x() &&
           topLeft.y() <= cursor.y() && cursor.y() <= bottomRight.y();
}

bool MngSpots::findRecordUnderCursor( const Eigen::Vector2f *pCursor,
                                      MapSpotRecord **ppOutRecord ) const {
    // FUN_00746840
    if ( pCursor == nullptr ) {
        return false;
    }

    for ( int32_t i = 0; i < numVisibleRecords; i++ ) {
        MapSpotRecord *pRecord = visibleRecords[i];
        if ( !IsMapRecordVisible( pRecord, true ) ||
             !isCursorOverRecord( pRecord, *pCursor ) ) {
            continue;
        }
        if ( ppOutRecord != nullptr ) {
            *ppOutRecord = pRecord;
        }
        return true;
    }
    return false;
}

bool MngSpots::isCursorOverSelection( const Eigen::Vector2f &cursor ) const {
    // FUN_007468f0
    return pSelectedRecord != nullptr &&
           isCursorOverRecord( pSelectedRecord, cursor );
}

void MngSpots::buildRecords() {
    // FUN_0074c170
    MngBookmark::LoadBookmarkPositions( numBookmarks, bookmarks );

    numRecords =
        numBookmarks + gpScenaric->getNumExtraChallenges() +
        static_cast<int32_t>( gpScenaric->getSpotList().getNumWorldSpots() );
    records.assign( numRecords, MapSpotRecord() );
    visibleRecords.assign( numRecords + kNumExtraVisibleRecords, nullptr );
    numVisibleRecords = 0;
    onlineRecords.assign( kNumOnlineRecords, MapSpotRecord() );
    numOnlineRecords = 0;

    int32_t numIconRecords = numRecords + kNumExtraVisibleRecords;
    const int32_t numWorldSpots =
        static_cast<int32_t>( gpScenaric->getSpotList().getNumWorldSpots() );
    for ( int32_t i = 0; i < numWorldSpots; i++ ) {
        const SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpot( i );
        if ( pSpot != nullptr && pSpot->Type == kSpotTypeChallenge ) {
            numIconRecords += ( ( pSpot->SubType & 0xf ) == 1 ) ? 2 : 1;
        }
    }
    DrawStreams iconStreams;
    iconStreams.Diffuse = 1;
    iconStreams.UVMap = 1;
    iconStreams.bIndex16 = 1;
    iconDrawList.initialize( 1, numIconRecords * 9 + 1, numIconRecords * 3,
                             true, &iconStreams, nullptr, false );

    DrawStreams densityStreams;
    densityStreams.Diffuse = 1;
    densityDrawList.initialize( kNumDensityColumns * kNumDensityRows + 1,
                                kNumDensityColumns * kNumDensityRows * 10 + 3,
                                0, true, &densityStreams, nullptr, false );

    DrawStreams coneStreams;
    coneStreams.Diffuse = 1;
    coneStreams.bIndex16 = 1;
    challengeConeDrawList.initialize( numWorldSpots + 1, numWorldSpots * 10 + 3,
                                      0, true, &coneStreams, nullptr, false );

    createSpotMaterial();
    fillRecords();
    refreshDensityMap();
    buildBrandLogoLists();
}

float MngSpots::computeSpotHeight( const float x, const float z ) const {
    const Eigen::Vector4f boxMin( x - kSpotHeightQueryHalfSize,
                                  kSpotHeightQueryMinY,
                                  z - kSpotHeightQueryHalfSize, 1.0f );
    const Eigen::Vector4f boxMax( x + kSpotHeightQueryHalfSize,
                                  kSpotHeightQueryMaxY,
                                  z + kSpotHeightQueryHalfSize, 1.0f );
    Eigen::Vector4f vertices[kSpotHeightQueryVertices];
    const int32_t numVertices = GetLevelHeightmapTrianglesInBBox(
        boxMin, boxMax, vertices, kSpotHeightQueryVertices, true );

    float height = 0.0f;
    for ( int32_t i = 0; i < numVertices; i++ ) {
        if ( height < vertices[i].y() ) {
            height = vertices[i].y();
        }
    }
    return height;
}

void MngSpots::fillRecords() {
    // FUN_0074b7b0
    const SectorManager &sectorManager =
        gpWorld->getStreamingManager()->sectorManager;
    const float cellSizeX = static_cast<float>(
        static_cast<int32_t>( sectorManager.numSectors * 0x300 ) );
    const float cellSizeZ = static_cast<float>(
        static_cast<int32_t>( sectorManager.numSubSectors * 0x300 ) );

    Spots &spotList = gpScenaric->getSpotList();
    const int32_t numWorldSpots =
        static_cast<int32_t>( spotList.getNumWorldSpots() );
    const GameIntroTutorial &tutorial = gpScenaric->getIntroTutorial();
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    const SavedRoadDiscovery *pRoads = pSave->getRoadDiscovery();
    const eGameMode activeGameMode = gpTestDriveInstance->getActiveGameMode();

    int32_t numKept = 0;
    for ( int32_t i = 0; i < numWorldSpots; i++ ) {
        const SpotMap *pSpot = spotList.getWorldSpot( i );
        bool bHidden =
            IsSpotHidden( pSpot->Hashcode ) || tutorial.hidesSpot( pSpot );

        const int32_t tutorialStep = tutorial.getStep();
        const bool bTutorialShowsRoad =
            tutorial.isActive() &&
            ( tutorialStep == kTutorialStateHouseBought ||
              tutorialStep == kTutorialStateCarBought ||
              tutorialStep == kTutorialStateHome );
        const bool bRoadLocked =
            pSpot->RoadID != 0 &&
            !( !pRoads->isRoadLocked( pSpot->RoadID, pSpot->RoadSegment ) &&
               IsRoadAvailable( pSpot->RoadID ) ) &&
            !bTutorialShowsRoad;

        bool bKeep = true;
        if ( pSpot->Type == kSpotTypeChallenge ) {
            const int32_t playerLevel =
                pSave->getAchievements()->getPlayerLevel();
            const int32_t minLevel =
                static_cast<int32_t>( pSpot->MinLevelRequired ) - 1;
            if ( bRoadLocked ) {
                if ( playerLevel < minLevel || minLevel == kMaxPlayerLevel ) {
                    bHidden = true;
                }
                if ( activeGameMode == GM_Lobby || activeGameMode == GM_Club ) {
                    bKeep = false;
                }
            }
        } else if ( bRoadLocked && pSpot->Type != kSpotTypeCarDealer &&
                    pSpot->Type != kSpotTypeBikeDealer ) {
            bKeep = false;
        }

        if ( !bKeep || bHidden ) {
            numRecords--;
            continue;
        }

        MapSpotRecord &record = records[numKept];
        record.Position = Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
        record.Position.y() = computeSpotHeight( pSpot->X, pSpot->Z );
        record.ID = i;
        record.BookmarkIndex = -1;
        record.CellX = static_cast<int32_t>( record.Position.x() / cellSizeX );
        record.CellZ = static_cast<int32_t>( record.Position.z() / cellSizeZ );
        record.pSpot = pSpot;
        GetSpotIconInfo( pSpot, record.Info );
        numKept++;
    }
    numWorldSpotRecords = numKept;

    int32_t numKeptExtraChallenges = 0;
    const int32_t numExtraChallenges = gpScenaric->getNumExtraChallenges();
    for ( int32_t i = 0; i < numExtraChallenges; i++ ) {
        EntityExtraChallenge *pChallenge = static_cast<EntityExtraChallenge *>(
            gpScenaric->findExtraChallenge( i ) );
        if ( pChallenge->getRoadID() == -1 ) {
            pChallenge->computeRoadLocation();
        }
        if ( pChallenge->getRoadSegment() == -1 ) {
            pChallenge->computeRoadLocation();
        }

        if ( pRoads->isRoadLocked( pChallenge->getRoadID(),
                                   pChallenge->getRoadSegment() ) ||
             !IsRoadAvailable( pChallenge->getRoadID() ) ||
             !pChallenge->isAvailable() ) {
            numRecords--;
            continue;
        }

        MapSpotRecord &record =
            records[numWorldSpotRecords + numKeptExtraChallenges];
        record.Position = pChallenge->getModelMatrix().row( 3 ).transpose();
        record.Position.y() =
            computeSpotHeight( record.Position.x(), record.Position.z() );
        record.ID = numWorldSpots + numKeptExtraChallenges;
        record.BookmarkIndex = -1;
        record.CellX = static_cast<int32_t>( record.Position.x() / cellSizeX );
        record.CellZ = static_cast<int32_t>( record.Position.z() / cellSizeZ );
        record.pExtraChallenge = pChallenge;
        pChallenge->getIconInfo( record.Info );
        numKeptExtraChallenges++;
    }

    const float invCellSizeX = 1.0f / cellSizeX;
    const float invCellSizeZ = 1.0f / cellSizeZ;
    for ( int32_t i = 0; i < numBookmarks; i++ ) {
        const Bookmark &bookmark = bookmarks[i];
        MapSpotRecord &record =
            records[numWorldSpotRecords + numKeptExtraChallenges + i];
        record.Position =
            Eigen::Vector4f( bookmark.Position.x(), bookmark.Position.y(),
                             bookmark.Position.z(), 1.0f );
        record.ID = numWorldSpots + i + numKeptExtraChallenges;
        record.CellX =
            static_cast<int32_t>( record.Position.x() * invCellSizeX );
        record.CellZ =
            static_cast<int32_t>( record.Position.z() * invCellSizeZ );
        record.BookmarkIndex = i;
        record.pSpot = nullptr;
        record.Info.Kind = MapSpotRecord::kKindBookmark;
        record.Info.ChallengeFlags = 0u;
        record.Info.SpotFlags = 0u;
        record.Info.Unknown0xC = 0u;
        record.Info.Unknown0x10 = 0xffffffffu;
    }

    std::qsort( records.data(), numRecords, sizeof( MapSpotRecord ),
                &CompareRecordsByDepth );

    if ( pOwner != nullptr ) {
        Eigen::Vector4f unusedFocus =
            gpScenaric->getLocalPlayer().getLastPosition();
        gpScenaric->getIntroTutorial().computeMapFocus( pOwner, &unusedFocus,
                                                        true );
    }
}

void MngSpots::deleteSelectedBookmark() {
    // FUN_0074b0f0
    if ( pSelectedRecord == nullptr ||
         pSelectedRecord->Info.Kind != MapSpotRecord::kKindBookmark ) {
        return;
    }

    const int32_t bookmarkIndex = pSelectedRecord->BookmarkIndex;
    if ( bookmarkIndex < 0 || bookmarkIndex >= numBookmarks ) {
        return;
    }
    const int32_t pictureID = bookmarks[bookmarkIndex].PictureID;

    gpFlashMessageBox->display( 0x37ba8e2, 3.0f, 0, nullptr, nullptr, false );

    int32_t recordIndex = 0;
    while ( recordIndex < numRecords &&
            records[recordIndex].BookmarkIndex != bookmarkIndex ) {
        recordIndex++;
    }
    for ( int32_t i = recordIndex + 1; i < numRecords; i++ ) {
        records[i - 1] = records[i];
    }
    numRecords--;
    for ( int32_t i = 0; i < numRecords; i++ ) {
        if ( records[i].BookmarkIndex > bookmarkIndex ) {
            records[i].BookmarkIndex--;
        }
    }

    int32_t slot = bookmarkIndex + 1;
    for ( ; slot < numBookmarks; slot++ ) {
        bookmarks[slot - 1] = bookmarks[slot];
    }
    Bookmark &lastBookmark = bookmarks[slot - 1];
    lastBookmark.pName[0] = '\0';
    lastBookmark.DynaMapID = 0;
    lastBookmark.PictureID = 0;
    lastBookmark.Position = Eigen::Vector3f::Zero();
    lastBookmark.Orientation = Eigen::Vector3f::Zero();
    numBookmarks--;

    MngBookmark::SaveBookmarkPos( numBookmarks, bookmarks );
    char fileName[512];
    snprintf( fileName, sizeof( fileName ), "%d.jpg", pictureID );
    gpFile->deleteFile( "bookmark", fileName, L"Bookmarks" );
    gpFile->setSaveQueueDelayed( false );

    pSelectedRecord = nullptr;
    updateSelection();
}

void MngSpots::selectSpotRecord( const SpotMap *pSpot ) {
    // FUN_007425c0
    for ( int32_t i = 0; i < numRecords; i++ ) {
        if ( records[i].pSpot == pSpot ) {
            pSelectedRecord = &records[i];
            return;
        }
    }
}

void MngSpots::latchMapRects() {
    // FUN_00745610
    if ( bMapRectsLatched ) {
        return;
    }

    FlashClip mapClip;
    if ( !pMapPlayer->getClipPlayer( "lure_zoed", &mapClip ) &&
         !pMapPlayer->getClipPlayer( "ss_menus/lure_zoed", &mapClip ) ) {
        return;
    }

    FlashClip freeModeClip;
    if ( pMapPlayer->getClipPlayer( "/lure_zoed_free", &freeModeClip ) ) {
        freeModeClip.getPropertyViewport( eFlashProperty::FP_Viewport,
                                          freeModeRect );
    }
    mapClip.getPropertyViewport( eFlashProperty::FP_Viewport, mapRect );
    bMapRectsLatched = true;
}

bool MngSpots::isOnMapPage( const MapSpotRecord *pRecord ) const {
    // FUN_007456b0
    float x = 0.0f;
    float y = 0.0f;
    ProjectToMapMovie( FindGPSCamera(), pRecord->Position, x, y );

    const FlashResourceHeader *pHeader =
        pMapPlayer->getBoundFlashResource()->getHeader();
    const float width = static_cast<float>(
        static_cast<int32_t>( pHeader->FrameSize.XMax ) / 0x14 );
    const float height = static_cast<float>(
        static_cast<int32_t>( pHeader->FrameSize.YMax ) / 0x14 );
    const float u = x / width;
    const float v = y / height;
    return 0.0f < u && u < 1.0f && 0.0f < v && v < 1.0f;
}

void MngSpots::updateSelection() {
    // FUN_0074a880
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    const bool bOnlineDirty = false;

    if ( records.empty() || visibleRecords.empty() ) {
        return;
    }

    const int32_t previousID =
        ( pSelectedRecord != nullptr ) ? pSelectedRecord->ID : kNoRecordID;
    const Eigen::Vector4f previousPosition = ( pSelectedRecord != nullptr )
                                                 ? pSelectedRecord->Position
                                                 : Eigen::Vector4f::Zero();
    pSelectedRecord = nullptr;

    if ( bResortRecords || bOnlineDirty ) {
        std::qsort( records.data(), numRecords, sizeof( MapSpotRecord ),
                    &CompareRecordsByVisibility );
        bResortRecords = false;
    }

    auto computeAnchor = [&]() -> Eigen::Vector4f {
        if ( previousID != kNoRecordID ) {
            return previousPosition;
        }
        if ( bWasFreeMode && pOwner != nullptr ) {
            Eigen::Vector4f cursorPosition;
            ScreenToWorldGround( Eigen::Vector2f( pOwner->getCursorX(),
                                                  1.0f - pOwner->getCursorY() ),
                                 cursorPosition );
            return cursorPosition;
        }
        return focusPosition;
    };

    MapSpotRecord *pNearest = nullptr;
    float nearestDistance = 0.0f;
    numVisibleRecords = 0;
    if ( bIconsEnabled ) {
        for ( int32_t i = 0; i < numRecords; i++ ) {
            MapSpotRecord *pRecord = &records[i];
            if ( isOnMapPage( pRecord ) ) {
                visibleRecords[numVisibleRecords++] = pRecord;
            }

            if ( !IsMapRecordVisible( pRecord, false ) ) {
                continue;
            }

            Eigen::Vector4f corners[4];
            ComputeIconCorners( pRecord->Position, true, corners );
            if ( !isInsideMapRect( corners[0] ) &&
                 !isInsideMapRect( corners[1] ) &&
                 !isInsideMapRect( corners[2] ) &&
                 !isInsideMapRect( corners[3] ) ) {
                continue;
            }

            const Eigen::Vector4f anchor = computeAnchor();
            const float deltaX =
                fabsf( anchor.x() - pRecord->Position.x() ) * 2.0f;
            const float deltaZ =
                fabsf( anchor.z() - pRecord->Position.z() ) * 2.0f;
            if ( previousID == pRecord->ID ) {
                pSelectedRecord = pRecord;
            }

            const float distance = deltaZ * deltaZ + deltaX * deltaX;
            if ( pNearest == nullptr || distance < nearestDistance ) {
                pNearest = pRecord;
                nearestDistance = distance;
            }
        }
    }

    if ( pSelectedRecord == nullptr ) {
        pSelectedRecord = pNearest;
    }

    CameraGPS *pCameraGPS = FindGPSCamera();
    const bool bMapMoving = pCameraGPS->isInTransition() ||
                            ( gpMapView != nullptr && gpMapView->isMoving() );
    if ( bMapMoving ) {
        for ( int32_t i = 0; i < numRecords; i++ ) {
            if ( IsMapRecordVisible( &records[i], false ) &&
                 records[i].ID == previousID ) {
                pSelectedRecord = &records[i];
            }
        }
    }

    numOnlineRecords = 0;
    if ( bIconsEnabled ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        if ( pSelectedRecord == nullptr ) {
            pSelectedRecord = pNearest;
        }
    }

    if ( pSelectedRecord == nullptr || pSelectedRecord->ID != previousID ||
         bOnlineDirty ) {
        refreshSelectionCart();
    }

    if ( pSelectedRecord == nullptr && pOwner != nullptr &&
         gpMapView != nullptr && !gpMapView->isMoving() &&
         gMapActiveFilterMode != 0x10 ) {
        pOwner->enterFreeMode();
    }
    bWasFreeMode = false;
}
