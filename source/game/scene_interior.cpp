#include "shared.h"
#include "scene_interior.h"

#include "tdu_instance.h"

#include "core/maths_helper.h"

#include "audio/vehicle/gs_vehicle_sound.h"
#include "game/gm_scene.h"
#include "game/scenaric/bank_scene_arrays.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/entity/entity_camera.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_music_desc.h"
#include "game/scenaric/entity/entity_pos_cam.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "game/scenaric/entity/entity_volume.h"
#include "game/scenaric/entity/entity_weather_desc.h"
#include "world/weather/gs_weather.h"
#include "game/scenaric/mng_actor.h"
#include "filesystem/bank.h"
#include "render/gs_render.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/mng_reflection.h"
#include "render/mng_shadow.h"
#include "render/render_scene.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_game.h"
#include "render/camera/camera_path.h"
#include "render/camera/camera_showcase.h"
#include "render/camera/camera_mouse.h"
#include "world/gs_world.h"
#include "world/mng_ocean.h"
#include "world/streaming_manager.h"
#include "config/gs_config.h"

bool gbSceneInteriorLeaving = false; // BOOL_00f7cfc8

#ifdef OTDU_DEVBUILD
bool gbInteriorFreeCameraFallback = true;
#endif

static constexpr const char *kReflectPlaneNode = "D_REFLECT";
static constexpr const char *kOceanLevelNode = "D_OCEAN";

static constexpr float kUndergroundInteriorDepth = 50.0f; // DAT_00f41848
static constexpr float kInteriorFovVsLodFactor = 0.5f;    // FLOAT_00f41674
// Path Camera Index
static constexpr int32_t kInteriorCameraMode = 7;
static constexpr float kInteriorCameraNearPlane = 0.3f;   // FLOAT_00f416bc
static constexpr float kInteriorCameraFarPlane = 5000.0f; // FLOAT_00f41b54
static constexpr int32_t kMaxSpotSectorWaitTicks = 600;

SceneInterior::SceneInterior()
    : pRootNode( nullptr ),
      bank(),
      pShopData( nullptr ),
      pInteriorCamera( nullptr ),
      state( SIS_Idle ),
      bEntered( false ),
      pInteriorBank( nullptr ),
      pSpot( nullptr ),
      pSpotSector( nullptr ),
      bReentry( false ),
      savedFovVsLodFactor( -1.0f ),
      bOverrideFovVsLod( true ),
      bDrawInDOFPass( false ),
      bModeActive( false ),
      placementGameMode( GM_Invalid ),
      spotSectorWaitTicks( 0 ),
      bShowcaseCamera( false ),
      showcaseTarget( Eigen::Vector4f::Zero() ),
      showcaseMaxDistance( 0.0f ),
      showcaseCarBoundingBox( Matrix4x4f_t::Identity() ) {
    // FUN_00774930
    if ( gpCamera != nullptr ) {
        gpCamera->setInteriorView( true );
    }
}

SceneInterior::~SceneInterior() {
    // FUN_00775640
    if ( gpCamera != nullptr ) {
        gpCamera->setInteriorView( false );
    }

    if ( bEntered ) {
        leave();
    }
}

bool SceneInterior::onWorldStreamed( Eigen::Vector4f * /*param_1*/,
                                     Eigen::Vector4f * /*param_2*/,
                                     Eigen::Vector4f *param_3 ) {
    // FUN_007754f0
    SceneryBinary *pScenery = bank.getActiveScenery();
    if ( pScenery != nullptr ) {
        pScenery->shiftObjects( *param_3 );
    }

    return true;
}

eGameMode SceneInterior::getPlacementGameMode() const {
    return ( placementGameMode != GM_Invalid )
               ? placementGameMode
               : gpTestDriveInstance->getActiveGameMode();
}

Eigen::Vector4f SceneInterior::getWorldPosition() const {
    // FUN_00774780
    if ( pSpot == nullptr ) {
        return Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    }

    const eGameMode activeGM = getPlacementGameMode();
    if ( activeGM != GM_Garage && activeGM != GM_Dressing && !bReentry ) {
        return Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    }

    return Eigen::Vector4f( pSpot->X, pSpot->Y - kUndergroundInteriorDepth,
                            pSpot->Z, 1.0f );
}

