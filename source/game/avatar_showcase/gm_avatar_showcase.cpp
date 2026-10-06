#include "shared.h"
#include "gm_avatar_showcase.h"

#include "tdu_instance.h"

#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/avatar/gs_avatar.h"
#include "game/gm_scene.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_anm_set.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "player_data/gs_playerdata.h"
#include "render/camera/camera_showcase.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

static constexpr int32_t kCameraModeShowcase = 6;

MngFlashAvatarShowcase::MngFlashAvatarShowcase( GMAvatarShowcase *pOwner )
    : MngFlash(), pGameModeInstance( pOwner ) {}

bool MngFlashAvatarShowcase::onMessage( FlashMessage *pMessage,
                                        FlashPlayer *pPlayer ) {
    // FUN_007b6b90
    if ( pGameModeInstance == nullptr ) {
        return false;
    }

    return pGameModeInstance->handleFlashMessage( pMessage );
}

GMAvatarShowcase::GMAvatarShowcase()
    : GameMode(),
      animSetBank(),
      avatarConfig(),
      mngActor(),
      mngNumber(),
      mngFlash( this ),
      pAvatar( nullptr ),
      pFlashPlayer( nullptr ),
      backgroundColor( 29, 41, 44, 0xff ),
      clearColor( 0u ),
      animIndex( 1 ),
      pAnimSet( nullptr ) {
    // FUN_007b6a80
    registerManager( &mngActor );
    registerManager( &mngFlash );
    registerManager( &mngNumber );
}

GMAvatarShowcase::~GMAvatarShowcase() {
    // FUN_007b6a30
}

void GMAvatarShowcase::initialize() {
    // FUN_007b6eb0
    if ( gpWorldStreaming->isStreamingActive() ) {
        gpWorldStreaming->resetStreamCenter();
        gpWorldStreaming->updateStreaming();
    }

    GSScenaric::LoadScenaricBank( "Scenarics/Avatars", "animset",
                                  &animSetBank );

    pAnimSet = ( animSetBank.getEntityCount() != 0 )
                   ? static_cast<EntityAnmSet *>( animSetBank.getEntity( 0 ) )
                   : nullptr;
}

void GMAvatarShowcase::reset() {
    // FUN_007b6c20

    // HACK: Force HDR to fix frame composition (broken in the original game).
    ToggleHDRAndRadialBlur();

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bDrawSector = true;
    pStreaming->areaGrid.bDrawArea = true;
    pStreaming->setActiveRenderLayers( 0 );

    RenderPass &nearPass = gpRender->getRenderPass<RP_Near>();
    clearColor = nearPass.pViewport->ClearColor;

    gpCamera->setCameraMode( kCameraModeShowcase, 0, 1.0f );
    CameraShowcase *pCamera =
        static_cast<CameraShowcase *>( gpCamera->findCameraWithMode(
            kCameraModeShowcase, gpCamera->getBoundCarState() ) );
    pCamera->setDistanceRange( 1.0f, 10.0f );
    pCamera->setOffset( Eigen::Vector4f( 0.0f, 0.75f, 0.0f, 0.0f ) );
    pCamera->setTarget( Eigen::Vector4f::Zero() );
    pCamera->placeAt( Eigen::Vector4f( 5.0f, 0.75f, 5.0f, 0.0f ), true );
    pCamera->setAllowRoll( true );
    pCamera->setCanMoveCamera( true );
    pCamera->setCollisionFlags( 0 );
    pCamera->setTargetCar( nullptr, nullptr );
    pCamera->setAspectRatio( static_cast<float>( gpRender->getWidth() ),
                             static_cast<float>( gpRender->getHeight() ) );

    Viewport *pViewport = nearPass.pViewport;
    pViewport->X = 0.24f;
    pViewport->Y = 0.1f;
    pViewport->Width = 0.76f;
    pViewport->Height = 0.76f;

    pFlashPlayer = gpFlash->getFlashPlayer( "AVATAR" );

    pAvatar = gpScenaric->getAvatarManager().allocateAvatar( ~0ull, 1 );
    if ( pAvatar == nullptr ) {
        return;
    }

    // HACK: Retail relies on the instance reference taken by the mode it was
    // entered from; nothing in the shipped game reaches this mode.
    pAvatar->addInstanceReference();

    const AvatarConfiguration *pConfig =
        TestDrive::GetSavedAvatarConfiguration();
    const int32_t activeClotheSet = pAvatar->getPooledEntry()->ActiveClotheSet;
    if ( activeClotheSet == kNoClotheSet ) {
        gpAvatar->bakeAvatar( pAvatar, pConfig, 0, false, false );
    } else if ( activeClotheSet != 0 ) {
        gpAvatar->installClothes( pAvatar, pConfig, 0, false, false );
    }
}

