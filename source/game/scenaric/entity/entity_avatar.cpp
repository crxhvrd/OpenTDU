#include "shared.h"
#include "entity_avatar.h"

#include "core/maths_helper.h"
#include "game/cinematic/cin_res_collection.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/game_mechanism.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_anm_set.h"
#include "game/scenaric/entity/entity_avatar_passenger.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/vehicle/car_state.h"
#include "game/avatar/avatar_layer.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/mng_car_alpha.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/weather/gs_weather.h"
#include "tdu_instance.h"
#include "game/gm_scene.h"
#include "game/intro/gm_intro.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"
#include "render/mng_reflection.h"
#include "render/mng_shadow.h"
#include "render/animation/clip.h"
#include "world/mng_world.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_game.h"

// Binds the playing cinematic interface onto the avatar.
static constexpr FlashMessage kAvatarPlayVoice = FlashMessage( "plvo" );

float gAvatarLODDistances[3] = { 150.0f, 10.0f, 0.0f }; // DAT_010d6f54

EntityAvatar::EntityAvatar()
    : EntityActor(),
      avatarType( 0 ),
      PlayerType( 0 ),
      pLightingAttenuation( gAvatarOutCarLightFactor ),
      currentEnvFactor( 1.0f ),
      currentEnvFactorBackFace( 0.0f ),
      ShadowIntensity( 1.0f ),
      pPooledAvatarEntry( nullptr ),
      FaceParams{},
      pLayers( nullptr ),
      pVoiceEmitter( nullptr ),
      pCineInterface( nullptr ),
      Alpha( 1.0f ),
      RenderLOD( 0 ),
      LodLevel( 0 ),
      LODDistances{ 150.0f, 10.0f, 0.0f },
      AlphaSceneSlot( -1 ),
      CineAvatarMode( 0 ),
      InstanceRefCount( 1 ),
      HeadBoneIndex( 0 ),
      pUseSkeleton( nullptr ),
      avatarSkeleton(),
      HelmetLinkBoneHC( kHeadBoneHashcode ),
      bNeedToRefreshFace( false ),
      bForceShowHelmet( false ),
      bLastShowHead( true ),
      bIsToUpdate( false ),
      bLowResShadow( true ) {
    // FUN_007f8240
    gAvatarLODDistances[0] = LODDistances[0];
    gAvatarLODDistances[1] = LODDistances[1];
    gAvatarLODDistances[2] = LODDistances[2];
}

EntityAvatar::~EntityAvatar() {
    // FUN_00424900
}

bool EntityAvatar::onMessage( FlashMessage *pMessage ) {
    // FUN_007f8130
    if ( *pMessage == kAvatarPlayVoice ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC
        return true;
    }

    return EntityActor::onMessage( pMessage );
}

void EntityAvatar::initializeForCinematic( void *param_2, void *param_3 ) {
    // FUN_007f7b60
    Entity::initializeForCinematic( param_2, param_3 );

    if ( CineAvatarMode != 0 ) {
        return;
    }

    EntityDriver *pDriver = getDriver();
    if ( pDriver != nullptr ) {
        pDriver->leaveSeatForCinematic();
    }

    for ( AvatarLayer *pLayer = pLayers; pLayer != nullptr;
          pLayer = pLayer->getNext() ) {
        pLayer->onResourceChange();
    }
}