static inline Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

Matrix4x4f_t SceneInterior::getInteriorTransform() const {
    // FUN_00774810
    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    if ( pSpotSector != nullptr && getPlacementGameMode() != GM_Dressing ) {
        transform = BuildSpotSectorRotation( pSpotSector );
    }

    const Eigen::Vector4f interiorPosition = getWorldPosition();
    transform( 3, 0 ) += interiorPosition.x();
    transform( 3, 1 ) += interiorPosition.y();
    transform( 3, 2 ) += interiorPosition.z();
    transform( 3, 3 ) = 1.0f;
    return transform;
}

Render2DB *SceneInterior::getBankBitmap( const uint64_t hashcode ) {
    // FUN_007745f0
    return bank.getRender2DBByBitmapHashcode( hashcode );
}

static Eigen::Vector4f GetLastPathKeyValue( const void *pKeyArray ) {
    const int32_t numKeys = *reinterpret_cast<const int32_t *>( pKeyArray );
    const float *pValue = reinterpret_cast<const float *>(
        static_cast<const uint8_t *>( pKeyArray ) + 4 + ( numKeys - 1 ) * 0x20 +
        4 );
    return Eigen::Vector4f( pValue[0], pValue[1], pValue[2], pValue[3] );
}

void SceneInterior::focusCamera( const int32_t positionIndex ) {
    // FUN_00774a00
    EntityShopData *pShop = getShopData();
    if ( pShop == nullptr || positionIndex >= pShop->getNumPositions() ) {
        return;
    }

    EntityPosCam *pPosition =
        static_cast<EntityPosCam *>( pShop->getPosition( positionIndex ) );
    if ( pPosition == nullptr ) {
        return;
    }

    EntityCamera *pCameraEntity =
        static_cast<EntityCamera *>( pPosition->getCamera() );
    if ( pCameraEntity == nullptr ) {
        return;
    }

    Matrix4x4f_t cameraMatrix = pCameraEntity->getModelMatrix();
    const Eigen::Vector4f streamOrigin = GetStreamOrigin();
    cameraMatrix( 3, 0 ) -= streamOrigin.x();
    cameraMatrix( 3, 1 ) -= streamOrigin.y();
    cameraMatrix( 3, 2 ) -= streamOrigin.z();
    cameraMatrix( 3, 3 ) = 1.0f;

    const Eigen::Vector4f position = TDUMat4x4MulVec(
        cameraMatrix, Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
    const Eigen::Vector4f target = TDUMat4x4MulVec(
        cameraMatrix, Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f ) );
    const Eigen::Vector4f up = TDUMat4x4MulVec(
        cameraMatrix, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );

    CameraPath *pCamera = (CameraPath *)gpCamera->findCameraWithMode(
        kInteriorCameraMode, gpCamera->getBoundCarState() );
    pCamera->setPositionOffseted( position );
    pCamera->setTargetOffseted( target );
    pCamera->setUp( up );
    pCamera->setFOVAndAspectRatio( pCameraEntity->getRatio(),
                                   pCameraEntity->getFOV() );
    pCamera->updateMatrix( position.head<3>(), up.head<3>(), target.head<3>() );
    pCamera->stopPath();

    gpCamera->setCameraMode( kInteriorCameraMode, 0, 1.0f );
}

