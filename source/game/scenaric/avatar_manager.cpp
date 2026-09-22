#include "shared.h"
#include "avatar_manager.h"

#include "game/avatar/gs_avatar.h"
#include "player_data/gs_playerdata.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_factory.h"
#include "render/gs_render.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_game.h"

static constexpr float kFreeSlotSortKey = 3.0e38f;        // DAT_00f41bd4
static constexpr float kNonPlayerAvatarSortKey = FLT_MAX; // DAT_00f4171c

struct AvatarRank {
    int32_t SlotIndex;
    float SortKey;
};

AvatarManager::AvatarManager()
    : avatarBank(),
      currentFrameIndex( 0 ),
      bIsFullUpdate( false ),
      bUnknownBoolean( false ) {
    // FUN_007f5e10
    animFrameIndex[1] = 2;
    animFrameIndex[2] = 2;
    animFrameIndex[4] = 6;
    animFrameIndex[5] = 6;
    animFrameIndex[6] = 6;
    animFrameIndex[7] = 6;
    animFrameStart[3] = 2;
    animFrameIndex[0] = 1;
    animFrameIndex[3] = 3;
    animFrameIndex[8] = 1;
    animFrameStart[0] = 0;
    animFrameStart[1] = 0;
    animFrameStart[2] = 1;
    animFrameStart[4] = 0;
    animFrameStart[5] = 1;
    animFrameStart[6] = 3;
    animFrameStart[7] = 4;
    animFrameStart[8] = 0;
}

AvatarManager::~AvatarManager() {}

void AvatarManager::initialize() {
    // FUN_007f6440
    GSScenaric::LoadScenaricBank( "scenarics/Avatars", "avatarsdriver",
                                  &avatarBank );
}

void AvatarManager::tick( const float deltaTime, const float totalTime ) {
    // FUN_007f5a30
    currentFrameIndex++;

    if ( !bIsFullUpdate ) {
        CameraGame *pCamera =
            gpCamera != nullptr ? gpCamera->getDefaultCamera() : nullptr;

        std::array<AvatarRank, kMaxAvatars> ranks;
        for ( int32_t i = 0; i < kMaxAvatars; i++ ) {
            const PlayerAvatar &slot = playerAvatars[i];
            ranks[i].SlotIndex = i;

            if ( slot.ReferenceCount < 1 || slot.pAvatar == nullptr ) {
                ranks[i].SortKey = kFreeSlotSortKey;
                continue;
            }

            if ( slot.pAvatar->getType() != eEntityType::ET_AVPL ||
                 pCamera == nullptr ) {
                ranks[i].SortKey = kNonPlayerAvatarSortKey;
                continue;
            }

            const Eigen::Vector3f cameraPosition =
                slot.pAvatar->isShifted() ? pCamera->getWorldPositionOffset()
                                          : pCamera->getWorldPosition();
            const Eigen::Vector3f delta =
                cameraPosition -
                slot.pAvatar->getModelMatrix().row( 3 ).head<3>().transpose();
            ranks[i].SortKey = delta.squaredNorm();
        }

        std::sort( ranks.begin(), ranks.end(),
                   []( const AvatarRank &lhs, const AvatarRank &rhs ) {
                       return lhs.SortKey < rhs.SortKey;
                   } );

        for ( int32_t rank = 0; rank < kMaxAvatars; rank++ ) {
            EntityAvatar *pAvatar =
                playerAvatars[ranks[rank].SlotIndex].pAvatar;
            if ( pAvatar == nullptr ) {
                continue;
            }

            pAvatar->setRefreshAnimation(
                ( ( animFrameStart[rank] + currentFrameIndex ) %
                  animFrameIndex[rank] ) == 0 );
        }
    } else {
        for ( PlayerAvatar &slot : playerAvatars ) {
            if ( slot.ReferenceCount > 0 && slot.pAvatar != nullptr ) {
                slot.pAvatar->setRefreshAnimation( true );
            }
        }
    }

    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.ReferenceCount > 0 && slot.pAvatar != nullptr ) {
            slot.pAvatar->tick( nullptr, deltaTime, totalTime );
        }
    }
}

void AvatarManager::draw() {
    // FUN_007f59c0
    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.ReferenceCount < 1 || slot.pAvatar == nullptr ) {
            continue;
        }

        const eRenderPass renderPass =
            static_cast<eRenderPass>( slot.pAvatar->getRenderSceneID() );
        slot.pAvatar->draw(
            gpRender->getRenderPassAtIndex( renderPass ).pScene );
    }
}