void EntityAvatar::initializeForCinematicKeyframed( void *param_2,
                                                    void *param_3 ) {
    // FUN_007f8450
    Entity::initializeForCinematicKeyframed( param_2, param_3 );

    const CinActorState *pState = static_cast<const CinActorState *>( param_2 );
    CinResCollection *pInterface = static_cast<CinResCollection *>( param_3 );
    EntityCinematic *pCinematic = pInterface->getOwner();

    EntityDriver *pDriver = getDriver();
    const CarState *pLocalCar = ( gpScenaric != nullptr )
                                    ? gpScenaric->getLocalPlayer().getCarState()
                                    : nullptr;

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    if ( pDriver == nullptr || pDriver->getCarState() != pLocalCar ||
         !pDriver->isSeating() ) {
        transform = pInterface->buildActorMatrix( *pState );
        propagateTransform( transform, false );
    }

    if ( bMustProjectOnHMap ) {
        ProjectActorOnGround( this, true, true );
    }

    if ( pDriver == nullptr || pDriver->isFreeMovement() ) {
        pInterface->animActor( this, pState );
    } else {
        Animation *pDriverAnimation =
            static_cast<Animation *>( pDriver->getAnimation() );
        if ( pDriverAnimation->isValid() && pCinematic != nullptr ) {
            pDriverAnimation->seekChannel( 0, pCinematic->getTime(), false );
        }
    }

    if ( avatarType != 1 ) {
        return;
    }

    const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                      ? gpWorldStreaming->getPosition()
                                      : Eigen::Vector4f::Zero();

    if ( pCinematic != nullptr && pCinematic->hasFocus() &&
         pInterface->canActorsDriveStreaming() && gpMngWorld != nullptr &&
         gpMngWorld->getDisableStreamingOnCar() &&
         gpMngWorld->isStreamEnabled() && !pInterface->isStreamTarget() &&
         gpWorld != nullptr ) {
        Eigen::Vector4f streamPosition = transform.row( 3 ).transpose();
        streamPosition.head<3>() += shift.head<3>();
        gpWorld->setStreamingPosition( &streamPosition );
    }

    if ( gpMngShadow != nullptr && pCinematic != nullptr &&
         pCinematic->hasFocus() ) {
        if ( pDriver != nullptr && pDriver->isSeating() ) {
            pInterface->setAvatarShadowTarget( false );
            return;
        }

        Eigen::Vector4f shadowTarget = modelMatrix.row( 3 ).transpose();
        if ( bShifted ) {
            shadowTarget.head<3>() += shift.head<3>();
        }
        gpMngShadow->setTarget( &shadowTarget );
        pInterface->setAvatarShadowTarget( true );
    }
}

static void UnloadSelfAsPassenger( EntityAvatar *pAvatar ) {
    EntityAvatarPassenger *pPassenger = pAvatar->getPassenger();
    if ( pPassenger != nullptr && pPassenger->getCarState() != nullptr ) {
        gpScenaric->unloadPassenger( pPassenger->getCarState(), pPassenger );
    }
}

void EntityAvatar::endCinematic( bool bStillReferenced,
                                 EntityCinematic *pLifeCinematic ) {
    // FUN_007e5f70 (inlined)
    pCineInterface = nullptr;

    EntityDriver *pDriver = getDriver();
    if ( avatarType == 1 ) {
        if ( pDriver != nullptr ) {
            pDriver->returnToSeatAfterCinematic();
        }

        if ( bStillReferenced ) {
            flags |= 8;
        } else {
            if ( animation.getNumChannels() > 0 ) {
                animation.resetChannel( 0 );
            }
            CineAvatarMode = 0;
        }
        removeInstanceReference();
    } else {
        if ( pDriver != nullptr && pDriver->getCarState() != nullptr ) {
            pDriver->getCarState()->unloadDriver();
        }
        if ( pLifeCinematic != nullptr ) {
            pLifeCinematic->start( true );
        }
    }

    UnloadSelfAsPassenger( this );

    bMustProjectOnHMap = false;
    if ( animation.getNumChannels() > 0 ) {
        animation.resetChannel( 0 );
    }
}

void EntityAvatar::endLifeSetCinematic() {
    // FUN_007e5f70 (inlined, life-set branch)
    if ( avatarType != 1 ) {
        return;
    }

    EntityDriver *pDriver = getDriver();
    if ( pDriver != nullptr ) {
        pDriver->returnToSeatAfterCinematic();
    }
    removeInstanceReference();
    UnloadSelfAsPassenger( this );
    if ( animation.getNumChannels() > 0 ) {
        animation.resetChannel( 0 );
    }
    CineAvatarMode = 0;
}

bool EntityAvatar::isValid() const {
    // FUN_007f7530
    return pPooledAvatarEntry != nullptr &&
           pPooledAvatarEntry->getLoadingState() == AVT_Ready;
}

void EntityAvatar::initialize() {
    // FUN_007f9070
    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }
}