void SceneInterior::moveCameraTo( const int32_t positionIndex ) {
    // FUN_00775190
    static constexpr float kMoveDuration = 1.0f; // FLOAT_00f4165c

    EntityShopData *pShop = getShopData();
    if ( pShop == nullptr || positionIndex >= pShop->getNumPositions() ) {
        return;
    }

    EntityPosCam *pPosition =
        static_cast<EntityPosCam *>( pShop->getPosition( positionIndex ) );

    Matrix4x4f_t target = Matrix4x4f_t::Identity();
    Entity *pCameraEntity = pPosition->getCamera();
    if ( pCameraEntity == nullptr ) {
        const Path *pPath = pPosition->getPath();
        if ( pPath == nullptr ) {
            return;
        }

        const Eigen::Vector4f position = GetLastPathKeyValue( pPath->pArray0 );
        const Eigen::Vector4f rotation = GetLastPathKeyValue( pPath->pArray1 );

        const Matrix4x4f_t rotationMatrix =
            BuildRotationMatrix( Eigen::Quaternionf(
                rotation.w(), rotation.x(), rotation.y(), rotation.z() ) );
        target.row( 0 ) = rotationMatrix.row( 0 );
        target.row( 1 ) = -rotationMatrix.row( 2 );
        target.row( 2 ) = rotationMatrix.row( 1 );
        target.row( 3 ) = position.transpose();
    } else {
        target = pCameraEntity->getModelMatrix();
        target.row( 0 ) *= -1.0f;
        target.row( 2 ) *= -1.0f;
    }

    const Eigen::Vector4f shiftOrigin = GetStreamOrigin();
    Matrix4x4f_t offset = Matrix4x4f_t::Identity();
    offset( 3, 0 ) = -shiftOrigin.x();
    offset( 3, 1 ) = -shiftOrigin.y();
    offset( 3, 2 ) = -shiftOrigin.z();
    offset( 3, 3 ) = 1.0f - shiftOrigin.w();

    pInteriorCamera->blendTo( pInteriorCamera->getCamToWorld(), target,
                              kMoveDuration, &offset );
}

void SceneInterior::startCameraPath( Path *pPath,
                                     const bool bInvert,
                                     const bool bStartAtEnd ) {
    // FUN_00775480
    if ( pPath == nullptr || gpCamera == nullptr ) {
        return;
    }

    Matrix4x4f_t offset = getInteriorTransform();
    const Eigen::Vector4f streamOrigin = GetStreamOrigin();
    offset( 3, 0 ) -= streamOrigin.x();
    offset( 3, 1 ) -= streamOrigin.y();
    offset( 3, 2 ) -= streamOrigin.z();
    offset( 3, 3 ) -= streamOrigin.w();

    pInteriorCamera->setPath( pPath, bInvert, -1.0f, &offset, bStartAtEnd );
}

bool SceneInterior::waitForSpotSector() {
    if ( gpScenaric == nullptr ) {
        return true;
    }

    SpotMap *pCurrentSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    if ( pCurrentSpot == nullptr || gpScenaric->getSpotList().getSpotSector(
                                        pCurrentSpot->Hashcode ) != nullptr ) {
        return true;
    }

    if ( ++spotSectorWaitTicks < kMaxSpotSectorWaitTicks ) {
        return false;
    }

    OTDU_LOG_WARN( "Spot 0x%016llx has no sector spot after %d ticks; the "
                   "interior will be built unrotated\n",
                   (unsigned long long)pCurrentSpot->Hashcode,
                   kMaxSpotSectorWaitTicks );
    return true;
}

