#include "shared.h"
#include "mng_gme.h"

#include "config/gs_config.h"
#include "flash/gs_flash.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/game_mechanism.h"
#include "game/mng_hud.h"
#include "game/scenaric/entity/base_entity.h"
#include "ai/ai_debug_draw.h"
#include "core/maths_helper.h"
#include "render/3dd.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/serialized_skinned_model.h"
#include "game/instant_challenge/gm_instant_challenge.h"
#include "game/map/gm_map.h"
#include "game/map/mng_map_viewer.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/ige_file.h"
#include "player_data/saved_news.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

MngCursor *gpMngCursor = nullptr;
MngPreview *gpMngPreview = nullptr;

static constexpr int32_t kGPSCameraType = 3;
static constexpr float kCursorEdgeEnter = 0.05f;    // DAT_00f41778
static constexpr float kCursorEdgeLeave = 0.1f;     // FLOAT_00f41668
static constexpr float kCursorFarEdgeEnter = 0.95f; // DAT_00f4195c
static constexpr float kCursorFarEdgeLeave = 0.9f;  // DAT_00f416a0
static constexpr float kCursorPadSpeed = 0.6f;      // DAT_00f94134
static constexpr float kCursorPadDeadZone = 0.3f;
static constexpr float kCursorPadMax = 0.7f;

static bool gbCursorMoving = false; // DAT_016a6a55

static GMInstantChallenge *GetActiveEditor() {
    return static_cast<GMInstantChallenge *>( gpActiveGameMode );
}

static float ReadCursorAction( DeviceAction *pAction ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return 0.0f;
    }
    return pAction->FUN_00434bc0( kCursorPadDeadZone, kCursorPadMax );
}

MngCursor::MngCursor()
    : Manager(),
      bKeyLeft( false ),
      bKeyRight( false ),
      bKeyUp( false ),
      bKeyDown( false ),
      worldPosition( 48783.555f, 32.117592f, 52610.99f, 1.0f ),
      screenPosition( 0.5f, 0.5f, 0.0f, 1.0f ),
      groundPosition( Eigen::Vector4f::Zero() ),
      bHasValidCamera( true ),
      pActiveCamera( nullptr ),
      pCursorModels{ nullptr, nullptr },
      pViewport( nullptr ),
      bAtLeftEdge( false ),
      bAtRightEdge( false ),
      bAtBottomEdge( false ),
      bAtTopEdge( false ),
      bAtEdge( false ),
      bLockWorldPosition( false ),
      margins( Eigen::Vector4f::Zero() ),
      bCursorOff( false ) {
    // FUN_00787f20
    gpMngCursor = this;
}

MngCursor::~MngCursor() { gpMngCursor = nullptr; }

bool MngCursor::initialize( TestDriveGameInstance * ) {
    // FUN_00787c40
    return true;
}

void MngCursor::bindGPSCamera() {
    // FUN_00787e80 (inlined)
    CameraGame *pCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    if ( pCamera != nullptr && pCamera->getType() == kGPSCameraType ) {
        pActiveCamera = pCamera;
        bHasValidCamera = true;
    }
}

void MngCursor::tick( float deltaTime, float ) {
    // FUN_00788460
    if ( pActiveCamera == nullptr ) {
        bHasValidCamera = false;
        bindGPSCamera();
        if ( pActiveCamera == nullptr ) {
            return;
        }
    }

    GMInstantChallenge *pEditor = GetActiveEditor();
    if ( !gpScenaric->isInstantChallenge() ||
         pEditor->isLocalPlayerRouteOwner() ) {
        const int32_t state = pEditor->getEditorState();
        if ( state >= 0 && state <= 2 ) {
            updateEdgeFlags();
            updateInput( deltaTime );
        }
    }

    if ( !bLockWorldPosition ) {
        ScreenToWorldGround(
            Eigen::Vector2f( screenPosition.x(), screenPosition.y() ),
            worldPosition );
    }
}

void MngCursor::reset() {
    // FUN_00787e80
    margins = Eigen::Vector4f::Zero();
    bLockWorldPosition = false;
    bHasValidCamera = false;
    bindGPSCamera();
}

