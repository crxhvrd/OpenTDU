#include "shared.h"
#include "camera_road_follow.h"

#include "ai/ai_pool_traffic.h"
#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/mng_world.h"

static constexpr float kDefaultMinDistance = 10.0f;   // DAT_00f4166c
static constexpr float kDefaultMaxDistance = 2000.0f; // DAT_00f419a8
static constexpr float kDefaultMaxSpeed = 1000.0f;    // DAT_00f41718
static constexpr float kDefaultMinPitch = 1.727876f;  // DAT_00f41c84
static constexpr float kDefaultMaxPitch = 3.110177f;  // DAT_00f41c80
static constexpr float kDefaultDistance = 25.0f;      // DAT_00f41834
static constexpr float kDefaultPitch = 2.670354f;     // DAT_00f41c7c
static constexpr uint32_t kRandomSeed = 0x3039;
static constexpr float kMouseDeadZone = 0.01f; // DAT_00fab37c
static constexpr float kDecreaseFactor = 0.9f; // DAT_00f416a0
static constexpr float kIncreaseFactor = 1.1f; // DAT_00f4174c
static constexpr float kMinSpeed = 1.0f;       // DAT_00f4165c
static constexpr float kCurveLengthStep = 0.01f;
static constexpr float kStraightAngle = 180.0f;       // DAT_00f417fc
static constexpr float kFullTurn = 360.0f;            // DAT_00f41800
static constexpr float kUTurnHandleLength = 10.0f;    // DAT_00f4166c
static constexpr float kTransitionHandleRatio = 0.5f; // DAT_00f41674

static inline bool IsChannelHeld( Device *pDevice, const char *pName ) {
    InputChannel *pChannel = pDevice->getChannel( pName );
    return pChannel != nullptr && ( pChannel->Flags >> 4 & 1 ) != 0;
}

static inline InputChannel *GetMouseButton( Device *pMouse,
                                            const int32_t buttonIndex ) {
    int32_t channelIndex = pMouse->getChannelIndex(
        static_cast<int32_t>( eInputChannelType::ICT_Digital ), buttonIndex );
    if ( channelIndex < 0 ) {
        return nullptr;
    }
    return &pMouse->getChannels()[channelIndex];
}

static inline bool IsMouseButtonHeld( Device *pMouse,
                                      const int32_t buttonIndex ) {
    InputChannel *pChannel = GetMouseButton( pMouse, buttonIndex );
    return pChannel != nullptr && ( pChannel->Flags >> 4 & 1 ) != 0;
}

static inline float ComputeRelativeRoadAngle( const float angle,
                                              const float referenceAngle ) {
    float relativeAngle = std::fmod( angle - referenceAngle, kFullTurn );
    while ( relativeAngle < 0.0f ) {
        relativeAngle += kFullTurn;
    }
    return relativeAngle;
}

CameraRoadFollow::CameraRoadFollow()
    : CameraGame(),
      bInTransition( false ),
      roadIt(),
      progress( 0.0f ),
      transitionCurveType( 0 ),
      transitionCurve(),
      speed( 0.0f ),
      turnMode( eRoadFollowTurn::RFT_Random ),
      bReverseRequested( false ),
      minDistance( kDefaultMinDistance ),
      maxDistance( kDefaultMaxDistance ),
      maxSpeed( kDefaultMaxSpeed ),
      minPitch( kDefaultMinPitch ),
      maxPitch( kDefaultMaxPitch ),
      distance( kDefaultDistance ),
      pitch( kDefaultPitch ),
      roadPosition( Eigen::Vector4f::Zero() ),
      roadDirection( Eigen::Vector4f::Zero() ),
      roadSide( Eigen::Vector4f::Zero() ),
      bStreamingDisabledBefore( false ) {
    // FUN_009caa10
    random.setSeed( kRandomSeed );
}

CameraRoadFollow::~CameraRoadFollow() {}

