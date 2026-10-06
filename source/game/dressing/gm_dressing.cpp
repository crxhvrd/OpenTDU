#include "shared.h"
#include "gm_dressing.h"

#include "tdu_instance.h"

#include "core/maths_helper.h"
#include "database/gs_database.h"
#include "flash/implementation/flash_player.h"
#include "game/avatar/avatar_clothes.h"
#include "game/avatar/avatar_part.h"
#include "game/avatar/gs_avatar.h"
#include "game/mng_music.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_anm_set.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_camera.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_avatar.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/render_scene.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

static constexpr int32_t kDressingCameraMode = 0;
static constexpr float kDressingNearPlane = 0.8f;         // DAT_00f416d0
static constexpr float kUndergroundInteriorDepth = 50.0f; // DAT_00f41848
static constexpr float kSliderMaxValue = 15.0f;           // DAT_00f41700
static constexpr float kFaceSliderVelocity = 8.0f;        // DAT_00f41ac4
static constexpr float kMaxSkinIndex = 19.0f;             // DAT_00f41ed0
static constexpr int32_t kFemaleSkinIDOffset = 0x14;
static constexpr uint32_t kEyeNameDB = 0xb;
static constexpr uint64_t kDefaultEyeNameHashcode = 0x8ad45e;

static constexpr const char *kFaceSliderVariables[kNumFaceParams] = {
    // DAT_00f91d80
    "/:AV_Eyes_h",     "/:AV_Eyes_d",    "/:AV_Eyes_w",   "/:AV_Eb_h",
    "/:AV_Eb_d",       "/:AV_Ears_s",    "/:AV_Ears_h",   "/:AV_Ears_t",
    "/:AV_Nose_l",     "/:AV_Nose_w",    "/:AV_Nose_t",   "/:AV_Nose_d",
    "/:AV_Nose_h",     "/:AV_Cb_d",      "/:AV_Cb_w",     "/:AV_Cb_h",
    "/:AV_Cheek_w",    "/:AV_Chin_d",    "/:AV_Chin_w",   "/:AV_Chin_h",
    "/:AV_Mouth_t",    "/:AV_Mouth_w",   "/:AV_Mouth_h",  "/:AV_Mouth_d",
    "/:AV_Jaw_d",      "/:AV_Jaw_w",     "/:AV_Jaw_h",    "/:AV_Forehead_d",
    "/:AV_Forehead_w", "/:AV_Cranium_h", "/:AV_Cranium_w" };

// Face message -> face slider index (FUN_007a14b0 callers).
static constexpr std::pair<FlashMessage, int32_t> kFaceSliderMessages[] = {
    {FlashMessage( "eyhe" ), 0 },
    {FlashMessage( "eyde" ), 1 },
    {FlashMessage( "eywi" ), 2 },
    {FlashMessage( "ebhe" ), 3 },
    {FlashMessage( "ebde" ), 4 },
    {FlashMessage( "easi" ), 5 },
    {FlashMessage( "eahe" ), 6 },
    {FlashMessage( "eati" ), 7 },
    {FlashMessage( "nole" ), 8 },
    {FlashMessage( "nowi" ), 9 },
    {FlashMessage( "noti" ), 10},
    {FlashMessage( "node" ), 11},
    {FlashMessage( "nohe" ), 12},
    {FlashMessage( "cbde" ), 13},
    {FlashMessage( "cbwi" ), 14},
    {FlashMessage( "cbhe" ), 15},
    {FlashMessage( "ckwi" ), 16},
    {FlashMessage( "chde" ), 17},
    {FlashMessage( "chwi" ), 18},
    {FlashMessage( "chhe" ), 19},
    {FlashMessage( "moth" ), 20},
    {FlashMessage( "mowi" ), 21},
    {FlashMessage( "mohe" ), 22},
    {FlashMessage( "mode" ), 23},
    {FlashMessage( "jade" ), 24},
    {FlashMessage( "jawi" ), 25},
    {FlashMessage( "jahe" ), 26},
    {FlashMessage( "fode" ), 27},
    {FlashMessage( "fowi" ), 28},
    {FlashMessage( "crhe" ), 29},
    {FlashMessage( "crwi" ), 30}
};

static inline Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

static inline void DisableDetail( const uint32_t detailHash ) {
    TDUDetail *pDetail = gpDetails->getDetailByHash( detailHash );
    if ( pDetail != nullptr ) {
        pDetail->setEnabled( false );
    }
}

static inline Frustum &GetNearFrustum() {
    return gpRender->getRenderPass<RP_Near>().pScene->getFrustumWrite();
}

static inline int32_t GetAvatarGender( const EntityAvatar *pAvatar ) {
    return pAvatar->getPooledEntry()->Body.getAvatarPart()->Gender;
}

static AvatarClotheCursor *
NewHeadPartCursor( AvatarPartDB *pDB,
                   const int32_t gender,
                   const eAvatarCustomizable category ) {
    // FUN_007a2750 / FUN_007a26d0 / FUN_007a2650
    AvatarClotheCursor *pCursor = new AvatarWrappingClotheCursor(
        pDB, gender, category, AvatarClotheCursor::kAnyClotheSet );
    pCursor->findRange();
    if ( pDB->getCount() > 0 && pCursor->hasRange() ) {
        return pCursor;
    }

    delete pCursor;
    return nullptr;
}

static AvatarGenderCursor *NewEyeCursor( AvatarPartDB *pDB,
                                         const int32_t gender ) {
    // FUN_007a1990
    AvatarGenderCursor *pCursor = new AvatarWrappingGenderCursor( pDB, gender );
    pCursor->findRange();
    if ( pDB->getCount() > 0 && pCursor->hasRange() ) {
        return pCursor;
    }

    delete pCursor;
    return nullptr;
}

static int32_t CountEyes( AvatarPartDB *pDB, const int32_t gender ) {
    // FUN_007a1550
    AvatarGenderCursor cursor( pDB, gender );
    cursor.findRange();
    int32_t numEyes = 0;
    if ( pDB->getCount() > 0 && cursor.hasRange() ) {
        while ( !cursor.isAtEnd() ) {
            numEyes++;
            cursor.next();
        }
    }
    return numEyes;
}

static void SetFaceParams( EntityAvatar *pAvatar,
                           const AvatarConfiguration &config ) {
    // FUN_008f4b50
    for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
        pAvatar->setFaceParam( i, config.FaceParams[i] );
    }
}

static const char *GetEyeColorName( const AvatarEyePart *pEye ) {
    const char *pName =
        gpDatabase->getStringByHashcode( kEyeNameDB, pEye->NameHC );
    if ( pName == nullptr ) {
        pName = gpDatabase->getStringByHashcode( kEyeNameDB,
                                                 kDefaultEyeNameHashcode );
    }
    return pName;
}

