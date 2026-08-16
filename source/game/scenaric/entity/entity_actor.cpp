#include "shared.h"
#include "entity_actor.h"

#include "gs_timer.h"
#include "core/maths_helper.h"
#include "game/game_mechanism.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "filesystem/binary.h"
#include "render/instance.h"
#include "render/hiearchy_node.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "physics/gs_physics.h"

static constexpr FlashMessage kProjectOnHMapOff =
    FlashMessage( 0x666f7270 ); // prof
static constexpr FlashMessage kProjectOnHMapOn =
    FlashMessage( 0x6e6f7270 ); // pron

Matrix4x4f_t *GetIdentityBonePalette() {
    static constexpr int32_t kScaffoldMaxBones = 256;
    static Matrix4x4f_t sPalette[kScaffoldMaxBones];
    static bool sInitialized = false;
    if ( !sInitialized ) {
        for ( int32_t i = 0; i < kScaffoldMaxBones; i++ ) {
            sPalette[i] = Matrix4x4f_t::Identity();
        }
        sInitialized = true;
    }
    return sPalette;
}

static void BindBones( SerializedSkinnedModel &model, Matrix4x4f_t *pPalette ) {
    if ( model.pInstance != nullptr ) {
        model.pInstance->setBones( pPalette );
    } else {
        for ( HiearchyNode *pNode = model.pHiearchNode; pNode != nullptr;
              pNode = pNode->pNext ) {
            if ( pNode->pInstance != nullptr ) {
                pNode->pInstance->setBones( pPalette );
            }
        }
    }
}

static void SetIdentityBones( SerializedSkinnedModel &model ) {
    BindBones( model, GetIdentityBonePalette() );
}

static void MassInstanceBaseSetMatrix( MassInstanceImpostor &impostor,
                                       Matrix4x4f_t &transformMatrix ) {
    // FUN_00424400
    Instance *pInstance = impostor.pInstance;
    if ( pInstance == nullptr || pInstance->getNumLODs() == 0 ) {
        return;
    }

    pInstance->setModelMatrix( transformMatrix );

    Render3DDHashEntry *pHashcode = pInstance->getLOD( 0 ).pHashcode;
    if ( pHashcode == nullptr ) {
        return;
    }
    uint8_t *pEntry = (uint8_t *)pHashcode->pEntry;
    if ( pEntry == nullptr ) {
        return;
    }

    const RenderObject *pObject = (const RenderObject *)( pEntry + 0x10 );
    impostor.SpherePos = Mat4x4fTransform( pInstance->getModelMatrix(),
                                           pObject->BoundingSphere );
}

EntityActor::EntityActor()
    : Entity(),
      bRefreshAnimationThisFrame( true ),
      DeltaTime( 0.0f ),
      pLightingContext( nullptr ),
      Model(),
      AnimStand(),
      Skeleton(),
      animation(),
      NumMaxLayers( 1 ),
      LightingMode( 0 ),
      bLightingValid( false ),
      bMustProjectOnHMap( false ),
      bMustProjectOnRoad( true ) {
    // FUN_00424380
}

EntityActor::~EntityActor() {
    // FUN_00424120
}

bool EntityActor::onMessage( FlashMessage *pMessage ) {
    // FUN_007fa610
    if ( Entity::onMessage( pMessage ) ) {
        return true;
    }

    switch ( *pMessage ) {
    case kProjectOnHMapOff:
        bMustProjectOnHMap = false;
        return true;
    case kProjectOnHMapOn:
        bMustProjectOnHMap = true;
        return true;
    default:
        break;
    }

    return false;
}

void EntityActor::initializeForCinematicKeyframed( void *param_2,
                                                   void *param_3 ) {
    // FUN_007fa9f0
    Entity::initializeForCinematicKeyframed( param_2, param_3 );

    if ( bMustProjectOnHMap ) {
        OTDU_UNIMPLEMENTED; // TODO: CINEMATIC
    }

    OTDU_UNIMPLEMENTED; // TODO: CINEMATIC
}