void EntityAvatar::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f9060
    EntityActor::loadResources( param_2 );
}

void EntityAvatar::unloadResources() {
    // FUN_007f8230
    Entity::unloadResources();
}

void EntityAvatar::tick( PlayerGameMech *pGameMech,
                         float deltaTime,
                         float totalTime ) {
    // FUN_007f90a0
    RenderLOD = chooseLODLevel();

    if ( RenderLOD != 0 ) {
        if ( pPooledAvatarEntry != nullptr ) {
            void *pEntrySkeleton = pPooledAvatarEntry->pSkeleton;
            if ( pEntrySkeleton != nullptr && pUseSkeleton != pEntrySkeleton ) {
                onSkeletonChange( pEntrySkeleton );
            }

            if ( bIsToUpdate != pPooledAvatarEntry->bUpdatable ) {
                onResourceChange();
                bIsToUpdate = pPooledAvatarEntry->bUpdatable;
            }

            for ( AvatarLayer *pLayer = pLayers; pLayer != nullptr;
                  pLayer = pLayer->getNext() ) {
                if ( pLayer->isEnabled() ) {
                    pLayer->tick( deltaTime, totalTime );
                }
            }

            if ( bNeedToRefreshFace ) {
                refreshFace();
            }
        }

        EntityActor::tick( pGameMech, deltaTime, totalTime );

        if ( pPooledAvatarEntry != nullptr ) {
            Matrix4x4f_t *pPalette =
                animation.isValid() ? animation.getPalette() : nullptr;
            if ( pPalette != nullptr ) {
                pPooledAvatarEntry->setBones( pPalette );
            } else if ( gpAvatar != nullptr ) {
                // REMOVEME: use APose for now
                pPooledAvatarEntry->setBones(
                    gpAvatar->getIdentityBonePalette( 0 ) );
            }
        }

        unknownActorFunc();

        if ( animation.isValid() && pPooledAvatarEntry != nullptr ) {
            Matrix4x4f_t headMatrix =
                animation.getNodeWorldMatrix( -1, HeadBoneIndex );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR - FUN_008f1d80 head
                                     // material up/right shader params
            headMatrix = headMatrix * getModelMatrix();
            onHeadUpdate( headMatrix );
        }

        for ( AvatarLayer *pLayer = pLayers; pLayer != nullptr;
              pLayer = pLayer->getNext() ) {
            pLayer->onPoseComputed();
        }

        if ( isShadowCaster() ) {
            pPooledAvatarEntry->addShadowCasters( bLowResShadow,
                                                  ShadowIntensity );
        }
    }

    if ( gpMngCarAlpha != nullptr && pPooledAvatarEntry != nullptr ) {
        updateCarAlpha();
    }
}

bool EntityAvatar::draw( RenderScene *pScene ) {
    // FUN_007f8e20
    if ( pPooledAvatarEntry == nullptr ) {
        return EntityActor::draw( pScene );
    }

    if ( InstanceRefCount < 1 || ( flags & 1 ) == 0 ) {
        return true;
    }

    relightFromWeather( &pPooledAvatarEntry->AvatarLight );

    if ( pLightingAttenuation != nullptr ) {
        AvatarLightAttenuation attenuation;
        attenuation.HeightBase = pLightingAttenuation[ALF_HeightBase];
        attenuation.HeightEnd = pLightingAttenuation[ALF_HeightEnd];
        attenuation.Ambiant =
            1.0f - pLightingAttenuation[ALF_AmbiantAttenuation];
        attenuation.Diffuse =
            1.0f - pLightingAttenuation[ALF_DiffuseAttenuation];
        attenuation.Specular =
            1.0f - pLightingAttenuation[ALF_SpecularAttenuation];
        attenuation.EnvFactor = currentEnvFactor * 0.5f;

        pPooledAvatarEntry->applyLightingAttenuation( attenuation );

        pPooledAvatarEntry->ShaderParamData = Eigen::Vector4f(
            currentEnvFactor, 0.0f, currentEnvFactorBackFace, 0.0f );
    }

    if ( bForceShowHelmet ) {
        AvatarClothe &helmet = pPooledAvatarEntry->Slots[AC_Helmet];
        if ( helmet.pLowRes != nullptr &&
             helmet.pHierarchyNodeLowRes != nullptr && !helmet.bVisible ) {
            helmet.showLowResHierarchy();
        }
        helmet.bVisible = true;
    }

    if ( RenderLOD == 2 ) {
        pPooledAvatarEntry->draw( pScene );

        if ( gpMngReflection != nullptr ) {
            pPooledAvatarEntry->draw( gpMngReflection->getBoundScene() );
        }

        return true;
    }

    if ( RenderLOD == 1 ) {
        pPooledAvatarEntry->drawLowRes( pScene );
    }

    return true;
}