void MngCursor::draw() {
    // FUN_00787e20
    if ( !bHasValidCamera ) {
        return;
    }
    if ( pCursorModels[0] != nullptr || pCursorModels[1] != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: GME (cursor models: vtbl +0x4c set
                            // position, +0x3c draw in RP_Near)
    }
}

void MngCursor::pause() {
    // FUN_00787c30
    // NOP
}

void MngCursor::terminate() {
    // FUN_00787c20
    // NOP
}

void MngCursor::updateEdgeFlags() {
    // FUN_00787c50
    const float x = screenPosition.x();
    const float y = 1.0f - screenPosition.y();

    bAtLeftEdge =
        x < margins.x() + ( bAtLeftEdge ? kCursorEdgeLeave : kCursorEdgeEnter );
    bAtRightEdge =
        ( bAtRightEdge ? kCursorFarEdgeLeave : kCursorFarEdgeEnter ) -
            margins.z() <
        x;
    bAtBottomEdge = y < margins.y() + ( bAtBottomEdge ? kCursorEdgeLeave
                                                      : kCursorEdgeEnter );
    bAtTopEdge = ( bAtTopEdge ? kCursorFarEdgeLeave : kCursorFarEdgeEnter ) -
                     margins.w() <
                 y;

    GMInstantChallenge *pEditor = GetActiveEditor();
    if ( !bAtBottomEdge && !bAtTopEdge && !bAtLeftEdge && !bAtRightEdge ) {
        if ( bAtEdge ) {
            bAtEdge = false;
            pEditor->setCursorAtEdge( false );
        }
    } else if ( !bAtEdge ) {
        bAtEdge = true;
        if ( pEditor->isEdgeScrollEnabled() ) {
            pEditor->setCursorAtEdge( true );
        }
    }
}

void MngCursor::updateInput( const float deltaTime ) {
    // FUN_00788090
    if ( gpConfig->NbMaxPlayers < 1 ) {
        return;
    }
    GMInstantChallenge *pEditor = GetActiveEditor();
    if ( !pEditor->isEditorInputEnabled() || pEditor->isPaused() ) {
        return;
    }

    const bool bMouseActive = gpDevice->getMouseState().FUN_00980000();
    bool bMoving = false;
    if ( bCursorOff ) {
        bMoving = bMouseActive;
    } else if ( bMouseActive ) {
        screenPosition.x() = gEdFlash.Input.MouseCoordinates[0];
        screenPosition.y() = 1.0f - gEdFlash.Input.MouseCoordinates[1];
        bMoving = true;
    } else if ( gpConfig->pPlayerConfig != nullptr ) {
        PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
        const float left = ReadCursorAction( pPlayerConfig->pMapCursorLeft );
        const float right = ReadCursorAction( pPlayerConfig->pMapCursorRight );
        const float up = ReadCursorAction( pPlayerConfig->pMapCursorUp );
        const float down = ReadCursorAction( pPlayerConfig->pMapCursorDown );

        bool bPadMoved = false;
        if ( down > 0.0f ) {
            screenPosition.y() -= kCursorPadSpeed * down * deltaTime;
            bPadMoved = true;
        }
        if ( up > 0.0f ) {
            screenPosition.y() += kCursorPadSpeed * up * deltaTime;
            bPadMoved = true;
        }
        if ( left > 0.0f ) {
            screenPosition.x() -= kCursorPadSpeed * left * deltaTime;
            bPadMoved = true;
        }
        if ( right > 0.0f ) {
            screenPosition.x() += kCursorPadSpeed * right * deltaTime;
            bPadMoved = true;
        }

        if ( bPadMoved ) {
            float scrollX = 0.0f;
            float scrollZ = 0.0f;
            if ( screenPosition.x() <= margins.x() ) {
                scrollX = 0.0f - left;
            } else if ( margins.z() <= screenPosition.x() ) {
                scrollX = right;
            }
            if ( screenPosition.y() <= margins.y() ) {
                scrollZ = down;
            } else if ( margins.w() <= screenPosition.y() ) {
                scrollZ = 0.0f - up;
            }
            gpMapView->InputX = scrollX;
            gpMapView->InputZ = scrollZ;
            bMoving = true;
        }
    }

    if ( bMoving != gbCursorMoving ) {
        gbCursorMoving = bMoving;

        const char *pPageName = nullptr;
        const char *pFrame = "loop";
        if ( gpFlash->getFlashPlayer( "ICE" ) != nullptr ) {
            pPageName = "ICE";
            pFrame = gbCursorMoving ? "loop_sound" : "loop";
        } else if ( gpFlash->getFlashPlayer( "GMEditor" ) != nullptr ) {
            pPageName = "GMEditor";
            pFrame = gbCursorMoving ? "loop_sound" : "loop";
        }
        if ( pPageName != nullptr ) {
            gpFlash->getFlashPlayer( pPageName )
                ->showFrame( pFrame, "/curseur/curseur", true );
        }
    }

    if ( screenPosition.x() < margins.x() ) {
        screenPosition.x() = margins.x();
    }
    if ( screenPosition.y() < margins.y() ) {
        screenPosition.y() = margins.y();
    }
    if ( 1.0f - margins.z() < screenPosition.x() ) {
        screenPosition.x() = 1.0f - margins.z();
    }
    if ( 1.0f - margins.w() < screenPosition.y() ) {
        screenPosition.y() = 1.0f - margins.w();
    }
}

static constexpr float kPreviewDefaultDistance = 10.0f;  // FLOAT_00f4166c
static constexpr float kPreviewDefaultWidth = 0.2f;      // DAT_00f41730
static constexpr float kPreviewDefaultRadius = 12.0f;    // DAT_00f41850
static constexpr float kPreviewTargetHeightRatio = 0.4f; // DAT_00f94604
static constexpr float kPreviewDistanceRatio = 1.3f;     // DAT_00f94600
static constexpr float kPreviewBoundsScale = 2.0f;       // DAT_00f416b0
static constexpr float kPreviewOrbitSpeed = 3.1415927f;  // DAT_00f41940

MngPreview::MngPreview()
    : Manager(),
      camera(),
      viewport(),
      scene(),
      lightColor( Eigen::Vector4f::Zero() ),
      light(),
      lightSetup(),
      pDisplayedEntity( nullptr ),
      pPendingEntity( nullptr ),
      cameraDistance( kPreviewDefaultDistance ),
      cameraTarget( Eigen::Vector4f::Zero() ),
      bHidden( true ),
      bBackgroundColorSet( false ),
      bEntityDirty( true ),
      orbitAngle( 0.0f ) {
    // FUN_00782a10
    viewport.X = 0.0f;
    viewport.Y = 0.0f;
    viewport.Width = 1.0f;
    viewport.Height = 1.0f;
    viewport.Flags = 7;
    viewport.ClearColor = { 0, 0, 0, 0 };
    gpMngPreview = this;
}

MngPreview::~MngPreview() { gpMngPreview = nullptr; }

bool MngPreview::initialize( TestDriveGameInstance * ) {
    // FUN_00782890
    viewport.ClearColor = { 0, 0, 0, 0xff };
    viewport.X = 0.0f;
    viewport.Y = 0.0f;
    viewport.Width = kPreviewDefaultWidth;
    viewport.Height = 1.0f;
    bBackgroundColorSet = false;
    scene.create( &camera, &viewport, nullptr, 0, "GMEPreview", true, nullptr,
                  nullptr );
    scene.setEnabled( false );
    bHidden = true;
    lightColor = Eigen::Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
    light.Color = lightColor;
    lightSetup.addLight( &light );
    scene.addLightSetup( &lightSetup );
    return true;
}

void MngPreview::refreshEntityBounds() {
    // FUN_00808980 (inlined)
    Eigen::Vector4f sphere( 0.0f, 0.0f, 0.0f, kPreviewDefaultRadius );
    SerializedSkinnedModel *pModel = static_cast<SerializedSkinnedModel *>(
        pDisplayedEntity->getGeometryData() );
    Instance *pInstance = pModel->pInstance;
    if ( pInstance == nullptr && pModel->pHiearchNode != nullptr &&
         pModel->pHiearchNode->pPrevious != nullptr ) {
        pInstance = pModel->pHiearchNode->pPrevious->pInstance;
    }
    if ( pInstance != nullptr ) {
        const uint8_t *pEntry =
            (const uint8_t *)pInstance->getLOD( 0 ).pHashcode->pEntry;
        if ( pEntry != nullptr ) {
            sphere =
                ( (const RenderObject *)( pEntry + 0x10 ) )->BoundingSphere;
        }
    }

    cameraTarget = Eigen::Vector4f( sphere.x(), sphere.y(), sphere.z(), 1.0f );
    cameraTarget.y() +=
        ( kPreviewTargetHeightRatio - 0.5f ) * sphere.w() * kPreviewBoundsScale;
    cameraDistance = kPreviewDistanceRatio * sphere.w() * kPreviewBoundsScale;
}

void MngPreview::tick( float deltaTime, float ) {
    // FUN_00782b40
    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    if ( !bBackgroundColorSet && pHudPlayer != nullptr ) {
        char background[0x20] = {};
        pHudPlayer->getVariableAsString( "/:ViewportBG", background );
        if ( strlen( background ) > 5 ) {
            int32_t r = 0;
            int32_t g = 0;
            int32_t b = 0;
            sscanf( background, "%d %d %d", &r, &g, &b );
            viewport.ClearColor = { uint8_t( r ), uint8_t( g ), uint8_t( b ),
                                    0xff };
            bBackgroundColorSet = true;
        }
    }

    if ( ( pDisplayedEntity == nullptr && pPendingEntity == nullptr ) ||
         bHidden ) {
        return;
    }

    if ( pPendingEntity != nullptr ) {
        pDisplayedEntity = pPendingEntity;
        pPendingEntity = nullptr;
    }
    if ( bEntityDirty ) {
        refreshEntityBounds();
        bEntityDirty = false;
    }
    if ( pDisplayedEntity != nullptr ) {
        Eigen::Vector4f origin = GetShiftWorldPosition();
        pDisplayedEntity->setPosition( origin, true );
        static_cast<SerializedSkinnedModel *>(
            pDisplayedEntity->getGeometryData() )
            ->updateLOD(
                pDisplayedEntity->getMechanism(),
                const_cast<EntityDesc *>( pDisplayedEntity->getDesc() ) );
    }

    orbitAngle += deltaTime * kPreviewOrbitSpeed;
    camera.setTarget( cameraTarget );
    const Eigen::Vector4f direction =
        NormalizeVector4( Eigen::Vector4f( 8.0f, -4.0f, 0.0f, 0.0f ) );
    Matrix4x4f_t rotation = Matrix4x4f_t::Identity();
    MatrixRotateY( rotation, orbitAngle );
    const Eigen::Vector4f offset = TDUMat4x4MulVec( rotation, direction );
    const Eigen::Vector3f target = camera.getTarget();
    camera.setPosition(
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) -
        offset * cameraDistance );
    camera.updateMatrix( camera.getWorldPosition(), camera.getUp(),
                         camera.getTarget() );
}