static inline void BindTintSlider( FlashSlider &slider,
                                   FlashPlayer *pFlashPlayer,
                                   const char *pVarName,
                                   const uint8_t colorIndex,
                                   const uint8_t numColors ) {
    slider.rebind( pFlashPlayer, pVarName, 1 );
    slider.setRange( 0.0f, static_cast<float>( std::max( numColors - 1, 1 ) ) );
    slider.setValue( static_cast<float>( colorIndex ) );
    slider.setAutoVelocity( true );
}

GMDressing::GMDressing()
    : GameModeScene(),
      AvatarCatalogScene(),
      mngReflection(),
      mngEnvMap(),
      savedNearPlane( 0.0f ),
      cameraNodeName( "C_INTRO" ),
      pPreviewAvatar( nullptr ),
      pOriginalAvatar( nullptr ),
      pOriginalPreviewAvatar( nullptr ),
      bAvatarSwapPending( false ),
      pIntroCinematic( nullptr ),
      poseLayer(),
      lookAtLayer(),
      previewHeelLayer(),
      previewHelmetLayer(),
      previewPoseLayer(),
      previewLookAtLayer(),
      pFocusedFaceSlider( nullptr ),
      hairCursors{ nullptr, nullptr },
      pActiveHairCursor( nullptr ),
      piloCursors{ nullptr, nullptr },
      pActivePiloCursor( nullptr ),
      eyebrowCursors{ nullptr, nullptr },
      pActiveEyebrowCursor( nullptr ),
      eyeCursors{ nullptr, nullptr },
      pActiveEyeCursor( nullptr ),
      debugPNJConfigIndex( 0 ),
      bIntroIsInteriorCinematic( false ),
      bUIPausedForIntro( false ),
      bIntroSavedSeating( false ),
      bIntroSavedFreeMovement( false ),
      bIntroSavedUsedIK( false ) {
    // FUN_007a3f60
    setTransitionFlags( 0x27 );

    registerManager( &mngReflection );
    registerManager( &mngEnvMap );
}

GMDressing::~GMDressing() {
    // FUN_007a3e40
}

void GMDressing::initialize() {
    // FUN_007a4df0
    GameModeScene::initialize();

    createEditorCursors();

    AvatarCatalogScene::initialize();

    const AvatarConfiguration *pSavedConfig =
        TestDrive::GetSavedAvatarConfiguration();
    pPreviewAvatar = gpScenaric->getAvatarManager().allocateAvatar( ~0ull, 0 );
    if ( pPreviewAvatar != nullptr ) {
        const int32_t activeClotheSet =
            pPreviewAvatar->getPooledEntry()->ActiveClotheSet;
        if ( activeClotheSet == kNoClotheSet ) {
            gpAvatar->bakeAvatar( pPreviewAvatar, pSavedConfig, 1, false,
                                  false );
        } else if ( activeClotheSet != 1 ) {
            gpAvatar->installClothes( pPreviewAvatar, pSavedConfig, 1, false,
                                      false );
        }
    }
    pPreviewAvatar->removeInstanceReference();
    pPreviewAvatar->getDriver()->setSeating( false );

    pPreviewAvatar->setLowResShadow( false );
    pAvatar->setLowResShadow( false );
    pOriginalAvatar = pAvatar;
    pOriginalPreviewAvatar = pPreviewAvatar;

    RenderAnm *pHeelAnim = findHeelAnim();
    if ( pHeelAnim != nullptr ) {
        previewHeelLayer.setHeelAnim( pHeelAnim );
    }

    if ( pPreviewAvatar != nullptr ) {
        pPreviewAvatar->addLayer( &previewHeelLayer );
        previewHeelLayer.enable();
        pPreviewAvatar->addLayer( &previewHelmetLayer );
        previewHelmetLayer.enable();
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bDrawSector = true;
    pStreaming->areaGrid.bDrawArea = true;
    pStreaming->setActiveRenderLayers( 0 );
    gpWorld->setEnabled( false );
    gpMngMusic->setPlayFlags( 1 );

    RenderScene *pReflectPrePass =
        gpRender->getRenderPass<RP_ReflectPrePass>().pScene;
    if ( pReflectPrePass != nullptr ) {
        pReflectPrePass->setEnabled( true );
    }

    gpScenaric->getAvatarManager().setFullUpdate( true );
}

void GMDressing::reset() {
    // FUN_007a39a0
    GameModeScene::reset();

    strcpy( frameLabel, "avatar_clic" );
    strcpy( movieName, "HOME" );
    AvatarCatalogScene::reset();

    DisableDetail( kDetailHashBirds );
    DisableDetail( kDetailHashPapers );

    gpCamera->setCameraMode( kDressingCameraMode, 0, 1.0f );
    CameraGame *pCamera = gpCamera->findCameraWithMode(
        kDressingCameraMode, gpCamera->getBoundCarState() );

    const Eigen::Vector4f position = scene.getWorldPosition();
    const Eigen::Vector4f target =
        position + Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
    pCamera->setPosition( position );
    pCamera->setTarget( target );
    pCamera->setUp( up );
    pCamera->updateMatrix( position.head<3>(), up.head<3>(), target.head<3>() );

    Frustum &nearFrustum = GetNearFrustum();
    savedNearPlane = nearFrustum.getZLimits().x();
    nearFrustum.setPlanes( kDressingNearPlane, nearFrustum.getZLimits().y() );

    const AvatarDBEntry *pBody =
        pAvatar->getPooledEntry()->Body.getAvatarPart();
    pFlashPlayer->setVariableValue( "/:IsWoman", pBody->Gender );

    refreshBikeGearVariable();

    pFlashPlayer->setVariableValue( "/:NbStyle", gpAvatar->getNumStyles() );
}

void GMDressing::tick( float deltaTime, float totalTime ) {
    // FUN_007a4980
    GameModeScene::tick( deltaTime, totalTime );
    AvatarCatalogScene::tick( deltaTime, totalTime );

    SpotMap *pCurrentSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    if ( pCurrentSpot != nullptr && pIntroCinematic != nullptr &&
         ( pIntroCinematic->getFlags() & 0x14u ) == 0 ) {
        if ( bIntroIsInteriorCinematic ) {
            const Eigen::Vector4f streamOrigin = GetStreamOrigin();
            Matrix4x4f_t interiorFrame = scene.getInteriorTransform();
            interiorFrame( 3, 0 ) -= streamOrigin.x();
            interiorFrame( 3, 1 ) -= streamOrigin.y();
            interiorFrame( 3, 2 ) -= streamOrigin.z();
            EntityDriver *pDriver = pAvatar->getDriver();
            bIntroSavedSeating = pDriver->isSeating();
            bIntroSavedFreeMovement = pDriver->isFreeMovement();
            bIntroSavedUsedIK = pDriver->isUsedIK();

            pIntroCinematic->launchCineAtFrame( pCurrentSpot, interiorFrame );

            SetHUDMoviesPaused( true );
            bUIPausedForIntro = true;
        } else {
            startIntroCinematic( pCurrentSpot, pIntroCinematic );
        }
    }

    if ( bUIPausedForIntro && pIntroCinematic->hasEnded() ) {
        SetHUDMoviesPaused( false );
        bUIPausedForIntro = false;
        restoreAvatarAfterIntro();
    }

    tickEditorSliders( deltaTime );
}

void GMDressing::draw( float deltaTime, float totalTime ) {
    // FUN_007a15c0
    if ( !bAvatarSwapPending ) {
        return;
    }

    PooledAvatarEntry *pPreviewEntry = pPreviewAvatar->getPooledEntry();
    if ( pPreviewEntry->getLoadingState() != AVT_Ready ) {
        return;
    }

    EntityAvatar *pPreviousAvatar = pAvatar;
    pAvatar = pPreviewAvatar;
    pPreviewAvatar = pPreviousAvatar;

    pPreviewAvatar->removeInstanceReference();
    pAvatar->addInstanceReference();

    syncCursorsToAvatar( pAvatar );
    bAvatarSwapPending = false;
}

void GMDressing::pause() {
    // FUN_007a31f0
    if ( pIntroCinematic != nullptr ) {
        pIntroCinematic->stop( true );
    }

    if ( bUIPausedForIntro ) {
        SetHUDMoviesPaused( false );
        bUIPausedForIntro = false;
    }

    Frustum &nearFrustum = GetNearFrustum();
    nearFrustum.setPlanes( savedNearPlane, nearFrustum.getZLimits().y() );

    pAvatar->setEnvMap( false, nullptr );
    if ( pPreviewAvatar != nullptr ) {
        pPreviewAvatar->setEnvMap( false, nullptr );
    }

    GameModeScene::pause();
}

void GMDressing::destroy() {
    // FUN_007a3c50
    gpScenaric->getAvatarManager().setFullUpdate( false );

    RenderScene *pReflectPrePass =
        gpRender->getRenderPass<RP_ReflectPrePass>().pScene;
    if ( pReflectPrePass != nullptr ) {
        pReflectPrePass->setEnabled( false );
    }

    pAvatar = pOriginalAvatar;
    pPreviewAvatar = pOriginalPreviewAvatar;
    if ( pPreviewAvatar != nullptr ) {
        pPreviewAvatar->removeLayer( &previewHeelLayer );
        pPreviewAvatar->removeLayer( &previewHelmetLayer );
        pPreviewAvatar->removeLayer( &previewPoseLayer );
        pPreviewAvatar->removeLayer( &previewLookAtLayer );
        pPreviewAvatar->setLowResShadow( true );
        gpScenaric->getAvatarManager().releaseAvatar( pPreviewAvatar );
    }

    if ( pAvatar != nullptr ) {
        pAvatar->removeLayer( &poseLayer );
        pAvatar->removeLayer( &lookAtLayer );
        pAvatar->setLowResShadow( true );
    }

    gpAvatar->bakeAvatar( pAvatar, TestDrive::GetSavedAvatarConfiguration(),
                          pAvatar->getPooledEntry()->ActiveClotheSet, false,
                          true );

    releaseEditorCursors();

    AvatarCatalogScene::destroy();

    gpWorld->setEnabled( true );
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );

    GameModeScene::destroy();
}