void EntityAvatar::deserialize( BinaryReader *param_2 ) {
    // FUN_007f8fb0
    EntityActor::deserialize( param_2 );
}

void EntityAvatar::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007f8d90
    EntityActor::instanciate( pMngActor, param_1 );

    EntityAvatar *pInstance = static_cast<EntityAvatar *>( param_1 );
    PooledAvatarEntry *pEntry = ( gpAvatar != nullptr )
                                    ? gpAvatar->getPool().allocateInstance()
                                    : nullptr;
    pInstance->pPooledAvatarEntry = pEntry;

    if ( pEntry != nullptr ) {
        pEntry->showAllParts();

        AvatarClothe &helmet = pEntry->Slots[AC_Helmet];
        if ( helmet.pLowRes != nullptr &&
             helmet.pHierarchyNodeLowRes != nullptr && helmet.bVisible ) {
            helmet.hideLowResHierarchy();
        }
        helmet.bVisible = false;
    }

    param_1->setTransform( modelMatrix, true );
}

void EntityAvatar::deinstanciate() {
    // FUN_007f8d20
    onAlphaChange();

    if ( pPooledAvatarEntry != nullptr ) {
        if ( pPooledAvatarEntry->bUsed ) {
            pPooledAvatarEntry->releaseParts();
            pPooledAvatarEntry->bUsed = false;
            pPooledAvatarEntry->pBoneArray = nullptr;
        }

        pPooledAvatarEntry = nullptr;
    }

    if ( pMechanism != nullptr && pMechanism->isCreated() ) {
        pMechanism->removeAndDestroy();
    }
}

static void RegisterAvatarPartCarAlphaMaterials( RenderAvatarBase &part,
                                                 const uint32_t slot,
                                                 const bool bEnabled ) {
    // FUN_00424710
    const uint32_t numMaterials = part.Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        Material *pMaterial = part.Material.getMaterialAtIndex( i );
        if ( pMaterial != nullptr ) {
            gpMngCarAlpha->registerMaterial( slot, pMaterial, bEnabled );
        }
    }
}

static void RegisterAvatarCarAlphaMaterials( PooledAvatarEntry *pEntry,
                                             const uint32_t slot,
                                             const bool bEnabled ) {
    // FUN_004246c0
    for ( AvatarClothe &clothe : pEntry->Slots ) {
        RegisterAvatarPartCarAlphaMaterials( clothe, slot, bEnabled );
    }
    RegisterAvatarPartCarAlphaMaterials( pEntry->Body, slot, bEnabled );
    RegisterAvatarPartCarAlphaMaterials( pEntry->Impostor, slot, bEnabled );
    RegisterAvatarPartCarAlphaMaterials( pEntry->CarBelt, slot, bEnabled );
}

void EntityAvatar::onAlphaChange() {
    // FUN_007f8110
    freeAlphaSlot();
    Alpha = 1.0f;
}

void EntityAvatar::freeAlphaSlot() {
    // FUN_007f80b0
    if ( AlphaSceneSlot == -1 ) {
        return;
    }

    if ( pPooledAvatarEntry != nullptr ) {
        RegisterAvatarCarAlphaMaterials(
            pPooledAvatarEntry, static_cast<uint32_t>( AlphaSceneSlot ),
            false );
    }

    if ( gpMngCarAlpha != nullptr ) {
        gpMngCarAlpha->freeElement( static_cast<uint32_t>( AlphaSceneSlot ) );
    }

    AlphaSceneSlot = -1;
}