void EntityActor::initialize() {
    // FUN_007fad60
    setTransformFromDesc();

    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }

    if ( Skeleton.File != 0 ) {
        animation.initialize( Skeleton.pSkeleton, AnimStand.pClip );

        Matrix4x4f_t *pPal = animation.getPalette();
        if ( pPal != nullptr ) {
            BindBones( Model, pPal );
        } else {
            OTDU_LOG_WARN( "FIXME: Skinned mesh is missing bone palette! "
                           "Expect A-Pos..." );
            SetIdentityBones( Model );
        }
    }

    bLightingValid = false;
    bRefreshAnimationThisFrame = true;
    DeltaTime = 0.0f;
}

void EntityActor::loadResources( ScenaricBank *param_2 ) {
    // FUN_007fac30
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();

    meshSet( param_2 );

    if ( Skeleton.File != 0 ) {
        Skeleton.pSkeleton =
            gpScenaric->resolveSkeleton( param_2, Skeleton.pInstance );
    }

    if ( AnimStand.File != 0 ) {
        AnimStand.pClip = gpScenaric->resolveClip( param_2, AnimStand.pRefBas );
    }
}

bool EntityActor::meshSet( ScenaricBank *pBank ) {
    // FUN_007fa940
    const eEntityType type = getType();
    if ( type == eEntityType::ET_ACTS || type == eEntityType::ET_TRLI ||
         type == eEntityType::ET_AIRC || type == eEntityType::ET_ASLB ) {
        Model.bShared2DB = true;
    }

    Model.loadResources( pBank );
    Model.instanciate( pBank->getCollection3D(), false );
    return true;
}

void EntityActor::initializeInstance( void *param_2, void *param_3 ) {
    // FUN_007fa590
    Entity::initializeInstance( param_2, param_3 );
    flags |= 4;
}

void EntityActor::unloadResources() {
    // FUN_007fa7f0
    Entity::unloadResources();
    Model.uninstanciate();
    animation.terminate();
}

void EntityActor::tick() {
    // FUN_007facb0
    Entity::tick();

    if ( ( flags & 1 ) && ( flags & 4 ) ) {
        Model.updateLOD( pMechanism, pDesc );
    }

    if ( animation.isValid() ) {
        if ( ( flags & 8 ) == 0 ) {
            DeltaTime += ( gpTimer != nullptr ) ? gpTimer->GameDeltaTime
                                                : ( 1.0f / 60.0f );

            if ( bRefreshAnimationThisFrame ) {
                animation.update( DeltaTime );
                DeltaTime = 0.0f;
            }
        } else {
            animation.update( 0.0f );
        }

        Matrix4x4f_t *pPalette = animation.getPalette();
        if ( pPalette != nullptr ) {
            BindBones( Model, pPalette );
        }
    }
}

bool EntityActor::draw( RenderScene *pScene ) {
    // FUN_007fabf0
    if ( ( flags & 1 ) && ( flags & 4 ) ) {
        if ( !bLightingValid ) {
            updateSkinningMatrices();
        }

        Model.draw( pScene );
        return true;
    }
    return false;
}

void EntityActor::deserialize( BinaryReader *param_2 ) {
    // FUN_007fa990
    Entity::deserialize( param_2 );

    AnimStand.deserialize( param_2 );
    Model.deserialize( param_2 );

    Skeleton.File = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );
    if ( Skeleton.File != 0 ) {
        Skeleton.pInstance = param_2->pCursor;
        param_2->pCursor += Skeleton.File * 8;
    }

    if ( gpScenaric->getVersion() < 0x18 ) {
        param_2->pCursor += 8;
    }
}

void EntityActor::setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) {
    // FUN_007fa410
    Entity::setTransform( transformMatrix, param_1 );
    unknownActorFunc();
}

void EntityActor::setPosition( Eigen::Vector4f &position, bool param_1 ) {
    // FUN_007fa550
    Entity::setPosition( position, param_1 );
    unknownActorFunc();
}

void EntityActor::propagateTransform( Matrix4x4f_t &transformMatrix,
                                      bool param_1 ) {
    // FUN_007fa790
    Entity::propagateTransform( transformMatrix, param_1 );
    unknownActorFunc();
}

void EntityActor::propagatePosition( Eigen::Vector4f &position, bool param_1 ) {
    // FUN_007fa7c0
    Entity::propagatePosition( position, param_1 );
    unknownActorFunc();
}