bool GMDressing::onMessage( FlashMessage *pMessage ) {
    // FUN_007a4270
    AvatarCatalogScene::onMessage( pMessage );

    switch ( *pMessage ) {
    case FlashMessage( "came" ): {
        if ( pIntroCinematic != nullptr ) {
            pIntroCinematic->stop( true );
        }

        char cameraName[16];
        pFlashPlayer->getVariableAsString( "/:WhatCam", cameraName );
        strncpy( cameraNodeName, cameraName, sizeof( cameraNodeName ) - 1 );
        cameraNodeName[sizeof( cameraNodeName ) - 1] = '\0';

        setHeadMode( strcmp( cameraNodeName, "C_head" ) == 0 );
        return true;
    }
    case FlashMessage( "drin" ):
    case FlashMessage( "neit" ):
    case FlashMessage( "prit" ):
        lookAtLayer.disable();
        return true;
    case FlashMessage( "avou" ):
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Living );
        return true;
    case FlashMessage( "chop" ):
        pAvatar->getPooledEntry()->bHasMotoGearsOnBike =
            pFlashPlayer->getVariableAsInt( "/:IsBikeGear" ) != 0;
        gpAvatar->savePlayerAvatarConfiguration( pAvatar );
        return true;
    case FlashMessage( "crav" ):
        gpAvatar->savePlayerAvatarConfiguration( pAvatar );
        gpPlayerData->requestSave( 1 );
        return true;
    case FlashMessage( "crou" ):
        revertFaceAndHair();
        return true;
    case FlashMessage( "goop" ):
        refreshBikeGearVariable();
        return true;
    case FlashMessage( "gohc" ):
    case FlashMessage( "nehc" ):
    case FlashMessage( "prhc" ):
        stepPart( *pMessage, FlashMessage( "nehc" ), FlashMessage( "prhc" ),
                  AC_Hair, pActiveHairCursor, hairTintSlider,
                  "popup/gauge_2/slider", "/:Haircut" );
        return true;
    case FlashMessage( "gopi" ):
    case FlashMessage( "nepi" ):
    case FlashMessage( "prpi" ):
        stepPart( *pMessage, FlashMessage( "nepi" ), FlashMessage( "prpi" ),
                  AC_FacePilosity, pActivePiloCursor, piloTintSlider,
                  "popup/gauge_2/slider", "/:Pilosity" );
        return true;
    case FlashMessage( "goeb" ):
    case FlashMessage( "neeb" ):
    case FlashMessage( "preb" ):
        stepPart( *pMessage, FlashMessage( "neeb" ), FlashMessage( "preb" ),
                  AC_Eyebrows, pActiveEyebrowCursor, eyebrowTintSlider,
                  "popup/gauge_4/slider", "/:EbShape" );
        return true;
    case FlashMessage( "goec" ):
    case FlashMessage( "neec" ):
    case FlashMessage( "prec" ):
        stepEyes( *pMessage );
        return true;
    case FlashMessage( "piti" ):
        piloTintSlider.setFocusState( true );
        return true;
    case FlashMessage( "ebti" ):
        eyebrowTintSlider.setFocusState( true );
        return true;
    case FlashMessage( "hati" ):
        hairTintSlider.setFocusState( true );
        return true;
    case FlashMessage( "skin" ):
        skinSlider.setFocusState( true );
        return true;
    case FlashMessage( "noga" ):
        pFlashPlayer->setVariableValue( "/:IsGaugeOff", 0 );
        skinSlider.setFocusState( false );
        hairTintSlider.setFocusState( false );
        piloTintSlider.setFocusState( false );
        eyebrowTintSlider.setFocusState( false );
        unfocusFaceSlider();
        return true;
    default:
        for ( const auto &faceMessage : kFaceSliderMessages ) {
            if ( *pMessage == faceMessage.first ) {
                return focusFaceSlider( faceMessage.second );
            }
        }
        return false;
    }
}