void MngPreview::reset() {
    // FUN_00782800
    // NOP
}

void MngPreview::draw() {
    // FUN_007827d0
    if ( pDisplayedEntity != nullptr && !bHidden ) {
        pDisplayedEntity->draw( &scene );
    }
}

void MngPreview::pause() {
    // FUN_007827c0
    // NOP
}

void MngPreview::terminate() {
    // FUN_00782850
    lightSetup.removeLight( &light );
    hide();
    scene.destroy();
}

void MngPreview::hide() {
    // FUN_00782810
    if ( !bHidden ) {
        bHidden = true;
        scene.setEnabled( false );
    }
}

void MngPreview::show() {
    // FUN_00782830 (no caller in retail)
    if ( bHidden ) {
        bHidden = false;
        scene.setEnabled( true );
    }
}

void MngPreview::setEntity( Entity *pNewEntity ) {
    // FUN_00782ae0
    pPendingEntity = pNewEntity;
    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    if ( pNewEntity == nullptr ) {
        if ( pHudPlayer != nullptr ) {
            pHudPlayer->setVariableValue( "/:PointType", "" );
        }
        return;
    }

    bEntityDirty = true;
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:PointType",
                                      pNewEntity->getInstanceName().c_str() );
    }
}

MngGMEFlshI::MngGMEFlshI() : Manager(), pOwner( nullptr ) {}