void EntityActor::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007fa850
    Entity::instanciate( pMngActor, param_1 );

    EntityActor *pTarget = static_cast<EntityActor *>( param_1 );

    if ( Model.pInstance != nullptr || Model.pHiearchNode != nullptr ) {
        pTarget->Model = Model;
        pTarget->Model.pInstance = nullptr;
        pTarget->Model.p3DFiles = nullptr;
        pTarget->Model.pHiearchNode = nullptr;
        pTarget->Model.bShared2DB = Model.bShared2DB;
        pTarget->Model.MaxLODDist = Model.MaxLODDist;
        pTarget->Model.instanciate( Model.p3DFiles, true );
    }

    pTarget->Skeleton.pInstance = Skeleton.pInstance;
    pTarget->Skeleton.File = Skeleton.File;
    pTarget->AnimStand.ID = AnimStand.ID;
    pTarget->AnimStand.File = AnimStand.File;
    pTarget->AnimStand.pRefBas = AnimStand.pRefBas;
}

bool EntityActor::refreshModelFromTemplate() {
    // WA/Hack for late PMI prop streaming (see
    // https://github.com/opentestdriveunlimited/OpenTestDriveUnlimited/issues/256).
    if ( Model.pInstance != nullptr || Model.pHiearchNode != nullptr ) {
        return false; // already has a model
    }
    EntityActor *pTemplate = static_cast<EntityActor *>( getParent() );
    if ( pTemplate == nullptr ||
         ( pTemplate->Model.pInstance == nullptr &&
           pTemplate->Model.pHiearchNode == nullptr ) ) {
        return false; // template still has no model to clone from
    }

    Model = pTemplate->Model;
    Model.pInstance = nullptr;
    Model.p3DFiles = nullptr;
    Model.pHiearchNode = nullptr;
    Model.bShared2DB = pTemplate->Model.bShared2DB;
    Model.MaxLODDist = pTemplate->Model.MaxLODDist;
    Model.instanciate( pTemplate->Model.p3DFiles, true );

    if ( Model.pInstance == nullptr && Model.pHiearchNode == nullptr ) {
        return false; // still failed (e.g. geometry genuinely absent)
    }

    unknownActorFunc();
    return true;
}

void EntityActor::deinstanciate() {
    // FUN_007fa830
    if ( pMechanism != nullptr && pMechanism->isCreated() ) {
        pMechanism->removeAndDestroy();
    }
}

void EntityActor::onWorldStreamed() {
    // FUN_007fa480
    if ( bShifted && Model.pInstance != nullptr && pMechanism != nullptr &&
         pMechanism->isStaticController() ) {
        const bool bEnabled = gpWorldStreaming->isEnabled();
        const Eigen::Vector4f pos = bEnabled ? gpWorldStreaming->getPosition()
                                             : Eigen::Vector4f::Zero();
        const Eigen::Vector4f lastPos =
            bEnabled ? gpWorldStreaming->getLastPosition()
                     : Eigen::Vector4f::Zero();

        Matrix4x4f_t m = modelMatrix;
        m( 3, 0 ) += lastPos.x() - pos.x();
        m( 3, 1 ) += lastPos.y() - pos.y();
        m( 3, 2 ) += lastPos.z() - pos.z();
        propagateTransform( m, false );
        return;
    }
    unknownActorFunc();
}

void *EntityActor::getGeometryData() {
    // FUN_00415c90
    return &Model;
}

void EntityActor::setColor( ColorRGBA color ) {
    // FUN_007fa3a0
    bLightingValid = true;

    if ( Model.pInstance != nullptr ) {
        Model.pInstance->getFlagsWrite() |= 0x30;
        Model.pInstance->setColor( color );
    } else {
        for ( HiearchyNode *pNode = Model.pHiearchNode; pNode != nullptr;
              pNode = pNode->pNext ) {
            pNode->pInstance->getFlagsWrite() |= 0x30;
            pNode->pInstance->setColor( color );
        }
    }

    if ( Model.MIInpostor.pInstance != nullptr ) {
        Model.MIInpostor.pInstance->getFlagsWrite() |= 0x30;
        Model.MIInpostor.pInstance->setColor( color );
    }
}

void EntityActor::onGeometrySubmitted() {
    // FUN_00423f10
    bLightingValid = true;
}

