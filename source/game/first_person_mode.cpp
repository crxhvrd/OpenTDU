#include "shared.h"
#include "first_person_mode.h"

#include "flash/flash_mouse.h"
#include "flash/gs_flash.h"
#include "game/gm_scene.h"
#include "game/scene_interior.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "physics/gs_physics.h"
#include "render/camera/camera_first_person.h"
#include "render/camera/gs_camera.h"
#include "render/hierarchy_geometry.h"
#include "system/ed_sys.h"

// TODO: Should be an option in a new panel
// (e.g. Interior Navigation: [] TDU1 [x] TDU2)
static constexpr const char *kToggleChannel = "TAB";

static constexpr float kCapsuleRadius = 0.4f;
static constexpr float kCapsuleCylinderHeight = 1.0f;

// TODO: FIXME: Currently using heuristics to find a suitable spawn location for
// the player. In the future we should add a new entity type to manually define
// the spawn point in interiors instead (once this is done we can scrap this).
static constexpr float kSpawnProbeHeight = 2.0f;
static constexpr float kSpawnFallbackHeight = 1.0f;
static constexpr float kSpawnGroundClearance = 0.05f;
static constexpr float kMaxSpawnDrop = 6.0f;
static constexpr float kMinFloorNormalY = 0.7f;
static constexpr int32_t kSpawnSettleSteps = 12;
static constexpr float kSpawnSettleStep = 1.0f / 60.0f;
static constexpr float kSpawnSettleMaxDrop = 1.0f;
static constexpr int32_t kMobilitySteps = 10;
static constexpr float kMobilityTestSpeed = 2.4f;
static constexpr float kOpenSpaceGoodEnough = 1.2f;

FirstPersonMode::FirstPersonMode()
    : pCamera( nullptr ),
      pCharacter( nullptr ),
      pPreviousCamera( nullptr ),
      pLoopCinematic( nullptr ),
      pCollisionKey( nullptr ),
      pCollisionBody( nullptr ),
      pointer(),
      spawnPosition( 0.0f, 0.0f, 0.0f, 1.0f ),
      hullMin( 0.0f, 0.0f, 0.0f, 1.0f ),
      hullMax( 0.0f, 0.0f, 0.0f, 1.0f ),
      hullNumTriangles( 0 ),
      pSpawnSource( "<none>" ),
      bActive( false ),
      bSavedFlashCursorVisible( false ) {}

FirstPersonMode::~FirstPersonMode() {
    leave();

    if ( pCamera != nullptr ) {
        delete pCamera;
        pCamera = nullptr;
    }
}

bool FirstPersonMode::installCollision( SceneInterior *pScene ) {
    if ( gpPhysics == nullptr || pScene == nullptr ) {
        return false;
    }

    HiearchyNode *pRootNode = pScene->getRootNode();
    if ( pRootNode == nullptr ) {
        return false;
    }

    // Generate collision hull solely based on level geometry. This is okay-ish
    // since the game is old and the polycount is not that high. Ideally in the
    // future it should use hand authored colliders to reduce overhead.
    HierarchyTriangleSoup soup;
    if ( !GatherHierarchyTriangles( pRootNode, soup ) ) {
        return false;
    }

    const Eigen::Vector3f shiftOrigin = GSPhysics::GetShiftOrigin().head<3>();
    for ( Eigen::Vector3f &vertex : soup.Vertices ) {
        vertex += shiftOrigin;
    }

    hullMin = Eigen::Vector4f( soup.Vertices[0].x(), soup.Vertices[0].y(),
                               soup.Vertices[0].z(), 1.0f );
    hullMax = hullMin;
    for ( const Eigen::Vector3f &vertex : soup.Vertices ) {
        hullMin.head<3>() = hullMin.head<3>().cwiseMin( vertex );
        hullMax.head<3>() = hullMax.head<3>().cwiseMax( vertex );
    }
    hullNumTriangles = (uint32_t)( soup.Indices.size() / 3 );
    pCollisionKey = pScene;
    pCollisionBody = gpPhysics->addStaticMesh(
        pCollisionKey, soup.Vertices.data(), (uint32_t)soup.Vertices.size(),
        soup.Indices.data(), (uint32_t)soup.Indices.size(),
        TDUSurface::Generic );
    if ( pCollisionBody == nullptr ) {
        pCollisionKey = nullptr;
        return false;
    }
    return true;
}