void SceneInterior::enter( Bank *pBank, const bool bIsReentry ) {
    // FUN_00775d60
    bReentry = bIsReentry;
    gbSceneInteriorLeaving = false;

    if ( bEntered ) {
        return;
    }

    if ( gpActiveGMScene != nullptr &&
         gpActiveGMScene->isStaticFrontEndScene() &&
         gpVehicleSound != nullptr ) {
        gpVehicleSound->setMutedCategoryMask( GSVehicleSound::VSC_Car |
                                              GSVehicleSound::VSC_Bike );
    }

    pInteriorBank = pBank;

    if ( !bReentry ) {
        if ( bOverrideFovVsLod && gpRender != nullptr ) {
            savedFovVsLodFactor = gpRender->getFovVsLodFactor();
            if ( getPlacementGameMode() != GM_Living ) {
                gpRender->setFovVsLodFactor( kInteriorFovVsLodFactor );
            }
        }

        if ( gpWorld != nullptr && gpWorld->isAreaStreamingEnabled() ) {
            gpWorld->setStreamingPositionPtr( nullptr );
            if ( gpWorldStreaming != nullptr &&
                 gpWorldStreaming->isStreamingActive() ) {
                gpWorldStreaming->setStreamCenter( nullptr );
            }
        }
    }

    pSpot = nullptr;
    pSpotSector = nullptr;

    SpotMap *pCurrentSpot = ( gpScenaric != nullptr )
                                ? gpScenaric->getSpotList().getWorldSpotChecked(
                                      gpScenaric->getCurrentSpotIndex() )
                                : nullptr;

    if ( pCurrentSpot == nullptr ) {
        Eigen::Vector4f origin( 0.0f, 0.0f, 0.0f, 1.0f );
        gpWorld->setStreamingPosition( &origin );

        if ( gpWorldStreaming != nullptr &&
             gpWorldStreaming->isStreamingActive() ) {
            gpWorldStreaming->setStreamCenter( nullptr );
            gpWorldStreaming->setStreamCenterPosition( origin );
            gpWorldStreaming->updateStreaming();
        }
    } else {
        pSpot = pCurrentSpot;

        if ( !bReentry ) {
            StreamingManager *pStreaming = gpWorld->getStreamingManager();
            pStreaming->sectorManager.bNeedStreamUpdate = true;
            pStreaming->sectorManager.bDrawSector = false;

            pStreaming->areaGrid.bNeedStreamUpdate = true;
            pStreaming->areaGrid.bDrawArea = false;

            pStreaming->setActiveRenderLayers( 1 );
            pStreaming->sectorManager.bStreamSector = true;

            if ( gpWorldStreaming != nullptr ) {
                gpWorldStreaming->updateStreaming();
            }
            pStreaming->areaGrid.bStreamArea = true;

            Eigen::Vector4f interiorPosition = getWorldPosition();
            gpWorld->setStreamingPosition( &interiorPosition );
        }
    }

    gpWorld->setDirtyStreaming( true );
    state = SIS_Instanciate;

    if ( gpCamera != nullptr ) {
        CameraMouse *pFreeCamera = gpCamera->getMouseCamera();
        if ( pFreeCamera != nullptr ) {
            const Eigen::Vector4f interiorPosition = getWorldPosition();
            const Eigen::Vector4f lookAt =
                interiorPosition + Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );

            pFreeCamera->setPosition( interiorPosition );
            pFreeCamera->setTarget( lookAt );
            pFreeCamera->setUp( Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
            pFreeCamera->updateMatrix( interiorPosition.head<3>(),
                                       Eigen::Vector3f( 0.0f, 1.0f, 0.0f ),
                                       lookAt.head<3>() );
        }
    }

    bEntered = true;
}

void SceneInterior::instanciate() {
    // FUN_007756c0
    if ( pInteriorBank == nullptr || gpScenaric == nullptr ) {
        state = SIS_Streaming;
        return;
    }

    Spots &spots = gpScenaric->getSpotList();
    if ( pSpot != nullptr ) {
        pSpotSector = spots.getSpotSector( pSpot->Hashcode );
    }

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    if ( pSpotSector != nullptr && getPlacementGameMode() != GM_Dressing ) {
        transform = BuildSpotSectorRotation( pSpotSector );
    }

    const Eigen::Vector4f interiorPosition = getWorldPosition();
    transform.row( 3 ) =
        Eigen::Vector4f( interiorPosition.x(), interiorPosition.y(),
                         interiorPosition.z(), 1.0f );
    bank.setBankTransform( transform );

    MngActor *pMngActor = ( gpActiveGMScene != nullptr )
                              ? &gpActiveGMScene->getActorManager()
                              : nullptr;

    gpScenaric->registerBank( &bank );
    bank.load( pInteriorBank->getBankFile(), 0, 0, pMngActor,
               /*bNoCopy=*/true );
    bank.forceLoad();

    bank.instanciateSceneries();

    SceneryBinary *pScenery = bank.getActiveScenery();
    if ( pScenery != nullptr ) {
        pRootNode = pScenery->pHierarchyNode;
    }

    if ( pRootNode != nullptr && gpMngReflection != nullptr ) {
        HiearchyNode *pReflectNode =
            pRootNode->findChild( GetIdentifier64bit( kReflectPlaneNode ) );
        if ( pReflectNode != nullptr ) {
            gpMngReflection->addNode( pRootNode );
        }
    }

    if ( gpMngReflection != nullptr ) {
        RenderScene *pReflectScene = gpMngReflection->getBoundScene();
        if ( pReflectScene != nullptr ) {
            pReflectScene->setEnabled( false );
        }

        if ( pRootNode != nullptr ) {
            Instance *pReflectInstance = pRootNode->findInstance(
                GetIdentifier64bit( kReflectPlaneNode ) );
            if ( pReflectInstance != nullptr ) {
                const Matrix4x4f_t &planeMatrix =
                    pReflectInstance->getModelMatrix();
                const Eigen::Vector3f normal = planeMatrix.row( 1 ).head<3>();

                if ( pReflectScene != nullptr ) {
                    pReflectScene->setEnabled( true );
                }

                gpMngReflection->setPlanarCoords( normal,
                                                  planeMatrix( 3, 1 ) * 2.0f );

                if ( getPlacementGameMode() == GM_Dressing ) {
                    gpMngReflection->setClipEnabled( false );
                }
            }
        }
    }

    pShopData = bank.getFirst( eEntityType::ET_SHDA );
    if ( pShopData != nullptr && gpCamera != nullptr ) {
        pInteriorCamera = (CameraPath *)gpCamera->findCameraWithMode(
            kInteriorCameraMode, gpCamera->getBoundCarState() );
        if ( pInteriorCamera != nullptr ) {
            pInteriorCamera->setStaticView(
                interiorPosition,
                interiorPosition + Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ) );

            if ( !bReentry ) {
                EntityCamera *pShopCamera =
                    static_cast<EntityCamera *>( getShopData()->getCamera() );
                pInteriorCamera->setFOVAndAspectRatio( pShopCamera->getRatio(),
                                                       pShopCamera->getFOV() );

                gpCamera->setNextCamera( pInteriorCamera, 0, 0.0f );

                pInteriorCamera->setNearPlane( kInteriorCameraNearPlane );
                pInteriorCamera->setFarPlane( kInteriorCameraFarPlane );
            }
        }
    }