void GMAvatarShowcase::tick( float deltaTime, float totalTime ) {
    // FUN_007b6630
    SampleFrameRate();

    if ( gpMngNumber != nullptr ) {
        const float screenWidth = static_cast<float>( gpRender->getWidth() );
        const float screenHeight = static_cast<float>( gpRender->getHeight() );

        Eigen::Vector3f position( ( screenWidth - 130.0f ) / screenWidth,
                                  50.0f / screenHeight, 0.0f );
        gpMngNumber->addNumber( position, 10, ColorRGBA( 0, 0xff, 0, 0xff ),
                                static_cast<int32_t>( gCurrentFPS ) );
    }
}

void GMAvatarShowcase::pause() {
    // FUN_007b6bb0
    if ( pAvatar != nullptr ) {
        pAvatar->removeInstanceReference();
    }
    gpScenaric->getAvatarManager().releaseAvatar( pAvatar );

    Viewport *pViewport = gpRender->getRenderPass<RP_Near>().pViewport;
    pViewport->X = 0.0f;
    pViewport->Y = 0.0f;
    pViewport->Width = 1.0f;
    pViewport->Height = 1.0f;

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );
}

void GMAvatarShowcase::destroy() {
    // FUN_007b6ea0
    gpScenaric->unloadBank( &animSetBank );
}

bool GMAvatarShowcase::setFaceParam( const uint32_t value,
                                     const int32_t index ) {
    // FUN_007b6600
    if ( pAvatar != nullptr ) {
        pAvatar->setFaceParam( index, value );
    }
    return true;
}

bool GMAvatarShowcase::setFaceParamFromVariable( const char *pVariableName,
                                                 const int32_t index ) {
    const uint32_t value = static_cast<uint32_t>(
        pFlashPlayer->getVariableAsInt( pVariableName ) );
    return setFaceParam( value, index );
}

int32_t GMAvatarShowcase::getNumAnimations() const {
    return ( pAnimSet != nullptr ) ? pAnimSet->getNumAnimations() : 0;
}

bool GMAvatarShowcase::handleFlashMessage( FlashMessage *pMessage ) {
    // FUN_007b6750
    static constexpr FlashMessage kEyesHeight = FlashMessage( "eyhe" );
    static constexpr FlashMessage kEyebrowsHeight = FlashMessage( "ebhe" );
    static constexpr FlashMessage kEarsTilt = FlashMessage( "eati" );
    static constexpr FlashMessage kNoseTilt = FlashMessage( "noti" );
    static constexpr FlashMessage kCheekbonesWidth = FlashMessage( "cbwi" );
    static constexpr FlashMessage kChinWidth = FlashMessage( "chwi" );
    static constexpr FlashMessage kMouthTilt = FlashMessage( "moth" );
    static constexpr FlashMessage kJawWidth = FlashMessage( "jawi" );
    static constexpr FlashMessage kPreviousAnim = FlashMessage( "pran" );
    static constexpr FlashMessage kNextAnim = FlashMessage( "nean" );
    static constexpr FlashMessage kQuit = FlashMessage( "quit" );

    char itemCategory[32];
    pFlashPlayer->getVariableAsString( "/:WhatItemCat", itemCategory );

    switch ( *pMessage ) {
    case kEyesHeight:
        return setFaceParamFromVariable( "/:AV_Eyes_h", 0 );
    case kEyebrowsHeight:
        return setFaceParamFromVariable( "/:AV_Eb_h", 3 );
    case kEarsTilt:
        return setFaceParamFromVariable( "/:AV_Ears_t", 7 );
    case kNoseTilt:
        return setFaceParamFromVariable( "/:AV_Nose_t", 10 );
    case kCheekbonesWidth:
        return setFaceParamFromVariable( "/:AV_Cb_w", 14 );
    case kChinWidth:
        return setFaceParamFromVariable( "/:AV_Chin_w", 18 );
    case kMouthTilt:
        return setFaceParamFromVariable( "/:AV_Mouth_t", 20 );
    case kJawWidth:
        return setFaceParamFromVariable( "/:AV_Jaw_w", 25 );
    case kPreviousAnim:
        animIndex--;
        if ( animIndex < 0 ) {
            animIndex = getNumAnimations();
        }
        return true;
    case kNextAnim:
        animIndex++;
        if ( getNumAnimations() <= animIndex ) {
            animIndex = 0;
        }
        return true;
    case kQuit:
        gpAvatar->savePlayerAvatarConfiguration( pAvatar );
        bExitRequested = true;
        gpTestDriveInstance->setNextGameMode( GM_Home );
        return true;
    case FlashMessage( "crin" ):
    case FlashMessage( "crou" ):
    case FlashMessage( "heou" ):
    case FlashMessage( "neit" ):
    case FlashMessage( "nehc" ):
    case FlashMessage( "prhc" ):
    case FlashMessage( "prit" ):
        return true;
    default:
        return false;
    }
}