void CameraRoadFollow::initialize() {
    // FUN_009cb610
    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pCarState = localPlayer.getCarState();
    if ( pCarState == nullptr ) {
        frame.row( 3 ) = localPlayer.getLastPosition().transpose();
    } else {
        frame = pCarState->getWorldMatrix();
    }

    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoadExhaustive( frame, true );
    AiRoadShape *pShape = pRoad->getShape();
    AiRoadIt it;
    ProjectOnRoadShape( frame, it, nullptr, false, pShape );
    setRoadPosition( it );

    speed = 0.0f;
    turnMode = eRoadFollowTurn::RFT_Random;
    bReverseRequested = false;
    tick( 0.0f, true, true );

    if ( gpMngWorld == nullptr ) {
        bStreamingDisabledBefore = true;
        return;
    }

    bStreamingDisabledBefore = gpMngWorld->getDisableStreamingOnCar();
    gpMngWorld->setDisableStreamingOnCar( true );
}

bool CameraRoadFollow::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_009cb460
    if ( roadIt.pRoad == nullptr ) {
        return false;
    }

    updateFromInput();
    advance( deltaTime );

    if ( Vector4DCross( roadSide, roadDirection ).y() < 0.0f ) {
        roadSide *= -1.0f;
    }

    Eigen::Vector4f pitchAxis = ToDirection( roadSide );
    Matrix4x4f_t rotation;
    MatrixRotateZ( rotation, pitchAxis, pitch );

    const Eigen::Vector4f target = roadPosition;
    setPosition( TDUMat4x4MulVec( rotation, roadDirection ) * distance +
                 roadPosition );
    setTarget( target );

    Eigen::Vector4f streamingPosition = target;
    gpWorld->setStreamingPosition( &streamingPosition );

    updateMatrix( worldPosition.head<3>(), kWorldUpVector.head<3>(),
                  worldTarget.head<3>() );

    gpCamera->setFarPlane( farPlane );
    gpCamera->setNearPlane( nearPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    return false;
}

void CameraRoadFollow::destroy() {
    // FUN_009ca680
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( bStreamingDisabledBefore );
    }
}

void CameraRoadFollow::updateFromInput() {
    // FUN_009ca6b0
    Device *pMouse = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );
    Device *pKeyboard = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );

    bool bCloser = false;
    bool bFarther = false;
    if ( pKeyboard != nullptr ) {
        bCloser = IsChannelHeld( pKeyboard, "PAD_PLUS" );
        bFarther = IsChannelHeld( pKeyboard, "PAD_MINUS" );
    }

    bool bLeftButton = false;
    bool bRightButton = false;
    bool bMiddleButtonPressed = false;
    float pitchDelta = 0.0f;
    float speedDelta = 0.0f;
    if ( pMouse != nullptr ) {
        bLeftButton = IsMouseButtonHeld( pMouse, 0 );
        bRightButton = IsMouseButtonHeld( pMouse, 1 );
        bMiddleButtonPressed =
            IsChannelJustPressed( GetMouseButton( pMouse, 2 ) );

        // Retail reads analog channels 2 (Y) and 4 (wheel).
        float mouseY = -pMouse->getChannelValue( "UP" );
        if ( kMouseDeadZone <= std::abs( mouseY ) ) {
            pitchDelta = mouseY;
        }
        speedDelta =
            static_cast<float>( IsChannelHeld( pMouse, "WHEEL_UP" ) ) -
            static_cast<float>( IsChannelHeld( pMouse, "WHEEL_DOWN" ) );
    }

    pitch += pitchDelta;
    if ( maxPitch < pitch ) {
        pitch = maxPitch;
    }
    if ( pitch < minPitch ) {
        pitch = minPitch;
    }

    if ( bCloser ) {
        distance *= kDecreaseFactor;
    } else if ( bFarther ) {
        distance *= kIncreaseFactor;
    }
    if ( distance < minDistance ) {
        distance = minDistance;
    }
    if ( maxDistance < distance ) {
        distance = maxDistance;
    }

    if ( 0.0f < speedDelta ) {
        speed = ( speed < kMinSpeed ) ? kMinSpeed : speed * kIncreaseFactor;
    } else if ( speedDelta < 0.0f ) {
        speed = ( speed <= kMinSpeed ) ? 0.0f : speed * kDecreaseFactor;
    }
    if ( maxSpeed < speed ) {
        speed = maxSpeed;
    }
    if ( speed < 0.0f ) {
        speed = 0.0f;
    }

    if ( bLeftButton ) {
        turnMode = bRightButton ? eRoadFollowTurn::RFT_Straight
                                : eRoadFollowTurn::RFT_LargestAngle;
    } else {
        turnMode = bRightButton ? eRoadFollowTurn::RFT_SmallestAngle
                                : eRoadFollowTurn::RFT_Random;
    }

    if ( bMiddleButtonPressed ) {
        bReverseRequested = true;
    }
}

