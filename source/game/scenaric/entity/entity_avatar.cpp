#include "shared.h"
#include "entity_avatar.h"

#include "core/maths_helper.h"
#include "game/game_mechanism.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/mng_car_alpha.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/weather/gs_weather.h"
#include "render/gs_render.h"
#include "render/mng_reflection.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_game.h"

// Binds the playing cinematic interface onto the avatar.
static constexpr FlashMessage kAvatarOverlap = FlashMessage( 0x706c766f );

float gAvatarLODDistances[3] = { 150.0f, 10.0f, 0.0f }; // DAT_010d6f54

EntityAvatar::EntityAvatar()
    : EntityActor(),
      avatarType( 0 ),
      PlayerType( 0 ),
      pLightingAttenuation( gAvatarOutCarLightFactor ),
      currentEnvFactor( 1.0f ),
      currentEnvFactorBackFace( 0.0f ),
      pSerializedData( nullptr ),
      pPooledAvatarEntry( nullptr ),
      FaceParams{},
      pBehavior( nullptr ),
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
      HelmetLinkBoneHC( kHeadBoneHashcode ),
      bNeedToRefreshFace( false ),
      bForceShowHelmet( false ),
      bLastShowHead( true ),
      bIsToUpdate( false ),
      bLowResShadow( false ) {
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
    if ( *pMessage == kAvatarOverlap ) {
        OTDU_UNIMPLEMENTED; // TODO: CINEMATIC
        return true;
    }

    return EntityActor::onMessage( pMessage );
}

void EntityAvatar::initializeForCinematic( void *param_2, void *param_3 ) {
    // FUN_007f7b60
    OTDU_UNIMPLEMENTED; // TODO: CINEMATIC
}

void EntityAvatar::initializeForCinematicKeyframed( void *param_2,
                                                    void *param_3 ) {
    // FUN_007f8450
    Entity::initializeForCinematicKeyframed( param_2, param_3 );
    OTDU_UNIMPLEMENTED; // TODO: CINEMATIC — apply keyframed avatar transform
}

bool EntityAvatar::isValid() const {
    // FUN_007f7530
    if ( pPooledAvatarEntry != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: AVATAR
    }
    return true;
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

void EntityAvatar::tick() {
    // FUN_007f90a0
    RenderLOD = chooseLODLevel();

    if ( RenderLOD != 0 ) {
        if ( pPooledAvatarEntry != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR — skeleton/resource/
                                     // attachment sync
        }

        EntityActor::tick();

        if ( pPooledAvatarEntry != nullptr && gpAvatar != nullptr ) {
            pPooledAvatarEntry->setBones(
                gpAvatar->getIdentityBonePalette( 0 ) );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR — edAnm bone palette
        }

        unknownActorFunc();

        if ( pPooledAvatarEntry != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR — head update + shadow
                                     // caster
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

    if ( bLowResShadow ) {
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
    onCarAlphaTerm();

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

void EntityAvatar::onCarAlphaTerm() {
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
        OTDU_UNIMPLEMENTED_ONCE; // TODO: CARALPHA - FUN_004246c0, the per-part
                                 // alpha material reset
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
    LightingMode = 0;
    OTDU_UNIMPLEMENTED; // TODO: AVATAR
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

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR

    if ( AlphaSceneSlot != -1 && gpMngCarAlpha != nullptr ) {
        renderPass = gpMngCarAlpha->bindAndGetRenderPassForAlpha(
            renderPass, static_cast<uint32_t>( AlphaSceneSlot ) );
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

int32_t EntityAvatar::unknownAvatarFunc2() {
    // FUN_007f7210
    return 0;
}

void EntityAvatar::getLightingFactor( Eigen::Vector4f &outFactor ) {
    // FUN_007f72a0
    outFactor = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f );
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
    HelmetLinkBoneHC = kHeadBoneHashcode;
    HeadBoneIndex = 0;
    OTDU_UNIMPLEMENTED; // TODO: AVATAR
}

void EntityAvatar::onResourceChange() {
    // FUN_007f8190
    OTDU_UNIMPLEMENTED; // TODO: AVATAR
    bNeedToRefreshFace = true;
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
    OTDU_UNIMPLEMENTED; // TODO: AVATAR
}

void EntityAvatar::updateCarAlpha() {
    // FUN_007f79d0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
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