EntityAvatar *AvatarManager::allocateAvatar( const uint64_t hashcode,
                                             const int32_t type ) {
    // FUN_007f6150

    // Drivers (type 1) share a single loaded avatar regardless of hashcode:
    // reuse the first loaded driver slot and rebind it to this hashcode.
    if ( type == 1 ) {
        for ( PlayerAvatar &slot : playerAvatars ) {
            if ( slot.ReferenceCount != 0 && slot.pAvatar != nullptr &&
                 slot.pAvatar->getAvatarType() == 1 ) {
                slot.ReferenceCount++;
                slot.Hashcode = hashcode;
                return slot.pAvatar;
            }
        }
    }

    // Reuse an already-loaded avatar matching both hashcode and type.
    if ( hashcode != ~0ull ) {
        for ( PlayerAvatar &slot : playerAvatars ) {
            if ( slot.ReferenceCount != 0 && slot.Hashcode == hashcode &&
                 slot.pAvatar != nullptr &&
                 slot.pAvatar->getAvatarType() == type ) {
                slot.ReferenceCount++;
                return slot.pAvatar;
            }
        }
    }

    // Otherwise build a fresh avatar into the first free slot.
    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.pAvatar == nullptr ) {
            slot.pAvatar = createAvatarInstance( type );
            slot.Hashcode = hashcode;
            slot.ReferenceCount = 1;
            return slot.pAvatar;
        }
    }

    return nullptr;
}

EntityAvatar *AvatarManager::createAvatar( const AvatarConfiguration *pConfig,
                                           const int32_t clotheSet,
                                           const bool bAsync,
                                           const uint64_t hashcode,
                                           const bool bCritical,
                                           const int32_t type ) {
    // FUN_007f6260
    EntityAvatar *pAvatar = allocateAvatar( hashcode, type );
    if ( pAvatar == nullptr ) {
        return nullptr;
    }

    const AvatarConfiguration *pEffectiveConfig = pConfig;
    if ( type == 1 ) {
        pEffectiveConfig = TestDrive::GetSavedAvatarConfiguration();
    }

    pAvatar->installConfiguration( pEffectiveConfig, clotheSet, bAsync,
                                   bCritical );
    return pAvatar;
}

void AvatarManager::releaseAvatar( EntityAvatar *pAvatar ) {
    // FUN_007f6090
    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.pAvatar == pAvatar ) {
            if ( --slot.ReferenceCount == 0 ) {
                destroyAvatarInstance( slot.pAvatar );
                slot.pAvatar = nullptr;
            }
            return;
        }
    }
}

int32_t AvatarManager::getReferenceCount( const EntityAvatar *pAvatar ) const {
    // FUN_007f5780
    for ( const PlayerAvatar &slot : playerAvatars ) {
        if ( slot.pAvatar == pAvatar ) {
            return static_cast<int32_t>( slot.ReferenceCount );
        }
    }
    return 0;
}

EntityAvatar *AvatarManager::getDriverAvatar() {
    // FUN_007f5940
    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.ReferenceCount != 0 && slot.pAvatar != nullptr &&
             slot.pAvatar->getAvatarType() == 1 ) {
            return slot.pAvatar;
        }
    }
    return nullptr;
}

void AvatarManager::releaseAllAvatars() {
    // FUN_007f63b0
    releaseDriverAvatar();

    for ( PlayerAvatar &slot : playerAvatars ) {
        while ( slot.ReferenceCount != 0 ) {
            if ( --slot.ReferenceCount == 0 ) {
                destroyAvatarInstance( slot.pAvatar );
                slot.pAvatar = nullptr;
            }
        }
    }
}

void AvatarManager::releaseDriverAvatar() {
    // FUN_007f6350
    EntityAvatar *pDriver = nullptr;
    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.pAvatar != nullptr && slot.pAvatar->getAvatarType() == 1 ) {
            pDriver = slot.pAvatar;
            break;
        }
    }
    if ( pDriver == nullptr ) {
        return;
    }

    for ( PlayerAvatar &slot : playerAvatars ) {
        if ( slot.pAvatar == pDriver ) {
            if ( --slot.ReferenceCount == 0 ) {
                destroyAvatarInstance( slot.pAvatar );
                slot.pAvatar = nullptr;
            }
            return;
        }
    }
}

EntityAvatar *AvatarManager::createAvatarInstance( int32_t type ) {
    // FUN_007f60d0
    eEntityType classID;
    switch ( type ) {
    case 5:
        classID = eEntityType::ET_AVSG;
        break;
    case 7:
        classID = eEntityType::ET_AVHI;
        break;
    case 8:
        classID = eEntityType::ET_AVCO;
        break;
    case 9:
        classID = eEntityType::ET_AVTR;
        break;
    default:
        classID = eEntityType::ET_AVPL;
        break;
    }

    Entity *pTemplate = avatarBank.getFirst( classID );
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    EntityAllocation alloc = EntityFactory::DoAction(
        nullptr, classID, nullptr, eInstantiationFlag::IF_CreateEntity );
    EntityAvatar *pInstance = static_cast<EntityAvatar *>( alloc.pEntity );
    if ( pInstance == nullptr ) {
        return nullptr;
    }

    pTemplate->instanciate( nullptr, pInstance );
    pInstance->initializeInstance( nullptr, pTemplate );
    pInstance->initialize();
    pInstance->setAvatarType( type );
    return pInstance;
}

void AvatarManager::destroyAvatarInstance( EntityAvatar *pAvatar ) {
    // FUN_007f6040
    if ( pAvatar == nullptr ) {
        return;
    }
    pAvatar->deinstanciate();
    delete pAvatar;
}