void CameraRoadFollow::advance( const float deltaTime ) {
    // FUN_009cb160
    AiRoadShape *pShape = roadIt.pRoad->getShape();
    if ( pShape == nullptr ) {
        return;
    }

    if ( !bInTransition ) {
        if ( bReverseRequested ) {
            bReverseRequested = false;
            roadIt.flip();
        }

        const float step = ( speed * deltaTime ) / pShape->TotalLength;
        if ( 0.0f < step && ( ( 1.0f <= progress && roadIt.bWay ) ||
                              ( progress <= 0.0f && !roadIt.bWay ) ) ) {
            pickNextRoad();
            return;
        }

        const bool bForward = roadIt.bWay;
        if ( bForward ) {
            progress = std::min( progress + step, 1.0f );
        } else {
            progress = std::max( progress - step, 0.0f );
        }

        pShape->computeIt( progress, roadIt );
        if ( !bForward ) {
            roadIt.flip();
        }

        roadPosition = roadIt.computeOffsetPosition( 0.0f, 0.0f );
        roadDirection = NormalizeVector4( roadIt.computeDirection() );
    } else {
        const float curveStep =
            ( speed * deltaTime ) /
            transitionCurve.computeLength( kCurveLengthStep );
        if ( 0.0f < curveStep && 1.0f <= progress ) {
            progress = pShape->computeRoadFraction( roadIt );
            bInTransition = false;
            return;
        }

        progress = std::min( progress + curveStep, 1.0f );

        if ( transitionCurveType == 0 ) {
            QuadraticBezier quadratic;
            quadratic.ControlPoints[0] = transitionCurve.ControlPoints[0];
            quadratic.ControlPoints[1] = transitionCurve.ControlPoints[1];
            quadratic.ControlPoints[2] = transitionCurve.ControlPoints[2];
            roadPosition = quadratic.evaluate( progress );
        } else {
            roadPosition = transitionCurve.evaluate( progress );
        }
        roadPosition.w() = 0.0f;

        roadDirection =
            NormalizeVector4( transitionCurve.evaluateTangent( progress ) );
    }

    roadSide = NormalizeVector4(
        Eigen::Vector4f( roadDirection.z(), 0.0f, -roadDirection.x(), 0.0f ) );
}