void EntityAvatar::installConfiguration( const AvatarConfiguration *pConfig,
                                         const int32_t clotheSet,
                                         const bool bAsync,
                                         const bool bCritical ) {
    // FUN_007f5ff0
    if ( pPooledAvatarEntry == nullptr || gpAvatar == nullptr ) {
        return;
    }

    const int32_t activeClotheSet = pPooledAvatarEntry->ActiveClotheSet;
    if ( activeClotheSet == kNoClotheSet ) {
        gpAvatar->bakeAvatar( this, pConfig, clotheSet, bAsync, bCritical );
    } else if ( activeClotheSet != clotheSet ) {
        gpAvatar->installClothes( this, pConfig, clotheSet, bAsync, bCritical );
    }
}

void EntityAvatar::relight() {
    // FUN_007f83b0
    ClipRow = 0;
    if ( pPooledAvatarEntry == nullptr || gpAvatar == nullptr ) {
        return;
    }

    const int32_t clotheSet = pPooledAvatarEntry->ActiveClotheSet;
    if ( clotheSet != kNoClotheSet ) {
        const AvatarClothePart *pShoes = gpAvatar->findClotheByID(
            pPooledAvatarEntry->pClothesHC[clotheSet][AC_Shoes] );
        if ( pShoes != nullptr && pShoes->Size == ACS_Large ) {
            ClipRow = 2;
            return;
        }
    }

    const AvatarDBEntry *pBody = pPooledAvatarEntry->Body.getAvatarPart();
    ClipRow = ( pBody != nullptr && pBody->Gender == 1 ) ? 1 : 0;
}

void EntityAvatar::unknownActorFunc() {
    // FUN_007f8ca0
    Matrix4x4f_t worldMatrix = modelMatrix;
    if ( !bShifted ) {
        Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                    ? gpWorldStreaming->getPosition()
                                    : Eigen::Vector4f::Zero();
        shift.w() = 0.0f;
        worldMatrix.row( 3 ) -= shift;
    }
    worldMatrix( 3, 3 ) = 1.0f;
    bLightingValid = false;

    if ( pPooledAvatarEntry != nullptr ) {
        pPooledAvatarEntry->setMatrix( worldMatrix );
    }
}

int32_t EntityAvatar::getRenderSceneID() {
    // FUN_007f7570
    eRenderPass renderPass = eRenderPass::RP_Near;

    PostFXStep *pRadialBlur =
        ( gpPostFX != nullptr )
            ? gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
            : nullptr;
    if ( pRadialBlur != nullptr && pRadialBlur->getInput()->isEnabled() ) {
        renderPass = eRenderPass::RP_CarPlayer;
    }

    if ( gpGMIntro != nullptr && gpGMIntro->isCockpitZFarActive() ) {
        renderPass = eRenderPass::RP_Cockpit;
    }

    if ( AlphaSceneSlot != -1 && gpMngCarAlpha != nullptr ) {
        renderPass = gpMngCarAlpha->bindAndGetRenderPassForAlpha(
            renderPass, static_cast<uint32_t>( AlphaSceneSlot ) );
    }

    if ( gpActiveGMScene != nullptr && gpActiveGMScene->isDrawnInFakeDOF() ) {
        return eRenderPass::RP_FakeDOF;
    }

    return renderPass;
}

void EntityAvatar::setHeadVisible( bool bVisible ) {
    // FUN_007f8b60
    if ( pPooledAvatarEntry == nullptr || bLastShowHead == bVisible ) {
        return;
    }

    pPooledAvatarEntry->setMeshVisibility( gHashcodeHead, bVisible );
    pPooledAvatarEntry->setMeshVisibility( gHashcodeEyes, bVisible );
    pPooledAvatarEntry->setMeshVisibility( gHashcodeTeeth, bVisible );

    pPooledAvatarEntry->Slots[AC_Hair].setLowResVisible( bVisible );
    pPooledAvatarEntry->Slots[AC_FacePilosity].setLowResVisible( bVisible );
    pPooledAvatarEntry->Slots[AC_Glasses].setLowResVisible( bVisible );
    pPooledAvatarEntry->Slots[AC_Eyebrows].setLowResVisible( bVisible );

    if ( !bVisible ) {
        pPooledAvatarEntry->Slots[AC_Helmet].setLowResVisible( false );
    }

    bLastShowHead = bVisible;
}