bool EntityActor::isPMIInTile( const SectorHeightmap *pHeightmap,
                               int32_t tileKey ) {
    // FUN_007fa440
    if ( pMechanism == nullptr || !pMechanism->isCreated() ) {
        return Entity::isPMIInTile( pHeightmap, tileKey );
    }
    return false;
}

void *EntityActor::getAnimation() {
    // FUN_00423f00
    return &animation;
}

void EntityActor::relight() {
    // FUN_007fa370
    EntityAvatar *pDriverAvatar =
        ( gpScenaric != nullptr )
            ? gpScenaric->getAvatarManager().getDriverAvatar()
            : nullptr;
    if ( pDriverAvatar != nullptr ) {
        pDriverAvatar->relight();
        LightingMode = pDriverAvatar->LightingMode;
    }
}

void EntityActor::updateSkinningMatrices() {
    // FUN_007fa680
    if ( gpWorld == nullptr ) {
        return;
    }

    const Eigen::Vector4f entityPosition = modelMatrix.row( 3 ).transpose();

    ColorRGBA groundColor;
    if ( !gpWorld->computeGroundColorPacked( &groundColor, &entityPosition,
                                             GCF_Shade ) ) {
        return;
    }

    setColor( groundColor );

    if ( Model.pHiearchNode == nullptr ) {
        return;
    }

    if ( Model.pSimplifiedMeshChunk != nullptr && pMechanism != nullptr &&
         pMechanism->hasPhysicsController() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS
        return;
    }

    for ( HiearchyNode *pNode = Model.pHiearchNode; pNode != nullptr;
          pNode = pNode->pNext ) {
        if ( pNode->pInstance == nullptr ) {
            continue;
        }

        const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                          ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();
        const Eigen::Vector4f nodePosition =
            pNode->pInstance->getModelMatrix().row( 3 ).transpose() + shift;

        ColorRGBA nodeColor;
        if ( gpWorld->computeGroundColorPacked( &nodeColor, &nodePosition,
                                                GCF_Shade ) ) {
            pNode->pInstance->getFlagsWrite() |= 0x30;
            pNode->pInstance->setColor( nodeColor );
        }
    }
}

void EntityActor::unknownActorFunc() {
    // FUN_007faa40
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

    if ( Model.pInstance == nullptr ) {
        if ( Model.pHiearchNode != nullptr ) {
            Model.pHiearchNode->TransformMatrix = worldMatrix;
            Model.pHiearchNode->updateAndPropagateTransformMatrices( false );
        }
    } else {
        Model.pInstance->setModelMatrix( worldMatrix );
    }

    MassInstanceBaseSetMatrix( Model.MIInpostor, worldMatrix );
}

int32_t EntityActor::refreshMechanismRender(
    const std::vector<std::pair<uint64_t, void *>> &members ) {
    // FUN_004560c0
    if ( Model.pHiearchNode == nullptr || gpPhysics == nullptr ) {
        return 0;
    }

    Eigen::Vector4f shift = Eigen::Vector4f::Zero();
    if ( !bShifted && gpWorldStreaming->isEnabled() ) {
        shift = gpWorldStreaming->getPosition();
    }
    shift.w() = 0.0f;

    int32_t driven = 0;
    for ( const auto &member : members ) {
        HiearchyNode *pNode = Model.pHiearchNode->findChild( member.first );
        if ( pNode == nullptr || pNode->pInstance == nullptr ) {
            continue;
        }
        Matrix4x4f_t pose;
        bool bActive = false;
        if ( !gpPhysics->getBodyPose( static_cast<RigidBody_t>( member.second ),
                                      pose, bActive ) ) {
            continue;
        }
        if ( !bShifted ) {
            pose.row( 3 ) -= shift;
        }
        pose( 3, 3 ) = 1.0f;
        pNode->TransformMatrix = pose;
        pNode->Flags |= 1; // absolute: don't compose with the parent
        driven++;
    }

    if ( driven == 0 ) {
        return 0; // nothing to overlay
    }

    if ( Model.pHiearchNode->pInstance != nullptr ) {
        Model.pHiearchNode->pInstance->setModelMatrix(
            Model.pHiearchNode->TransformMatrix );
    }
    Model.pHiearchNode->propagateTransformMatricesWithFlag();
    bLightingValid = false;
    return driven;
}