MngGMEFlshI::~MngGMEFlshI() {}

void MngGMEFlshI::draw() {
    // FUN_00782fc0
    drawInterface();
}

void MngGMEFlshI::drawInterface() {
    // FUN_00782ea0
    if ( pOwner != nullptr ) {
        pOwner->drawInterface();
    }
}

MngTracRoadGME::MngTracRoadGME() : MngTracRoad(), pOwner( nullptr ) {}

MngTracRoadGME::~MngTracRoadGME() {}

AiRoadLocation *MngTracRoadGME::getWaypoint( const int32_t index ) {
    // FUN_00783030
    return &pOwner->getWaypoint( index );
}

void MngTracRoadGME::setNumWaypoints( const int32_t ) {
    // FUN_00783020
    // NOP
}

bool MngTracRoadGME::isActive() const {
    // FUN_007832e0
    if ( pOwner == nullptr ) {
        return false;
    }
    GameMechanism *pGameMech = pOwner->getGameMech();
    return pGameMech != nullptr && pGameMech->hasRaceHUD();
}

int32_t MngTracRoadGME::getNumWaypoints() const {
    // FUN_00783050
    return pOwner->getNumWaypoints();
}

bool MngTracRoadGME::isLoop() const {
    // FUN_007832c0
    return pOwner->getMissionDescription()->getLoop().getValue() != 0;
}

