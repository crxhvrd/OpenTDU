#include "shared.h"
#include "scene_interior.h"

#include "tdu_instance.h"

#include "game/gm_scene.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_music_desc.h"
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
static constexpr float kInteriorFovVsLodFactor = 0.5f; // FLOAT_00f41674
// Path Camera Index
static constexpr int32_t kInteriorCameraMode = 7;
static constexpr int32_t kMaxSpotSectorWaitTicks = 600;

SceneInterior::SceneInterior()
    : pRootNode( nullptr ),
      bank(),
      pShopData( nullptr ),
      state( SIS_Idle ),
      bEntered( false ),
      pInteriorBank( nullptr ),
      pSpot( nullptr ),
      pSpotSector( nullptr ),
      bReentry( false ),
      savedFovVsLodFactor( -1.0f ),
      bOverrideFovVsLod( true ),
      bDrawInDOFPass( false ),
      spotSectorWaitTicks( 0 ) {
    // FUN_00774930
    OTDU_UNIMPLEMENTED; // TODO: WORLD 

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

Eigen::Vector4f SceneInterior::getWorldPosition() const {
    // FUN_00774780
    if ( pSpot == nullptr ) {
        return Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    }

    const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
    if ( activeGM != GM_Garage && activeGM != GM_Dressing && !bReentry ) {
        return Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    }

    return Eigen::Vector4f( pSpot->X, pSpot->Y - kUndergroundInteriorDepth,
                            pSpot->Z, 1.0f );
}

bool SceneInterior::waitForSpotSector() {
    if ( gpScenaric == nullptr ) {
        return true;
    }

    SpotMap *pCurrentSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    if ( pCurrentSpot == nullptr ||
         gpScenaric->getSpotList().getSpotSector( pCurrentSpot->Hashcode ) !=
             nullptr ) {
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
         gpActiveGMScene->isStaticFrontEndScene() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (mute the vehicle layer)
    }

    pInteriorBank = pBank;

    if ( !bReentry ) {
        if ( bOverrideFovVsLod && gpRender != nullptr ) {
            savedFovVsLodFactor = gpRender->getFovVsLodFactor();
            if ( gpTestDriveInstance->getActiveGameMode() != GM_Living ) {
                gpRender->setFovVsLodFactor( kInteriorFovVsLodFactor );
            }
        }

        if ( gpWorld != nullptr && gpWorld->isAreaStreamingEnabled() ) {
            gpWorld->setStreamingPositionPtr( nullptr );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: WORLD (drop the origin-shift
                                     // follow target)
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
        OTDU_UNIMPLEMENTED_ONCE; // TODO: WORLD (origin-shift recenter)
    } else {
        pSpot = pCurrentSpot;

        if ( !bReentry ) {
            StreamingManager *pStreaming = gpWorld->getStreamingManager();
            pStreaming->sectorManager.bNeedStreamUpdate = true;
            pStreaming->sectorManager.bDrawSector = false;

            OTDU_UNIMPLEMENTED_ONCE; // TODO: SECTOR

            pStreaming->setActiveRenderLayers( 1 );
            pStreaming->sectorManager.bStreamSector = true;

            OTDU_UNIMPLEMENTED_ONCE; // TODO: WORLD (origin-shift recenter) /
                                     // SECTOR

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
    if ( pSpotSector != nullptr &&
         gpTestDriveInstance->getActiveGameMode() != GM_Dressing ) {
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

    bank.load( pInteriorBank->getBankFile(), 0, 0, pMngActor,
               /*bNoCopy=*/true );
    bank.forceLoad();
    gpScenaric->registerBank( &bank );

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

                if ( gpTestDriveInstance->getActiveGameMode() == GM_Dressing ) {
                    gpMngReflection->setClipEnabled( false );
                }
            }
        }
    }

    pShopData = bank.getFirst( eEntityType::ET_SHDA );
    if ( pShopData != nullptr && gpCamera != nullptr ) {
        CameraPath *pInteriorCamera =
            (CameraPath *)gpCamera->findCameraWithMode(
                kInteriorCameraMode, gpCamera->getBoundCarState() );
        if ( pInteriorCamera != nullptr ) {
            pInteriorCamera->setStaticView(
                interiorPosition,
                interiorPosition + Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ) );

            OTDU_UNIMPLEMENTED_ONCE; // TODO: CAMERA (the reference aspect and
                                     // FOV the ET_SHDA entity carries)

            gpCamera->setCameraMode( kInteriorCameraMode, nullptr, 0.0f );
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
            gpCamera->setCameraMode( 1, nullptr, 0.0f );

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

    pShopData = nullptr;
    bEntered = false;
    state = SIS_Idle;
}