void EntityAvatar::setAvatarType( int32_t type ) {
    // FUN_007f7240
    avatarType = type;
}

EntityDriver *EntityAvatar::getDriver() {
    // FUN_007f7220
    return nullptr;
}

EntityAvatarPassenger *EntityAvatar::getPassenger() {
    // FUN_007f7210
    return nullptr;
}

void EntityAvatar::getCarAlphaScreenRect( Eigen::Vector4f &outRect ) {
    // FUN_007f72a0
    outRect = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f );
}

int32_t EntityAvatar::chooseLODLevel() {
    // FUN_007f7d10
    CameraGame *pCamera =
        gpCamera != nullptr ? gpCamera->getActiveCamera() : nullptr;
    if ( pCamera == nullptr ) {
        return 0;
    }

    const Eigen::Vector3f cameraPosition =
        bShifted ? pCamera->getWorldPositionOffset()
                 : pCamera->getWorldPosition();
    const Eigen::Vector3f delta =
        modelMatrix.row( 3 ).head<3>().transpose() - cameraPosition;

    const float distance = delta.norm() * pCamera->getTanHalfFov();
    for ( int32_t lodLevel = 0; lodLevel < 3; lodLevel++ ) {
        if ( LODDistances[lodLevel] < distance ) {
            return lodLevel;
        }
    }

    return 0;
}

void EntityAvatar::onSkeletonChange( void *pSkeleton ) {
    // FUN_007f7c80
    animation.terminate();

    if ( pPooledAvatarEntry != nullptr &&
         pPooledAvatarEntry->pAnimBuffer != nullptr &&
         avatarSkeleton.wrapBlob( pSkeleton ) ) {
        animation.initialize( &avatarSkeleton, nullptr );
        animation.setNumChannels( kAnimChannelPoolSize );
    }

    for ( AvatarLayer *pLayer = pLayers; pLayer != nullptr;
          pLayer = pLayer->getNext() ) {
        pLayer->onSkeletonChange();
    }

    pUseSkeleton = pSkeleton;

    if ( animation.isValid() ) {
        HelmetLinkBoneHC = kHeadBoneHashcode;
        HeadBoneIndex = avatarSkeleton.findNode( kHeadBoneHashcode );
    }
}

bool gbDisableFaceMorph = false;

void EntityAvatar::refreshFace() {
    // FUN_007f7dd0
    static constexpr uint32_t kFaceMorphMale = 0x14;
    static constexpr uint32_t kFaceMorphFemale = 0x15;
    static constexpr float kFaceParamScale = 0.0625f; // DAT_00f41acc

    const AvatarDBEntry *pBody = pPooledAvatarEntry->Body.getAvatarPart();
    const int32_t gender = ( pBody != nullptr ) ? pBody->Gender : -1;

    RenderAnm *pFaceMorph = nullptr;
    EntityAnmSet *pFaceModifSet = gpAvatar->getFaceModifSet();
    if ( pFaceModifSet != nullptr && gender == 0 ) {
        pFaceMorph = pFaceModifSet->findAnimation( kFaceMorphMale );
    } else if ( pFaceModifSet != nullptr && gender == 1 ) {
        pFaceMorph = pFaceModifSet->findAnimation( kFaceMorphFemale );
    }

    if ( pFaceMorph == nullptr || pFaceMorph->pAnimState == nullptr ||
         !animation.isValid() || gbDisableFaceMorph ) {
        animation.setFacePose( nullptr );
        bNeedToRefreshFace = false;
        return;
    }

    const SkeletonInstance *pSkeleton = animation.getSkeleton();
    facePose.resetFromBindPose( pSkeleton );

    AnimPoseBuffer scratch;
    const int32_t numParams =
        std::min( pFaceMorph->pAnimState->getNumBlendClips(), kNumFaceParams );
    for ( int32_t i = 0; i < numParams; i++ ) {
        scratch.resetFromBindPose( pSkeleton );

        AnimStateBlendEntry entry;
        if ( !pFaceMorph->pAnimState->getBlendEntry( i, entry ) ||
             entry.pClip == nullptr ) {
            continue;
        }

        const Eigen::Vector2f &range = gpAvatar->getFaceRange( gender, i );
        const float param =
            static_cast<float>( static_cast<uint32_t>( FaceParams[i] ) ) *
            kFaceParamScale;
        const float ratio = range.x() + ( range.y() - range.x() ) * param;

        const float duration = entry.pClip->getTotalDuration();
        float time = duration * ratio;
        if ( !( time >= 0.0f && time < duration ) ) {
            time = duration;
        }

        animation.sampleClip( entry.pClip, scratch, time );
        animation.blendPose( scratch, kAnimWeightFull, facePose );
    }

    animation.setFacePose( &facePose );
    bNeedToRefreshFace = false;
}