float FirstPersonMode::measureOpenSpace( const Eigen::Vector4f &from ) {
    if ( pCharacter == nullptr || gpPhysics == nullptr ) {
        return 0.0f;
    }

    static const Eigen::Vector3f kDirections[4] = {
        Eigen::Vector3f( 1.0f, 0.0f, 0.0f ),
        Eigen::Vector3f( -1.0f, 0.0f, 0.0f ),
        Eigen::Vector3f( 0.0f, 0.0f, 1.0f ),
        Eigen::Vector3f( 0.0f, 0.0f, -1.0f ) };

    float totalDistance = 0.0f;
    for ( const Eigen::Vector3f &direction : kDirections ) {
        gpPhysics->setCharacterPosition( pCharacter, from );
        for ( int32_t step = 0; step < kMobilitySteps; step++ ) {
            gpPhysics->updateCharacter(
                pCharacter, direction * kMobilityTestSpeed, kSpawnSettleStep );
        }

        const Eigen::Vector4f moved =
            gpPhysics->getCharacterPosition( pCharacter );
        totalDistance += ( moved.head<3>() - from.head<3>() ).norm();
    }

    gpPhysics->setCharacterPosition( pCharacter, from );
    return totalDistance;
}

void FirstPersonMode::removeCollision() {
    if ( pCollisionKey != nullptr && gpPhysics != nullptr ) {
        gpPhysics->removeStaticMesh( pCollisionKey );
    }
    pCollisionKey = nullptr;
    pCollisionBody = nullptr;
}