int32_t MngTracRoadGME::getLoopWaypointCount() const {
    // FUN_007832a0
    return int32_t(
        pOwner->getMissionDescription()->getLoopCheckpointIndex().getValue() );
}

FlashListGMEParameters::FlashListGMEParameters()
    : FlashListParameters(), pOwner( nullptr ), numNextStepLines( 0 ) {
    // FUN_00789650 (vtbl 0xf32950)
}

bool FlashListGMEParameters::onMessage( FlashMessage *pMessage,
                                        FlashPlayer *pPlayer ) {
    // FUN_0078aa20
    updateCategory();
    return FlashListParameters::onMessage( pMessage, pPlayer );
}

int32_t FlashListGMEParameters::getListLength() const {
    // FUN_00789550
    return pOwner->countParameters( pOwner->getParameterCategory() ) +
           numNextStepLines;
}

void FlashListGMEParameters::open() {
    // FUN_0078aa50
    updateCategory();
    FlashList::open();
}

GameParameter *FlashListGMEParameters::getParameter( const int32_t index ) {
    // FUN_00789520
    if ( isNextStepLine( index ) || pOwner == nullptr ) {
        return nullptr;
    }
    return pOwner->findParameter( index );
}

const char *
FlashListGMEParameters::applyClick( GameParameter *pParameter,
                                    int32_t *pOutCheckBoxState ) {
    // FUN_00789450
    return pParameter->onClick( pOutCheckBoxState );
}

void FlashListGMEParameters::onParameterChanged() {
    // FUN_00789a00
    gpScenaric->setModifiedIGE( true );
    gpScenaric->setTestedLevel( false );
    FlashPlayer *pEditorPlayer = pOwner->getEditorPlayer();
    if ( pEditorPlayer != nullptr ) {
        pEditorPlayer->showFrame( "red", "infos/fire", false );
    }
}

bool FlashListGMEParameters::isNextStepLine( const int32_t index ) {
    // FUN_00789360
    return index == getListLength() - 1 && numNextStepLines > 0;
}

void FlashListGMEParameters::updateCategory() {
    // FUN_00789580
    char page[0x20] = {};
    if ( pWidgetPlayer != nullptr ) {
        pWidgetPlayer->getVariableAsString( "/:WhatPara", page );
    }

    numNextStepLines = 1;
    const FlashMessage pageID( page );
    if ( pageID == FlashMessage( "fees" ) ||
         pageID == FlashMessage( "mult" ) ) {
        pOwner->setParameterCategory( 4 );
    } else if ( pageID == FlashMessage( "goal" ) ||
                pageID == FlashMessage( "sett" ) ) {
        pOwner->setParameterCategory( 1 );
    } else if ( pageID == FlashMessage( "rule" ) ) {
        pOwner->setParameterCategory( 3 );
    } else {
        numNextStepLines = 0;
        pOwner->setParameterCategory( 0 );
    }
}