void GMDressing::onInteriorReady() {
    // FUN_007a3280
    AvatarCatalogScene::onInteriorReady();

    pIntroCinematic = static_cast<EntityCinematic *>(
        scene.getBank().findEntity( eEntityType::ET_CINE, "M_INTRO" ) );

    bIntroIsInteriorCinematic = false;
    if ( pIntroCinematic == nullptr ) {
        pIntroCinematic = static_cast<EntityCinematic *>(
            scene.getBank().findEntity( eEntityType::ET_CINE, "C_INTRO" ) );
        bIntroIsInteriorCinematic = ( pIntroCinematic != nullptr );
    }

    gpCamera->setCameraMode( kDressingCameraMode, 0, 1.0f );
    placeCameraOnNode( gpCamera->findCameraWithMode(
                           kDressingCameraMode, gpCamera->getBoundCarState() ),
                       cameraNodeName );

    placeAvatarsOnEntryNode();

    const Eigen::Vector4f streamOrigin = GetStreamOrigin();
    Eigen::Vector3f reflectionNormal( 0.0f, 0.0f, -1.0f );
    HiearchyNode *pRootNode = scene.getRootNode();
    Instance *pReflectNode =
        ( pRootNode != nullptr )
            ? pRootNode->findInstance( GetIdentifier64bit( "D_REFLECT" ) )
            : nullptr;
    if ( pReflectNode != nullptr ) {
        reflectionNormal = -pReflectNode->getModelMatrix().row( 1 ).head<3>();
    }

    RenderScene *pReflectScene = mngReflection.getBoundScene();
    if ( pReflectScene != nullptr ) {
        pReflectScene->setEnabled( true );
    }

    // NOTE: The original reads the Z component of the interior position here.
    const float reflectionOffset =
        ( scene.getWorldPosition().z() - streamOrigin.z() ) * 2.0f;
    mngReflection.setPlanarCoords( reflectionNormal, reflectionOffset );
    mngReflection.setClipEnabled( false );

    setInteriorEnvMap( pPreviewAvatar );

    if ( pIntroCinematic != nullptr &&
         ( pIntroCinematic->getFlags() & 0x14u ) == 0 &&
         ( transitionFlags & 1 ) == 0 ) {
        pIntroCinematic->setGameModeInterface( this );
        pIntroCinematic->launchSpotCine( scene.getSourceSpot(), 0.0f );
    }

    EntityAnmSet *pAnims = static_cast<EntityAnmSet *>(
        scene.getBank().findEntity( eEntityType::ET_ANMS, "Anims" ) );
    if ( pAnims != nullptr ) {
        static constexpr std::pair<AvatarPoseLayer::ePose, uint32_t>
            kPoseAnims[] = {
                {AvatarPoseLayer::AP_Male,       0xc },
                {AvatarPoseLayer::AP_HeadMale,   0xe },
                {AvatarPoseLayer::AP_Female,     0xd },
                {AvatarPoseLayer::AP_HeadFemale, 0xf },
                {AvatarPoseLayer::AP_Bike,       0x12}
        };
        for ( const auto &poseAnim : kPoseAnims ) {
            RenderAnm *pAnim = pAnims->findAnimation( poseAnim.second );
            if ( pAnim != nullptr ) {
                poseLayer.setPoseAnim( poseAnim.first, pAnim );
                previewPoseLayer.setPoseAnim( poseAnim.first, pAnim );
            }
        }

        RenderAnm *pHorizontalAnim = pAnims->findAnimation( 0x10 );
        RenderAnm *pVerticalAnim = pAnims->findAnimation( 0x11 );
        lookAtLayer.setAnims( pHorizontalAnim, pVerticalAnim );
        previewLookAtLayer.setAnims( pHorizontalAnim, pVerticalAnim );
    }

    if ( pAvatar != nullptr ) {
        pAvatar->addLayer( &poseLayer );
        poseLayer.enable();
        pAvatar->addLayer( &lookAtLayer );
        lookAtLayer.disable();
    }

    if ( pPreviewAvatar != nullptr ) {
        pPreviewAvatar->addLayer( &previewPoseLayer );
        previewPoseLayer.enable();
        pPreviewAvatar->addLayer( &previewLookAtLayer );
        previewLookAtLayer.disable();
    }
}

bool GMDressing::startIntroCinematic( SpotMap *pSpot,
                                      EntityCinematic *pCinematic ) {
    // FUN_007a3160
    Matrix4x4f_t spotMatrix;
    pCinematic->calculateSpotMatrix( pSpot, spotMatrix );
    spotMatrix( 3, 1 ) += kUndergroundInteriorDepth;
    return pCinematic->launchCineAtFrame( pSpot, spotMatrix );
}

void GMDressing::placeCameraOnNode( CameraGame *pCamera,
                                    const char *pNodeName ) {
    // FUN_007a16b0
    ScenaricBank &interiorEntities = scene.getBank();
    for ( int32_t i = 0; i < interiorEntities.getEntityCount(); i++ ) {
        Entity *pEntity = interiorEntities.getEntity( i );
        if ( pEntity == nullptr || pEntity->getType() != eEntityType::ET__CAM ||
             pEntity->getInstanceName() != pNodeName ) {
            continue;
        }

        PlaceCameraOnEntity( pCamera, static_cast<EntityCamera *>( pEntity ) );
        return;
    }
}

AvatarClotheCursor *GMDressing::createCursor( AvatarPartDB *pDB,
                                              const int32_t category,
                                              const int32_t gender,
                                              const char *pBrand,
                                              const int32_t clotheSet ) {
    // FUN_007a1c80
    AvatarClotheCursor *pCursor =
        newListCursor( pDB, gender, category, clotheSet );
    pCursor->findRange();
    if ( pDB->getCount() > 0 && pCursor->hasRange() ) {
        return pCursor;
    }

    delete pCursor;
    return nullptr;
}