void EntityAvatar::onResourceChange() {
    // FUN_007f8190
    for ( AvatarLayer *pLayer = pLayers; pLayer != nullptr;
          pLayer = pLayer->getNext() ) {
        pLayer->onResourceChange();
    }
    freeAlphaSlot();
    bNeedToRefreshFace = true;
}

void EntityAvatar::addLayer( AvatarLayer *pLayer ) {
    // FUN_007f7330
    if ( pLayers == nullptr ) {
        pLayers = pLayer;
    } else {
        AvatarLayer *pTail = pLayers;
        while ( pTail->getNext() != nullptr ) {
            pTail = pTail->getNext();
        }
        pTail->setNext( pLayer );
    }
    pLayer->attach( this );
}

void EntityAvatar::removeLayer( AvatarLayer *pLayer ) {
    // FUN_007f72f0
    if ( pLayers == pLayer ) {
        pLayers = pLayer->getNext();
        pLayer->setNext( nullptr );
    } else {
        for ( AvatarLayer *pIt = pLayers; pIt != nullptr;
              pIt = pIt->getNext() ) {
            if ( pIt->getNext() == pLayer ) {
                pIt->setNext( pLayer->getNext() );
                pLayer->setNext( nullptr );
            }
        }
    }
    pLayer->detach();
}

void EntityAvatar::setSkin( AvatarSkinPart *pSkin ) {
    // FUN_007f81e0
    PooledAvatarEntry *pRsc = pPooledAvatarEntry;
    pRsc->pSkinResource = nullptr;
    pRsc->pSkinTint = nullptr;
    pRsc->pSkinResource = pSkin;
    if ( pSkin != nullptr &&
         gpAvatar->getBnkManager().getCommonBodies().isInstallOK() ) {
        pRsc->pSkinTint = pSkin->Tint;
    }
    pRsc->tickBodyMaterials();
}