FlashListGMEChallenges::FlashListGMEChallenges()
    : FlashList(), pEditorPlayer( nullptr ), pOwner( nullptr ) {
    // FUN_00789480 (vtbl 0xf32910)
}

int32_t FlashListGMEChallenges::getListLength() const {
    // FUN_00789440
    return gIGEFileRequest.NumSortedFiles;
}

void FlashListGMEChallenges::open() {
    // FUN_0078b090
    RefreshIGEFileList( &gIGEFileRequest, true, false );
    FlashPlayer *pPlayer = pOwner->getEditorPlayer();
    pPlayer->setVariableValue( "/:WhatGMType", "solo" );
    pPlayer->setVariableValue( "/:WhatGM", "empty" );
    pPlayer->setVariableValue( "/:ChallengeName", "" );
    pPlayer->setVariableValue( "/:ChallengeDescription", "" );
    selectChallenge( 0 );
    FlashList::open();
}

void FlashListGMEChallenges::close() {
    // FUN_007899b0
    bOpened = false;
    pOwner->getEditorPlayer()->setVariableValue(
        "/:ChallengeName",
        pOwner->getMissionDescription()->getDisplayName() );
}

void FlashListGMEChallenges::sort( int32_t sortMode ) {
    // FUN_00789680
    SortIGEFileList( &gIGEFileRequest, sortMode, bInverted );
    reset();
    selectedLineChanged();
}

void FlashListGMEChallenges::update( const int32_t index,
                                     const int32_t lineIndex ) {
    // FUN_007896d0
    if ( index >= getListLength() ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    IGEFileEntry *pEntry = gIGEFileRequest.getSortedFile( index );
    char variable[0x80];
    snprintf( variable, sizeof( variable ), "%s/line%d:%s", movieName.c_str(),
              lineIndex + 1, "name" );
    pMovie->setVariableValue( variable, pEntry->pName );

    char date[0x20] = {};
    FormatSaveDate( date, sizeof( date ), pEntry->Year, pEntry->Month,
                    pEntry->Day );
    snprintf( variable, sizeof( variable ), "%s/line%d:%s", movieName.c_str(),
              lineIndex + 1, "date" );
    pMovie->setVariableValueNonLinked( variable, date );
    gpScenaric->setTestedLevel( pEntry->bTested );

    const char *pGameModeName = nullptr;
    if ( pEntry->GameType < 8 ) {
        pGameModeName = GMInstantChallenge::GetGameModeName( pEntry->GameType );
    }
    if ( pGameModeName == nullptr ) {
        pGameModeName = GMInstantChallenge::GetGameModeGSName( 0 );
    }
    snprintf( variable, sizeof( variable ), "%s/line%d/%s", movieName.c_str(),
              lineIndex + 1, "text/gm_ic" );
    pMovie->showFrame( pGameModeName, variable, false );
}

void FlashListGMEChallenges::selectedLineChanged() {
    // FUN_00789990
    selectChallenge( getSelectedIndex() );
}

void FlashListGMEChallenges::selectChallenge( const int32_t index ) {
    // FUN_00789830
    FlashPlayer *pPlayer = pOwner->getEditorPlayer();
    if ( index >= gIGEFileRequest.NumSortedFiles ) {
        pPlayer->setVariableValue( "/:WhatGMType", "solo" );
        pPlayer->setVariableValue( "/:WhatGM", "empty" );
        pPlayer->setVariableValue( "/:ChallengeName", "" );
        pPlayer->setVariableValue( "/:ChallengeDescription", "" );
        return;
    }

    IGEFileEntry *pEntry = gIGEFileRequest.getSortedFile( index );
    if ( pPlayer != nullptr ) {
        const char *pGameModeName =
            GMInstantChallenge::GetGameModeName( pEntry->GameType );
        if ( pGameModeName != nullptr ) {
            pPlayer->setVariableValue( "/:WhatGMType", "solo" );
            pPlayer->setVariableValue( "/:WhatGM", pGameModeName );
        }
        pPlayer->setVariableValue( "/:ChallengeName", pEntry->pName );
        pPlayer->setVariableValue( "/:ChallengeDescription",
                                   pEntry->pDescription );
        gpScenaric->setTestedLevel( pEntry->bTested );
    }
    GMInstantChallenge::SetSelectedFileName( pEntry->pFileName );
}