#ifdef OTDU_DEVBUILD
    // Fallback to freecam if the house interior is malformed/WIP.
    const bool bHasLoopCinematic =
        bank.findEntity( eEntityType::ET_CINE, "C_INT_L" ) != nullptr ||
        bank.findEntity( eEntityType::ET_CINE, "C_INTERIOR_L" ) != nullptr ||
        bank.findEntity( eEntityType::ET_CINE, "C_LIVING" ) != nullptr;

    if ( gbInteriorFreeCameraFallback && pShopData == nullptr &&
         !bHasLoopCinematic && gpCamera != nullptr ) {
        CameraMouse *pFreeCamera = gpCamera->getMouseCamera();
        if ( pFreeCamera != nullptr ) {
            pFreeCamera->setFollowPlayer( false );
            pFreeCamera->initialize();
            gpCamera->setCameraMode( 1, 0, 0.0f );

            OTDU_LOG_WARN( "Interior scene has neither a camera entity nor a "
                           "loop cinematic: falling back to freecam\n" );
        }
    }
#endif

    applyWeatherDescriptor();

    EntityMusicDesc *pMusicDesc =
        (EntityMusicDesc *)bank.getFirst( eEntityType::ET_MUSD );
    if ( pMusicDesc != nullptr && pMusicDesc->getMusicName() != nullptr &&
         gpActiveGMScene != nullptr ) {
        gpActiveGMScene->setMusicDesc( pMusicDesc->getMusicName() );
    }

    if ( pRootNode != nullptr && gpMngOcean != nullptr ) {
        HiearchyNode *pOceanNode =
            pRootNode->findChild( GetIdentifier64bit( kOceanLevelNode ) );
        if ( pOceanNode != nullptr ) {
            const float oceanLevel = pOceanNode->TransformMatrix( 3, 1 );
            if ( oceanLevel < gpMngOcean->getOceanHeight() -
                                  gpMngOcean->getWaveAmplitude() * 0.5f ) {
                gpMngOcean->setOceanHeight( oceanLevel );
            }
        }
    }

    OTDU_LOG_INFO( "Interior scene installed: bank state %d, %d entities, "
                   "root node %p, shop data %p, position %.1f %.1f %.1f\n",
                   (int32_t)bank.getState(), bank.getEntityCount(),
                   (void *)pRootNode, (void *)pShopData, interiorPosition.x(),
                   interiorPosition.y(), interiorPosition.z() );

    state = SIS_Streaming;
}