void FirstPersonMode::enter( SceneInterior *pScene,
                             EntityCinematic *pLoopCine ) {
    if ( bActive || pScene == nullptr || gpCamera == nullptr ) {
        return;
    }

    const bool bHasCollision = installCollision( pScene );

    CameraGame *pRenderCamera = gpCamera->getRenderCamera();
    CameraGame *pBoundCamera = gpCamera->getActiveCamera();

    const Eigen::Vector4f interiorPosition = pScene->getWorldPosition();

    Eigen::Vector3f viewDirection( 0.0f, 0.0f, -1.0f );
    Eigen::Vector4f cameraPosition = Eigen::Vector4f::Zero();
    bool bHasCameraPosition = false;
    if ( pRenderCamera != nullptr ) {
        const Eigen::Vector3f position = pRenderCamera->getWorldPosition();
        if ( position.squaredNorm() > 1.0f ) {
            cameraPosition = Eigen::Vector4f( position.x(), position.y(),
                                              position.z(), 1.0f );
            bHasCameraPosition = true;
        }
        viewDirection = pRenderCamera->getTarget() - position;
    }
    pLoopCinematic = pLoopCine;
    if ( pLoopCinematic != nullptr && pLoopCinematic->isPlaying() ) {
        pLoopCinematic->stop( true );
    }
    gpCamera->clearCinematicCameraOverride();

#if 1
    // TODO: REMOVEME: Crappy approach to find a spawn point in the scene
    // (solely heuristic based). Should use a hand placed dummy instead.
    Eigen::Vector4f candidates[16];
    const char *candidateNames[16];
    int32_t numCandidates = 0;

    auto addCandidate = [&]( const char *pName,
                             const Eigen::Vector4f &position ) {
        if ( numCandidates < 16 ) {
            candidates[numCandidates] = position;
            candidateNames[numCandidates] = pName;
            numCandidates++;
        }
    };

    if ( bHasCameraPosition ) {
        Eigen::Vector3f flatForward( viewDirection.x(), 0.0f,
                                     viewDirection.z() );
        if ( flatForward.squaredNorm() > 1.0e-6f ) {
            flatForward.normalize();
            for ( const float distance : { 3.0f, 1.5f } ) {
                Eigen::Vector4f position = cameraPosition;
                position.head<3>() += flatForward * distance;
                addCandidate( "ahead of the camera", position );
            }
        }
        addCandidate( "render camera", cameraPosition );
    }

    if ( bHasCollision ) {
        Eigen::Vector4f hullCentre = ( hullMin + hullMax ) * 0.5f;
        hullCentre.w() = 1.0f;
        addCandidate( "hull centre", hullCentre );

        const float spreadX = ( hullMax.x() - hullMin.x() ) * 0.25f;
        const float spreadZ = ( hullMax.z() - hullMin.z() ) * 0.25f;
        static const float kOffsets[4][2] = {
            {1.0f,  0.0f },
            {-1.0f, 0.0f },
            {0.0f,  1.0f },
            {0.0f,  -1.0f}
        };
        for ( const float( &offset )[2] : kOffsets ) {
            Eigen::Vector4f position = hullCentre;
            position.x() += offset[0] * spreadX;
            position.z() += offset[1] * spreadZ;
            addCandidate( "hull ring", position );
        }
    }

    Eigen::Vector4f originProbe = interiorPosition;
    originProbe.y() += kSpawnProbeHeight;
    addCandidate( "interior origin", originProbe );

    if ( pCamera == nullptr ) {
        pCamera = new CameraFirstPerson();
    }

    spawnPosition = originProbe;
    spawnPosition.y() = interiorPosition.y() + kSpawnFallbackHeight;
    pSpawnSource = "interior origin (nothing validated)";
    float bestOpenSpace = 0.0f;

    if ( bHasCollision && gpPhysics != nullptr ) {
        pCharacter = gpPhysics->createCharacter( spawnPosition, kCapsuleRadius,
                                                 kCapsuleCylinderHeight );
        gpPhysics->setCharacterCollisionRestriction( pCharacter,
                                                     pCollisionBody );
    }

    for ( int32_t i = 0; i < numCandidates && pCharacter != nullptr; i++ ) {
        float floorHeight = 0.0f;
        Eigen::Vector4f floorNormal = Eigen::Vector4f::Zero();
        if ( !gpPhysics->probeSurface( candidates[i], false, floorHeight,
                                       floorNormal ) ) {
            OTDU_LOG_DEBUG( "First person: candidate '%s' (%.1f %.1f %.1f) "
                            "-> no floor below\n",
                            candidateNames[i], candidates[i].x(),
                            candidates[i].y(), candidates[i].z() );
            continue;
        }

        const bool bUpFacing = floorNormal.y() > kMinFloorNormalY;
        const bool bInReach = floorHeight > candidates[i].y() - kMaxSpawnDrop;
        if ( !bUpFacing || !bInReach ) {
            OTDU_LOG_DEBUG( "First person: candidate '%s' (%.1f %.1f %.1f) -> "
                            "rejected (floor %.1f, normal %.2f %.2f %.2f)\n",
                            candidateNames[i], candidates[i].x(),
                            candidates[i].y(), candidates[i].z(), floorHeight,
                            floorNormal.x(), floorNormal.y(), floorNormal.z() );
            continue;
        }

        Eigen::Vector4f candidateSpawn = candidates[i];
        candidateSpawn.y() = floorHeight + kSpawnGroundClearance;

        gpPhysics->setCharacterPosition( pCharacter, candidateSpawn );

        for ( int32_t step = 0; step < kSpawnSettleSteps; step++ ) {
            gpPhysics->updateCharacter( pCharacter, Eigen::Vector3f::Zero(),
                                        kSpawnSettleStep );
        }

        const bool bSupported = gpPhysics->isCharacterOnGround( pCharacter );
        const Eigen::Vector4f settled =
            gpPhysics->getCharacterPosition( pCharacter );
        const bool bStayed =
            settled.y() > candidateSpawn.y() - kSpawnSettleMaxDrop;

        const float openSpace =
            ( bSupported && bStayed ) ? measureOpenSpace( settled ) : 0.0f;

        if ( openSpace > bestOpenSpace ) {
            bestOpenSpace = openSpace;
            spawnPosition = settled;
            pSpawnSource = candidateNames[i];
            if ( bestOpenSpace >= kOpenSpaceGoodEnough ) {
                break;
            }
        }
    }
#endif

    if ( pCharacter != nullptr ) {
        gpPhysics->setCharacterPosition( pCharacter, spawnPosition );
    }

    pCamera->setCharacter( pCharacter );
    pCamera->setSpawnPosition( spawnPosition );
    pCamera->setViewDirection( viewDirection );

    Eigen::Vector4f eyePosition = spawnPosition;
    eyePosition.y() += pCamera->getEyeHeight();
    pCamera->setPosition( eyePosition );

    pPreviousCamera = pBoundCamera;
    gpCamera->bindCamera( pCamera, nullptr, 0.0f );

    SetHUDMoviesPaused( true );
    gEdSys.setRelativeMouseMode( true );

    // Hide flash mouse
    if ( gpFlash != nullptr && gpFlash->getMouse() != nullptr ) {
        bSavedFlashCursorVisible = gpFlash->getMouse()->field12_0x30;
        gpFlash->getMouse()->field12_0x30 = true;
    }

    // Set first person pointer in charge
    pointer.initialize();

    bActive = true;
}