void CameraRoadFollow::pickNextRoad() {
    // FUN_009caae0
    eRoadFollowTurn turn = turnMode;
    bInTransition = true;
    if ( turn == eRoadFollowTurn::RFT_Random ) {
        switch ( RandomRange( random, 0, 2 ) ) {
        case 0:
            turn = eRoadFollowTurn::RFT_LargestAngle;
            break;
        case 1:
            turn = eRoadFollowTurn::RFT_Straight;
            break;
        default:
            turn = eRoadFollowTurn::RFT_SmallestAngle;
            break;
        }
    }

    AiRoadMap::Junction *pJunction =
        roadIt.bWay ? roadIt.pRoad->pJunctionEnd : roadIt.pRoad->pJunctionBegin;
    if ( pJunction != nullptr ) {
        const float currentAngle = pJunction->computeRoadAngle(
            pJunction->getRoadIndex( roadIt.pRoad ) );

        int32_t bestIndex = -1;
        float bestScore = 0.0f;
        for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
            const float relativeAngle = ComputeRelativeRoadAngle(
                pJunction->computeRoadAngle( i ), currentAngle );
            if ( relativeAngle == 0.0f ) {
                continue;
            }

            float score = relativeAngle;
            bool bBetter = false;
            switch ( turn ) {
            case eRoadFollowTurn::RFT_SmallestAngle:
                bBetter = bestIndex == -1 || score < bestScore;
                break;
            case eRoadFollowTurn::RFT_LargestAngle:
                bBetter = bestIndex == -1 || bestScore < score;
                break;
            case eRoadFollowTurn::RFT_Straight:
                score = std::abs( kStraightAngle - relativeAngle );
                bBetter = bestIndex == -1 || score < bestScore;
                break;
            default:
                break;
            }

            if ( bBetter ) {
                bestIndex = i;
                bestScore = score;
            }
        }

        if ( bestIndex != -1 ) {
            AiRoadMap::Road *pNextRoad = pJunction->getRoad( bestIndex );
            AiRoadIt nextIt;
            if ( pNextRoad->pJunctionBegin == pJunction ) {
                nextIt.setRoadEntry( pNextRoad, true );
            } else if ( pNextRoad->pJunctionEnd == pJunction ) {
                nextIt.setRoadEntry( pNextRoad, false );
            }

            const Eigen::Vector4f currentPosition =
                roadIt.computeOffsetPosition( 0.0f, 0.0f );
            const Eigen::Vector4f nextPosition =
                nextIt.computeOffsetPosition( 0.0f, 0.0f );
            const float handleLength =
                ( nextPosition - currentPosition ).norm();
            const Eigen::Vector4f currentDirection = roadIt.computeDirection();
            const Eigen::Vector4f nextDirection = nextIt.computeDirection();

            // FUN_0040a5c0
            transitionCurve.ControlPoints[0] = currentPosition;
            transitionCurve.ControlPoints[1] =
                currentPosition + currentDirection / currentDirection.norm() *
                                      ( handleLength * kTransitionHandleRatio );
            transitionCurve.ControlPoints[2] =
                nextPosition +
                nextDirection * ( handleLength * -kTransitionHandleRatio );
            transitionCurve.ControlPoints[3] = nextPosition;
            transitionCurveType = 1;

            roadIt = nextIt;
            progress = 0.0f;
            return;
        }
    }

    // Dead end (or no other road at the junction): U-turn
    roadIt.bWay = !roadIt.bWay;
    roadIt.T = 0.0f;

    const Eigen::Vector4f position = roadIt.computeOffsetPosition( 0.0f, 0.0f );
    const Eigen::Vector4f direction = roadIt.computeDirection();
    const Eigen::Vector4f side = roadIt.computeSideDirection();

    // FUN_0040a5c0
    transitionCurve.ControlPoints[0] = position;
    transitionCurve.ControlPoints[1] =
        ( -direction - side ) * kUTurnHandleLength + position;
    transitionCurve.ControlPoints[2] =
        ( -direction + side ) * kUTurnHandleLength + position;
    transitionCurve.ControlPoints[3] = position;
    transitionCurveType = 1;

    progress = 0.0f;
}

void CameraRoadFollow::setRoadPosition( const AiRoadIt &it ) {
    // FUN_009cb400
    roadIt = it;
    AiRoadShape *pShape = roadIt.pRoad->getShape();
    if ( pShape != nullptr ) {
        progress = pShape->computeRoadFraction( roadIt );
        bInTransition = false;
    }
}