void SceneInterior::applyWeatherDescriptor() {
    // FUN_00775510
    const ScenaricBank::eBankState bankState = bank.getState();
    if ( bankState == ScenaricBank::BBS_Unused ||
         bankState == ScenaricBank::BBS_LoadFailed ) {
        return;
    }

    EntityWeatherDesc *pWeatherDesc =
        (EntityWeatherDesc *)bank.getFirst( eEntityType::ET_WTHD );
    if ( pWeatherDesc == nullptr ||
         pWeatherDesc->getWeatherName() == nullptr ) {
        return;
    }

    OTDU_LOG_INFO( "Interior weather descriptor: '%s'\n",
                   pWeatherDesc->getWeatherName() );

    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->setWeatherDesc( pWeatherDesc->getWeatherName() );
        return;
    }

    gpWeather->setCurrentWeatherDesc( pWeatherDesc->getWeatherName() );
}

void SceneInterior::tick() {
    // FUN_00775cf0
    if ( state == SIS_Ready ) {
        state = SIS_Live;
    }

    if ( state == SIS_Instanciate && waitForSpotSector() ) {
        instanciate();
    }

    if ( state == SIS_Streaming && bank.isLoaded() ) {
        state = SIS_Ready;
    }

    if ( gpMngShadow != nullptr && pRootNode != nullptr ) {
        gpMngShadow->addShadowReceiver( pRootNode, false );
    }
}

void SceneInterior::draw() {
    if ( pRootNode == nullptr || pRootNode->pInstance == nullptr ||
         gpRender == nullptr ) {
        return;
    }

    const eRenderPass pass =
        bDrawInDOFPass ? eRenderPass::RP_FakeDOF : eRenderPass::RP_Near;

    RenderScene *pScene = gpRender->getRenderPassAtIndex( pass ).pScene;
    if ( pScene != nullptr ) {
        pScene->enqueueDynamicHiearchy( pRootNode );
    }
}

bool BuildInteriorBankPath( eGameMode gameMode, char *pOutPath ) {
    // FUN_007bbf80
    if ( gameMode == GM_Invalid ) {
        gameMode = gpTestDriveInstance->getActiveGameMode();
    }

    if ( gpScenaric == nullptr ) {
        return false;
    }

    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    if ( pSpot == nullptr ) {
        return false;
    }

    char bankName[9];
    memcpy( bankName, &pSpot->SourceHashcode, sizeof( pSpot->SourceHashcode ) );
    bankName[8] = '\0';

    if ( gameMode == GM_Garage ) {
        bankName[0] = 'G';
    } else {
        bankName[0] = ( gameMode == GM_Living ) ? 'L' : 'I';
    }

    sprintf( pOutPath, "%sInterior/%s.bnk", gpConfig->getResRootPath(),
             bankName );
    return true;
}

void SceneInterior::leave() {
    // FUN_00775590
    gbSceneInteriorLeaving = true;

    if ( !bEntered ) {
        return;
    }

    if ( !bReentry && gpRender != nullptr && savedFovVsLodFactor > 0.0f ) {
        gpRender->setFovVsLodFactor( 1.0f );
    }

    OTDU_UNIMPLEMENTED; // TODO: PHYSICS (remove the interior floor plane)

    if ( gpMngReflection != nullptr && pRootNode != nullptr ) {
        gpMngReflection->removeNode( pRootNode );
    }

    const ScenaricBank::eBankState bankState = bank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed ) {
        SceneryBinary *pScenery = bank.getActiveScenery();
        if ( pScenery != nullptr ) {
            pRootNode = pScenery->pHierarchyNode;
            if ( gpMngReflection != nullptr && pRootNode != nullptr ) {
                gpMngReflection->removeNode( pRootNode );
            }
        }

        if ( gpScenaric != nullptr ) {
            gpScenaric->unloadBank( &bank );
        }
    }

    if ( gpVehicleSound != nullptr ) {
        gpVehicleSound->setMutedCategoryMask( 0u );
    }

    pRootNode = nullptr;

    pShopData = nullptr;
    bEntered = false;
    state = SIS_Idle;
}