void FirstPersonMode::leave() {
    if ( !bActive ) {
        return;
    }

    bActive = false;

    gEdSys.setRelativeMouseMode( false );
    SetHUDMoviesPaused( false );

    if ( gpFlash != nullptr && gpFlash->getMouse() != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = bSavedFlashCursorVisible;
    }

    if ( gpCamera != nullptr && pPreviousCamera != nullptr ) {
        gpCamera->bindCamera( pPreviousCamera, nullptr, 0.0f );
    }
    pPreviousCamera = nullptr;

    if ( pCamera != nullptr ) {
        pCamera->setCharacter( nullptr );
    }

    if ( pCharacter != nullptr && gpPhysics != nullptr ) {
        gpPhysics->destroyCharacter( pCharacter );
    }
    pCharacter = nullptr;

    removeCollision();

    // Clearing the ended flag lets GameModeScene::launchCine pick the loop
    // cinematic back up on its next tick.
    if ( pLoopCinematic != nullptr ) {
        pLoopCinematic->clearEnded();
    }
    pLoopCinematic = nullptr;

    OTDU_LOG_INFO( "First person: left\n" );
}

void FirstPersonMode::tick( SceneInterior *pScene,
                            EntityCinematic *pLoopCine ) {
    Device *pKeyboard = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );
    const bool bTogglePressed =
        pKeyboard != nullptr &&
        IsChannelJustPressed( pKeyboard->getChannel( kToggleChannel ) );

    const bool bInputCaptured = IsFirstPersonInputCaptured();
    if ( bActive ) {
        gEdSys.setRelativeMouseMode( !bInputCaptured );
        if ( !bInputCaptured ) {
            pCamera->updateLook();
            pointer.draw();
        }
    }

    if ( bInputCaptured ) {
        return;
    }

    if ( !bTogglePressed ) {
        return;
    }

    if ( bActive ) {
        leave();
        return;
    }

    if ( pScene == nullptr || pScene->getState() != SceneInterior::SIS_Live ) {
        return;
    }

    enter( pScene, pLoopCine );
}