void EntityAvatar::relightFromWeather( DirectionalLight *pLight ) {
    // FUN_007f8710
    if ( gpWeather->isConfigDirty() ) {
        gpWeather->updateActiveConfig();
    }

    const WeatherConfig &weatherCfg = gpWeather->getActiveWeatherConfig();

    const bool bLitFromGround =
        avatarType == 5 || avatarType == 7 || avatarType == 8 ||
        avatarType == 9 ||
        ( avatarType == 1 && gpScenaric != nullptr &&
          gpScenaric->getAvatarManager().getUnknownBoolean() );

    Eigen::Vector4f groundColor( 1.0f, 1.0f, 1.0f, 1.0f );
    if ( bLitFromGround ) {
        Eigen::Vector4f worldPosition = modelMatrix.row( 3 ).transpose();
        if ( bShifted ) {
            worldPosition += gpWorldStreaming->isEnabled()
                                 ? gpWorldStreaming->getPosition()
                                 : Eigen::Vector4f::Zero();
        }

        if ( !gpWorld->computeGroundColor( &groundColor, &worldPosition,
                                           GCF_Shade ) ) {
            groundColor.head<3>() = Eigen::Vector3f::Constant( 1.0f );
        }
    }

    float luminosity = groundColor.z() * 0.114f + groundColor.y() * 0.587f +
                       groundColor.x() * 0.299f;
    if ( luminosity < weatherCfg.getCarLumMin() ) {
        luminosity = weatherCfg.getCarLumMin();
    }
    if ( weatherCfg.getCarLumMax() < luminosity ) {
        luminosity = weatherCfg.getCarLumMax();
    }

    const float lightModulator = pLightingAttenuation[ALF_LightModulator];

    const Eigen::Vector4f ambient =
        Vector4DLerp( weatherCfg.getSunAmbientColor() * luminosity,
                      weatherCfg.getSunAmbientColor(),
                      weatherCfg.getCarAmbiantFactor() ) *
        lightModulator;
    const Eigen::Vector4f diffuse =
        Vector4DLerp( weatherCfg.getSunDiffuseColor() * luminosity,
                      weatherCfg.getSunDiffuseColor(),
                      weatherCfg.getCarDiffuseFactor() ) *
        lightModulator;
    const Eigen::Vector4f specular =
        Vector4DLerp( weatherCfg.getSunSpecularColor() * luminosity,
                      weatherCfg.getSunSpecularColor(),
                      weatherCfg.getCarSpecularFactor() ) *
        lightModulator;

    pLight->Ambient = ambient;
    pLight->Ambient.w() = 1.0f;
    pLight->Diffuse = diffuse;
    pLight->Diffuse.w() = 1.0f;
    pLight->Specular = specular;
    pLight->Specular.w() = 1.0f;
    pLight->Direction = weatherCfg.getSunDirection();

    currentEnvFactor =
        ( ( luminosity - 1.0f ) * weatherCfg.getCarReflectionFactor() + 1.0f ) *
        weatherCfg.getCarGlobalEnv();
    currentEnvFactorBackFace = 1.0f;
}

void EntityAvatar::onHeadUpdate( Matrix4x4f_t &headTransform ) {
    // FUN_007f8fc0
    if ( pPooledAvatarEntry != nullptr ) {
        pPooledAvatarEntry->Slots[AC_Helmet].setMatrix( headTransform );
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR - voice position (+0x1c3c)
}

void EntityAvatar::updateCarAlpha() {
    // FUN_007f79d0
    if ( Alpha >= 1.0f && AlphaSceneSlot != -1 ) {
        if ( pPooledAvatarEntry != nullptr ) {
            RegisterAvatarCarAlphaMaterials(
                pPooledAvatarEntry, static_cast<uint32_t>( AlphaSceneSlot ),
                false );
        }
        gpMngCarAlpha->freeElement( static_cast<uint32_t>( AlphaSceneSlot ) );
        AlphaSceneSlot = -1;
    }

    if ( Alpha < 1.0f && AlphaSceneSlot == -1 ) {
        AlphaSceneSlot =
            static_cast<int32_t>( gpMngCarAlpha->allocateElement( this ) );
        if ( AlphaSceneSlot != -1 && pPooledAvatarEntry != nullptr ) {
            RegisterAvatarCarAlphaMaterials(
                pPooledAvatarEntry, static_cast<uint32_t>( AlphaSceneSlot ),
                true );
        }
    }

    if ( AlphaSceneSlot == -1 ) {
        return;
    }

    const uint32_t slot = static_cast<uint32_t>( AlphaSceneSlot );
    gpMngCarAlpha->setElementAlpha( slot, Alpha );

    Eigen::Vector4f screenRect = Eigen::Vector4f::Zero();
    getCarAlphaScreenRect( screenRect );
    gpMngCarAlpha->setElementViewport( slot, screenRect );
}

bool EntityAvatar::isShadowCaster() {
    // FUN_007f7120
    return pPooledAvatarEntry != nullptr && InstanceRefCount > 0;
}

void EntityAvatar::setEnvMap( const bool bRealTime, Render2DB *p2DB ) {
    // FUN_007f8080
    if ( pPooledAvatarEntry == nullptr ) {
        return;
    }

    pPooledAvatarEntry->EnvMap.bRealTime = bRealTime;
    pPooledAvatarEntry->EnvMap.p2DB = p2DB;
    pPooledAvatarEntry->setEnvMap();
}