void SceneInterior::setupShowcaseCamera( const int32_t positionIndex,
                                         PhysicsVehicle *pPhysicsVehicle,
                                         const Matrix4x4f_t &carBoundingBox,
                                         const bool bNoBlend ) {
    // FUN_00774c60
    static constexpr int32_t kShowcaseCameraMode = 6;
    static constexpr float kDefaultMaxDistance = 10.0f;    // FLOAT_00f4166c
    static constexpr float kMaxTargetDrift = 2.0f;         // DAT_00f416b0
    static constexpr float kMinDistanceMargin = 0.18f;     // DAT_00f41ba0
    static constexpr float kShowcaseTransitionTime = 1.0f; // FLOAT_00f4165c
    static constexpr uint32_t kShowcaseCollisionFlags = 0xba;

    bShowcaseCamera = true;

    EntityShopData *pShop = getShopData();
    EntityPosCam *pPosition =
        ( pShop != nullptr && positionIndex < pShop->getNumPositions() )
            ? static_cast<EntityPosCam *>( pShop->getPosition( positionIndex ) )
            : nullptr;

    showcaseCarBoundingBox = carBoundingBox;

    const Eigen::Vector4f carCenter = carBoundingBox.row( 3 ).transpose();
    if ( pPosition == nullptr ) {
        showcaseTarget = carCenter;
        showcaseMaxDistance = kDefaultMaxDistance;
    } else {
        EntityVolume *pVolume =
            static_cast<EntityVolume *>( pPosition->getZoneVolume() );
        const Matrix4x4f_t volumeMatrix =
            ( pVolume != nullptr && pVolume->getVolumeType() == VT_Sphere )
                ? pVolume->getModelMatrix()
                : Matrix4x4f_t::Identity();

        showcaseTarget = volumeMatrix.row( 3 ).transpose();
        const float radiusX = volumeMatrix.row( 0 ).norm();
        const float radiusY = volumeMatrix.row( 1 ).norm();
        const float radiusZ = volumeMatrix.row( 2 ).norm();
        showcaseMaxDistance = Max( Max( radiusX, radiusY ), radiusZ );
    }

    if ( ( showcaseTarget - carCenter ).norm() > kMaxTargetDrift ) {
        showcaseTarget = carCenter;
    }

    const Eigen::Vector4f halfDiagonal =
        Eigen::Vector4f( 0.5f, 0.5f, 0.5f, 0.0f ).transpose() * carBoundingBox;
    const float frameDistance =
        ( showcaseTarget - carCenter ).norm() + halfDiagonal.norm();
    if ( frameDistance > showcaseMaxDistance ) {
        showcaseMaxDistance = frameDistance;
    }

    CameraShowcase *pCamera =
        static_cast<CameraShowcase *>( gpCamera->findCameraWithMode(
            kShowcaseCameraMode, gpCamera->getBoundCarState() ) );
    pCamera->setFOVAndAspectRatio( pInteriorCamera->getAspectRatio(),
                                   pInteriorCamera->getFOV() );
    pCamera->lookAt( &showcaseTarget );
    pCamera->setTargetCar( nullptr, pPhysicsVehicle );
    pCamera->setOffset( Eigen::Vector4f::Zero() );

    const float minDistance = frameDistance + kMinDistanceMargin;
    pCamera->setDistanceRange( Min( minDistance, showcaseMaxDistance ),
                               showcaseMaxDistance );
    pCamera->placeAt( *pInteriorCamera->getWorldPositionRW(), true );
    pCamera->setAllowRoll( false );
    pCamera->setCanMoveCamera( false );
    pCamera->setCollisionFlags( kShowcaseCollisionFlags );

    Instance *pReflection =
        pRootNode->findInstance( GetIdentifier64bit( "D_REFLECT" ) );
    pCamera->setFloorHeight( ( pReflection != nullptr )
                                 ? pReflection->getModelMatrix()( 3, 1 )
                                 : -FLT_MAX );

    gpCamera->setCameraMode( kShowcaseCameraMode,
                             bNoBlend ? CTT_Default : CTT_Lerp,
                             kShowcaseTransitionTime );
}

void SceneInterior::restoreInteriorCamera() {
    // FUN_00774660
    static constexpr int32_t kInteriorCameraMode = 7;

    bShowcaseCamera = false;
    if ( pInteriorCamera != nullptr ) {
        pInteriorCamera->setTime( 1.0f );
    }
    gpCamera->setCameraMode( kInteriorCameraMode, 0, 0.0f );
}