void GMDressing::syncCursorsToAvatar( EntityAvatar *pTargetAvatar ) {
    // FUN_007a1d60
    AvatarCatalogScene::syncCursorsToAvatar( pTargetAvatar );

    const int32_t gender = GetAvatarGender( pTargetAvatar );
    syncEditorCursors( pTargetAvatar, gender );
    bindEditorSliders( pTargetAvatar, gender );
}

int32_t GMDressing::countItems( AvatarPartDB *pDB,
                                const int32_t gender,
                                const int32_t category,
                                const char *pBrand,
                                const int32_t clotheSet,
                                int32_t *pOutNumPlaceholders ) {
    // FUN_007a1ba0
    if ( pOutNumPlaceholders != nullptr ) {
        *pOutNumPlaceholders = 0;
    }

    std::unique_ptr<AvatarClotheCursor> cursor(
        newListCursor( pDB, gender, category, clotheSet ) );
    cursor->findRange();
    if ( pDB->getCount() <= 0 || !cursor->hasRange() ) {
        return 0;
    }

    int32_t numItems = 0;
    while ( !cursor->isAtEnd() ) {
        const AvatarDBEntry *pPart = cursor->getResult();
        if ( pOutNumPlaceholders != nullptr &&
             IsPlaceholderMesh( pPart->getMeshName() ) ) {
            ( *pOutNumPlaceholders )++;
        }
        numItems++;
        cursor->next();
    }
    return numItems;
}

int32_t GMDressing::indexOfItem( AvatarPartDB *pDB,
                                 const AvatarClothePart *pPart,
                                 const int32_t clotheSet ) {
    // FUN_007a1ad0
    std::unique_ptr<AvatarClotheCursor> cursor(
        newListCursor( pDB, pPart->Gender, pPart->Category, clotheSet ) );
    cursor->findRange();
    if ( pDB->getCount() <= 0 || !cursor->hasRange() ) {
        return 0;
    }

    int32_t index = 0;
    do {
        index++;
        if ( cursor->getResult() == pPart ) {
            break;
        }
        cursor->next();
    } while ( !cursor->isAtEnd() );
    return index;
}

bool GMDressing::resetColor( const AvatarClothePart *pPart,
                             uint8_t *pColorIndex ) {
    // FUN_007a1410
    static constexpr int32_t kMaxColors = 16;

    if ( bIgnoreOwnership ) {
        return AvatarCatalogScene::resetColor( pPart, pColorIndex );
    }

    for ( int32_t i = 0; i < kMaxColors; i++ ) {
        if ( pPart->isColorOwned( static_cast<uint8_t>( i ) ) ) {
            *pColorIndex = static_cast<uint8_t>( i );
            return true;
        }
    }
    return false;
}

bool GMDressing::nextColor( const AvatarClothePart *pPart,
                            uint8_t *pColorIndex,
                            const uint8_t numColors ) {
    // FUN_007a13b0
    if ( bIgnoreOwnership ) {
        return AvatarCatalogScene::nextColor( pPart, pColorIndex, numColors );
    }

    if ( !pPart->isOwned() || numColors == 0 ) {
        return false;
    }

    int32_t colorIndex = ( *pColorIndex + 1 ) % numColors;
    while ( !pPart->isColorOwned( static_cast<uint8_t>( colorIndex ) ) ) {
        colorIndex = ( colorIndex + 1 ) % numColors;
    }
    *pColorIndex = static_cast<uint8_t>( colorIndex );
    return true;
}

void GMDressing::resetToSavedOutfit() {
    // FUN_007a3090
    AvatarConfiguration currentConfig;
    gpAvatar->readAvatarConfig( pAvatar, currentConfig );

    const AvatarConfiguration *pSavedConfig =
        TestDrive::GetSavedAvatarConfiguration();
    const int32_t clotheSet = getCurrentClotheSet();
    if ( currentConfig.isSameAs( *pSavedConfig ) &&
         pAvatar->getPooledEntry()->ActiveClotheSet == clotheSet ) {
        updateItemCounts();
        return;
    }

    gpAvatar->bakeAvatar( pPreviewAvatar, pSavedConfig, clotheSet, true, true );
    updateItemCounts();
    bAvatarSwapPending = true;
}

void GMDressing::refreshChangeColorVariable() {
    // FUN_007a1a40
    const int32_t category = getCurrentCategory();
    if ( category == kNoCategory || activeCursors[category] == nullptr ) {
        return;
    }

    const AvatarClothePart *pItem = getActiveItem( category );
    if ( pItem == nullptr ) {
        return;
    }

    const uint32_t numColors =
        pAvatar->getPooledEntry()->Slots[category].NumColors;
    int32_t numOwnedColors = 0;
    for ( uint32_t i = 0; i < numColors; i++ ) {
        if ( bIgnoreOwnership ||
             pItem->isColorOwned( static_cast<uint8_t>( i ) ) ) {
            numOwnedColors++;
        }
    }

    pFlashPlayer->setVariableValue( "/:IsChangeColor",
                                    ( numOwnedColors > 1 ) ? 1 : 0 );
}

AvatarClotheCursor *GMDressing::newListCursor( AvatarPartDB *pDB,
                                               const int32_t gender,
                                               const int32_t category,
                                               const int32_t clotheSet ) const {
    const eAvatarCustomizable clotheCategory =
        static_cast<eAvatarCustomizable>( category );
    if ( bIgnoreOwnership ) {
        return new AvatarClotheCursor( pDB, gender, clotheCategory, clotheSet );
    }
    return new AvatarOwnedClotheCursor( pDB, gender, clotheCategory,
                                        clotheSet );
}

#ifdef OTDU_USE_IMGUI
#include "imgui.h"

void GMDressing::drawDebug( float deltaTime ) {
    bool bShowAllClothes = bIgnoreOwnership;
    if ( ImGui::Checkbox( "Show all clothes", &bShowAllClothes ) ) {
        bIgnoreOwnership = bShowAllClothes;

        releaseCursors();
        createCursors();
        syncCursorsToAvatar( pAvatar );
        updateItemCounts();
    }

    if ( ImGui::Checkbox( "Disable face morph", &gbDisableFaceMorph ) ) {
        pAvatar->requestFaceRefresh();
        if ( pPreviewAvatar != nullptr ) {
            pPreviewAvatar->requestFaceRefresh();
        }
    }

    ImGui::SeparatorText( "Avatar configuration" );
    if ( debugPNJConfigNames.empty() ) {
        const int32_t numConfigs = gpAvatar->getNumPNJConfigurations();
        for ( int32_t i = 0; i < numConfigs; i++ ) {
            std::string name;
            AvatarConfiguration config;
            gpAvatar->getPNJConfiguration( i, name, config );
            const size_t separator = name.find_last_of( "\\/" );
            if ( separator != std::string::npos ) {
                name.erase( 0, separator + 1 );
            }
            debugPNJConfigNames.push_back( name.empty() ? std::to_string( i )
                                                        : name );
        }
    }

    if ( debugPNJConfigNames.empty() ) {
        ImGui::TextUnformatted( "PNJ.bnk is not loaded" );
        return;
    }

    debugPNJConfigIndex =
        std::clamp( debugPNJConfigIndex, 0,
                    static_cast<int32_t>( debugPNJConfigNames.size() ) - 1 );
    if ( ImGui::BeginCombo(
             "PNJ config (.avt)",
             debugPNJConfigNames[debugPNJConfigIndex].c_str() ) ) {
        for ( int32_t i = 0;
              i < static_cast<int32_t>( debugPNJConfigNames.size() ); i++ ) {
            const bool bSelected = ( i == debugPNJConfigIndex );
            if ( ImGui::Selectable( debugPNJConfigNames[i].c_str(),
                                    bSelected ) ) {
                debugPNJConfigIndex = i;
            }
            if ( bSelected ) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if ( ImGui::Button( "Apply to player" ) ) {
        std::string name;
        AvatarConfiguration config;
        if ( gpAvatar->getPNJConfiguration( debugPNJConfigIndex, name,
                                            config ) ) {
            applyDebugAvatarConfiguration( config );
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Prev" ) && debugPNJConfigIndex > 0 ) {
        debugPNJConfigIndex--;
        std::string name;
        AvatarConfiguration config;
        if ( gpAvatar->getPNJConfiguration( debugPNJConfigIndex, name,
                                            config ) ) {
            applyDebugAvatarConfiguration( config );
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Next" ) &&
         debugPNJConfigIndex + 1 <
             static_cast<int32_t>( debugPNJConfigNames.size() ) ) {
        debugPNJConfigIndex++;
        std::string name;
        AvatarConfiguration config;
        if ( gpAvatar->getPNJConfiguration( debugPNJConfigIndex, name,
                                            config ) ) {
            applyDebugAvatarConfiguration( config );
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Restore saved" ) ) {
        applyDebugAvatarConfiguration(
            *TestDrive::GetSavedAvatarConfiguration() );
    }
}
#endif

void GMDressing::applyDebugAvatarConfiguration(
    const AvatarConfiguration &config ) {
    gpAvatar->bakeAvatar( pAvatar, &config, getCurrentClotheSet(), false,
                          true );
    pFlashPlayer->setVariableValue(
        "/:IsWoman", pAvatar->getPooledEntry()->Body.getAvatarPart()->Gender );
    syncCursorsToAvatar( pAvatar );
    updateItemCounts();
}

void GMDressing::setHeadMode( const bool bHeadMode ) {
    // FUN_007a4270
    poseLayer.setMode( AvatarPoseLayer::APM_Body );
    poseLayer.refresh();
    previewPoseLayer.setMode( AvatarPoseLayer::APM_Body );
    previewPoseLayer.refresh();
    lookAtLayer.disable();
    previewLookAtLayer.disable();
    heelLayer.enable();
    previewHeelLayer.enable();

    if ( !bHeadMode ) {
        return;
    }

    poseLayer.setMode( AvatarPoseLayer::APM_Head );
    poseLayer.refresh();
    previewPoseLayer.setMode( AvatarPoseLayer::APM_Head );
    previewPoseLayer.refresh();
    lookAtLayer.enable();
    previewLookAtLayer.enable();
    heelLayer.disable();
    previewHeelLayer.disable();
}

void GMDressing::placeAvatarsOnEntryNode() {
    const Eigen::Vector4f streamOrigin = GetStreamOrigin();
    ScenaricBank &interiorEntities = scene.getBank();
    for ( int32_t i = 0; i < interiorEntities.getEntityCount(); i++ ) {
        Entity *pEntity = interiorEntities.getEntity( i );
        if ( pEntity == nullptr ||
             pEntity->getType() != eEntityType::ET__ENT ) {
            continue;
        }

        Matrix4x4f_t placement = pEntity->getModelMatrix();
        placement( 3, 0 ) -= streamOrigin.x();
        placement( 3, 1 ) -= streamOrigin.y();
        placement( 3, 2 ) -= streamOrigin.z();
        placement( 3, 3 ) -= streamOrigin.w();

        pAvatar->setTransform( placement, false );
        if ( pPreviewAvatar != nullptr ) {
            pPreviewAvatar->setTransform( placement, false );
        }
    }
}

void GMDressing::restoreAvatarAfterIntro() {
    EntityDriver *pDriver = pAvatar->getDriver();
    pDriver->setSeating( bIntroSavedSeating );
    pDriver->setFreeMovement( bIntroSavedFreeMovement );
    pDriver->setUsedIK( bIntroSavedUsedIK );

    placeAvatarsOnEntryNode();

    gpCamera->setCameraMode( kDressingCameraMode, 0, 1.0f );
    placeCameraOnNode( gpCamera->findCameraWithMode(
                           kDressingCameraMode, gpCamera->getBoundCarState() ),
                       cameraNodeName );

    poseLayer.disable();
    poseLayer.enable();
    if ( heelLayer.isEnabled() ) {
        heelLayer.disable();
        heelLayer.enable();
    }
}

void GMDressing::createEditorCursors() {
    // FUN_007a4df0 (inlined)
    AvatarPartDB *pDB = gpAvatar->getClothesDB();
    AvatarPartDB *pEyeDB = gpAvatar->getEyeDB();
    for ( int32_t gender = 0; gender < kNumGenders; gender++ ) {
        hairCursors[gender] = NewHeadPartCursor( pDB, gender, AC_Hair );
        piloCursors[gender] = NewHeadPartCursor( pDB, gender, AC_FacePilosity );
        eyebrowCursors[gender] = NewHeadPartCursor( pDB, gender, AC_Eyebrows );
        eyeCursors[gender] = NewEyeCursor( pEyeDB, gender );
    }
}

void GMDressing::releaseEditorCursors() {
    // FUN_007a3c50 (inlined)
    for ( int32_t gender = 0; gender < kNumGenders; gender++ ) {
        delete hairCursors[gender];
        delete eyebrowCursors[gender];
        delete piloCursors[gender];
        delete eyeCursors[gender];
        hairCursors[gender] = nullptr;
        eyebrowCursors[gender] = nullptr;
        piloCursors[gender] = nullptr;
        eyeCursors[gender] = nullptr;
    }
    pActiveHairCursor = nullptr;
    pActivePiloCursor = nullptr;
    pActiveEyebrowCursor = nullptr;
    pActiveEyeCursor = nullptr;
}

void GMDressing::syncEditorCursors( EntityAvatar *pTargetAvatar,
                                    const int32_t gender ) {
    // FUN_007a1d60 (inlined)
    if ( gender != 0 && gender != 1 ) {
        return;
    }

    pActiveHairCursor = hairCursors[gender];
    pActivePiloCursor = piloCursors[gender];
    pActiveEyeCursor = eyeCursors[gender];
    pActiveEyebrowCursor = eyebrowCursors[gender];

    PooledAvatarEntry *pRsc = pTargetAvatar->getPooledEntry();
    const int32_t clotheSet = pRsc->ActiveClotheSet;
    if ( clotheSet < 0 || clotheSet >= kNumClotheTypes ) {
        return;
    }

    auto seekToWornPart = [&]( AvatarClotheCursor *pCursor,
                               const eAvatarCustomizable category ) {
        if ( pCursor == nullptr ) {
            return;
        }

        const AvatarClothePart *pPart =
            gpAvatar->findClotheByID( pRsc->pClothesHC[clotheSet][category] );
        if ( pPart == nullptr || pPart->Gender != gender ||
             pPart->Category != category ||
             ( pPart->DrivingType != clotheSet &&
               pPart->DrivingType != ADT_Mixte ) ) {
            return;
        }

        const int32_t numItems = countItems( pClothesDB, gender, category,
                                             nullptr, clotheSet, nullptr );
        pCursor->rewind();
        for ( int32_t i = 0; i < numItems; i++ ) {
            if ( pCursor->getResult() == pPart ) {
                break;
            }
            pCursor->next();
        }
    };
    seekToWornPart( hairCursors[gender], AC_Hair );
    seekToWornPart( piloCursors[gender], AC_FacePilosity );
    seekToWornPart( eyebrowCursors[gender], AC_Eyebrows );

    AvatarGenderCursor *pEyeCursor = eyeCursors[gender];
    const AvatarEyePart *pEye = pRsc->pEyeResource;
    if ( pEyeCursor != nullptr && pEye != nullptr && pEye->Gender == gender ) {
        const int32_t numEyes = CountEyes( gpAvatar->getEyeDB(), gender );
        pEyeCursor->rewind();
        for ( int32_t i = 0; i < numEyes; i++ ) {
            if ( pEyeCursor->getResult() == pEye ) {
                break;
            }
            pEyeCursor->next();
        }
    }
}

void GMDressing::bindEditorSliders( EntityAvatar *pTargetAvatar,
                                    const int32_t gender ) {
    // FUN_007a1d60 (inlined)
    PooledAvatarEntry *pRsc = pTargetAvatar->getPooledEntry();
    const AvatarSkinPart *pSkin = pRsc->pSkinResource;
    if ( pSkin != nullptr ) {
        skinSlider.rebind( pFlashPlayer, "/:AV_Skin", 1 );
        skinSlider.setRange( 0.0f, kMaxSkinIndex );
        skinSlider.setAutoVelocity( true );

        uint32_t skinIndex = pSkin->IDHashcode;
        if ( gender == 1 ) {
            skinIndex -= kFemaleSkinIDOffset;
        }
        skinSlider.setValue( static_cast<float>( skinIndex ) );
    }

    const int32_t clotheSet = pRsc->ActiveClotheSet;
    BindTintSlider( hairTintSlider, pFlashPlayer, "/:AV_HairTint",
                    pRsc->pColors[clotheSet][AC_Hair],
                    pRsc->Slots[AC_Hair].NumColors );
    BindTintSlider( piloTintSlider, pFlashPlayer, "/:AV_PiloTint",
                    pRsc->pColors[clotheSet][AC_FacePilosity],
                    pRsc->Slots[AC_FacePilosity].NumColors );
    BindTintSlider( eyebrowTintSlider, pFlashPlayer, "/:AV_EyebrowTint",
                    pRsc->pColors[clotheSet][AC_Eyebrows],
                    pRsc->Slots[AC_Eyebrows].NumColors );

    for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
        FlashSlider &slider = faceSliders[i];
        slider.rebind( pFlashPlayer, kFaceSliderVariables[i], 1 );
        slider.setRange( 0.0f, kSliderMaxValue );
        slider.setIndex( i );
        slider.setVelocity( kFaceSliderVelocity );
        slider.setValue( static_cast<float>(
            static_cast<uint32_t>( pTargetAvatar->getFaceParam( i ) ) ) );
    }
}

bool GMDressing::focusFaceSlider( const int32_t index ) {
    // FUN_007a14b0
    unfocusFaceSlider();
    pFocusedFaceSlider = &faceSliders[index];
    pFocusedFaceSlider->setFocusState( true );
    return true;
}

void GMDressing::unfocusFaceSlider() {
    // FUN_007a1470
    if ( pFocusedFaceSlider != nullptr ) {
        pFocusedFaceSlider->setFocusState( false );
        pFocusedFaceSlider = nullptr;
    }
}

void GMDressing::tickEditorSliders( const float deltaTime ) {
    // FUN_007a4980 (inlined)
    auto updateGaugeOff = [&]( const FlashSlider &slider ) {
        pFlashPlayer->setVariableValue(
            "/:IsGaugeOff",
            ( slider.isLeftPressed() || slider.isRightPressed() ) ? 1 : 0 );
    };

    if ( pFocusedFaceSlider != nullptr && pFocusedFaceSlider->hasFocus() ) {
        pFocusedFaceSlider->tick( deltaTime );
        updateGaugeOff( *pFocusedFaceSlider );
        if ( pAvatar != nullptr ) {
            const uint8_t faceParam = static_cast<uint8_t>(
                static_cast<uint32_t>( pFocusedFaceSlider->getValue() ) );
            pAvatar->setFaceParam( pFocusedFaceSlider->getIndex(), faceParam );
        }
    }

    if ( skinSlider.hasFocus() ) {
        skinSlider.tick( deltaTime );
        updateGaugeOff( skinSlider );

        const int32_t gender = GetAvatarGender( pAvatar );
        int32_t skinID = static_cast<int32_t>( skinSlider.getValue() );
        if ( gender == 1 ) {
            skinID += kFemaleSkinIDOffset;
        }

        AvatarGenderCursor cursor( gpAvatar->getSkinDB(), gender );
        cursor.findRange();
        cursor.seekTo( static_cast<uint16_t>( skinID ) );
        AvatarSkinPart *pSkin =
            cursor.hasRange()
                ? static_cast<AvatarSkinPart *>( cursor.getResult() )
                : nullptr;
        pAvatar->setSkin( pSkin );
    }

    tickTintSlider( hairTintSlider, AC_Hair, pActiveHairCursor, deltaTime );
    tickTintSlider( piloTintSlider, AC_FacePilosity, pActivePiloCursor,
                    deltaTime );
    tickTintSlider( eyebrowTintSlider, AC_Eyebrows, pActiveEyebrowCursor,
                    deltaTime );
}

void GMDressing::tickTintSlider( FlashSlider &slider,
                                 const int32_t category,
                                 AvatarClotheCursor *pCursor,
                                 const float deltaTime ) {
    // FUN_007a4980 (inlined)
    if ( !slider.hasFocus() ) {
        return;
    }

    const uint8_t numColors =
        pAvatar->getPooledEntry()->Slots[category].NumColors;
    if ( numColors <= 1 ) {
        return;
    }

    slider.setMaxValue( static_cast<float>( std::max( numColors - 1, 1 ) ) );
    slider.tick( deltaTime );
    pFlashPlayer->setVariableValue(
        "/:IsGaugeOff",
        ( slider.isLeftPressed() || slider.isRightPressed() ) ? 1 : 0 );

    AvatarDBEntry *pPart = pCursor->getResult();
    PooledAvatarEntry *pRsc = pAvatar->getPooledEntry();
    if ( pPart != nullptr && pRsc != nullptr ) {
        pRsc->installClothePart(
            pPart,
            static_cast<uint8_t>( static_cast<uint32_t>( slider.getValue() ) ),
            pRsc->ActiveClotheSet, true, true );
    }
}

void GMDressing::stepPart( const FlashMessage &message,
                           const FlashMessage &nextMessage,
                           const FlashMessage &previousMessage,
                           const int32_t category,
                           AvatarClotheCursor *pCursor,
                           FlashSlider &tintSlider,
                           const char *pGaugePath,
                           const char *pNameVariable ) {
    // FUN_007a2ec0 / FUN_007a2cf0 / FUN_007a2b20
    PooledAvatarEntry *pRsc = pAvatar->getPooledEntry();
    if ( pCursor == nullptr ) {
        if ( pRsc != nullptr ) {
            pRsc->Slots[category].decreaseReferenceCount();
        }
        return;
    }

    if ( message == nextMessage || message == previousMessage ) {
        tintSlider.setValue( 0.0f );
        tintSlider.setMaxValue( 1.0f );
        tintSlider.tick( 0.0f );

        const float ratio =
            Clamp( ( tintSlider.getValue() - tintSlider.getMinValue() ) /
                       ( tintSlider.getMaxValue() - tintSlider.getMinValue() ),
                   0.0f, 1.0f );
        pFlashPlayer->goToFrameAndPlay(
            static_cast<int32_t>( floorf( ratio * kSliderMaxValue ) ),
            pGaugePath, false );

        if ( message == nextMessage ) {
            pCursor->next();
        }
        if ( message == previousMessage ) {
            pCursor->previous();
        }
    }

    AvatarClothePart *pPart =
        static_cast<AvatarClothePart *>( pCursor->getResult() );
    if ( pRsc != nullptr ) {
        pRsc->installClothePart( pPart,
                                 static_cast<uint8_t>( static_cast<uint32_t>(
                                     tintSlider.getValue() ) ),
                                 pRsc->ActiveClotheSet, true, true );
    }

    pFlashPlayer->setVariableValue( "/:Nametemp", pPart->getDisplayName() );
    pFlashPlayer->setVariableValue( pNameVariable, pPart->getDisplayName() );
}

void GMDressing::stepEyes( const FlashMessage &message ) {
    // FUN_007a29d0
    if ( pActiveEyeCursor == nullptr ) {
        return;
    }

    if ( message == FlashMessage( "neec" ) ) {
        pActiveEyeCursor->next();
    }
    if ( message == FlashMessage( "prec" ) ) {
        pActiveEyeCursor->previous();
    }

    AvatarEyePart *pEye =
        static_cast<AvatarEyePart *>( pActiveEyeCursor->getResult() );
    PooledAvatarEntry *pRsc = pAvatar->getPooledEntry();
    pRsc->pEyeResource = pEye;
    pRsc->tickEyeMaterials();

    pFlashPlayer->setVariableValue( "/:Nametemp", GetEyeColorName( pEye ) );
    pFlashPlayer->setVariableValue( "/:EyesColor", GetEyeColorName( pEye ) );
}

void GMDressing::revertFaceAndHair() {
    // FUN_007a27d0
    AvatarConfiguration currentConfig;
    gpAvatar->readAvatarConfig( pAvatar, currentConfig );

    const AvatarConfiguration *pSavedConfig =
        TestDrive::GetSavedAvatarConfiguration();
    const int32_t clotheSet = getCurrentClotheSet();
    PooledAvatarEntry *pRsc = pAvatar->getPooledEntry();
    if ( currentConfig.isSameAs( *pSavedConfig ) &&
         pRsc->ActiveClotheSet == clotheSet ) {
        return;
    }

    gpAvatar->installSkinResource( pAvatar, pSavedConfig );
    gpAvatar->installEyeResource( pAvatar, pSavedConfig );
    SetFaceParams( pAvatar, *pSavedConfig );
    pRsc->setClothesFromConfig( *pSavedConfig );

    static constexpr eAvatarCustomizable kHeadParts[] = {
        AC_Hair, AC_FacePilosity, AC_Eyebrows };
    for ( const eAvatarCustomizable category : kHeadParts ) {
        pRsc->Slots[category].decreaseReferenceCount();

        const uint8_t colorIndex = pRsc->pColors[clotheSet][category];
        AvatarClothePart *pPart =
            gpAvatar->findClotheByID( pRsc->pClothesHC[clotheSet][category] );
        if ( pPart != nullptr && pPart->Category == category &&
             pPart->Gender == GetAvatarGender( pAvatar ) ) {
            pRsc->installClothePart( pPart, colorIndex, clotheSet, false,
                                     true );
        } else {
            pRsc->Slots[category].decreaseReferenceCount();
        }
    }

    syncCursorsToAvatar( pAvatar );
}

void GMDressing::refreshBikeGearVariable() {
    // FUN_007a1a00
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr || pLoadedSave->getAvatar() == nullptr ) {
        return;
    }

    pFlashPlayer->setVariableValue(
        "/:IsBikeGear",
        pLoadedSave->getAvatar()->Config.hasMotoGears() ? 1 : 0 );
}
