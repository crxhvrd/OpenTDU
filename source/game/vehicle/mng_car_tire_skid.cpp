#include "shared.h"
#include "mng_car_tire_skid.h"

#include "render/2dm.h"
#include "render/gs_render.h"
#include "render/render_scene.h"

#include "world/gs_mng_resources.h"

#include "game/vehicle/gs_car_state.h"

#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"

#include "core/maths_helper.h"

MngCarTireSkid *gpMngCarTireSkid = nullptr; // DAT_010e7ef8

constexpr float kOffRoadMinOmega = 0.1f;             // FLOAT_00f41668
constexpr float kOffRoadMinSpeedLongitudinal = 1.0f; // FLOAT_00f4165c
constexpr float kOffRoadMinSlipAngle = 1.99f;        // DAT_00f41b8c
constexpr float kOffRoadMinSlipRatio = 0.2f;         // DAT_00f41730
constexpr float kOnRoadAlphaScale = 150.0f;          // DAT_00f4182c
constexpr float kOffRoadAlphaScale = -160.0f;        // DAT_00f41b88
constexpr int32_t kOffRoadAlphaBase = 0x28;
constexpr float kBikeScrubMinAngle = 0.1f; // DAT_00faba14
constexpr float kBikeScrubMaxAngle = 0.2f; // DAT_00faba10

// Quad construction.
constexpr float kSkidGroundOffset = 0.02f; // DAT_00f417f0
constexpr float kMinTravelSpeedSq = 0.01f; // DAT_00f416d4
constexpr float kHalfWidthIn = -0.5f;      // DAT_00f4186c
constexpr float kHalfWidthOut = 0.5f;      // FLOAT_00f41674

constexpr float kMaxPointGap = 2.0f;      // DAT_00f416b0
constexpr float kMinPointGap = 0.25f;     // DAT_00f416dc
constexpr float kCurvatureCoeff = -0.75f; // DAT_00f41b84
constexpr float kCurvatureScale = 4.0f;   // DAT_00f418d4

constexpr float kEpsilon = 1.0e-8f;

inline Eigen::Vector4f SkidPointCenter( const SkidPoint &point ) {
    return ( point.In + point.Out ) * 0.5f;
}

inline float GetBikeScrubAngle( const CarState & ) {
    OTDU_UNIMPLEMENTED; // TODO: BIKE (edPhysVehicle+0xe7c)
    return 0.0f;
}

static inline void EndSkidTrail( TireSkid &skid ) {
    // FUN_007140c0 (inlined)
    if ( skid.pLastAdded == nullptr ) {
        return;
    }
    if ( !skid.pLastAdded->bLast ) {
        skid.pLastAdded->bLast = true;
        skid.NumLastPoints++;
    }
    skid.pLastAdded->Color &= 0x00ffffff;
    skid.pLastAdded = nullptr;
    skid.pLastLastAdded = nullptr;
}

static inline void ShiftSkid( TireSkid &skid, const Eigen::Vector4f &offset ) {
    // FUN_00713f90
    if ( skid.NumPoints == 0 || skid.StartIndex == skid.EndIndex ) {
        return;
    }

    int32_t idx = skid.StartIndex;
    do {
        SkidPoint &point = skid.pArrayPoints[idx];
        point.In -= offset;
        point.Out -= offset;
        idx++;
        if ( idx >= skid.NumPointsMax ) {
            idx = 0;
        }
    } while ( idx != skid.EndIndex );
}

MngCarTireSkid::MngCarTireSkid()
    : Manager(),
      StreamingCallback(),
      bEnabled( false ),
      pCarSkids( nullptr ),
      pMaterial( nullptr ),
      renderList(),
      groundRed( 0x50 ),
      groundGreen( 0x3c ),
      groundBlue( 0x3c ),
      minOmega( 0.1f ),             // FLOAT_00f41668
      minSpeedLongitudinal( 1.0f ), // FLOAT_00f4165c
      minSlipAngle( 1.99f ),        // DAT_00f41b8c
      minSlipRatio( 3.5f ),         // DAT_00f41b4c
      minSleepLength( 1.0f ),       // FLOAT_00f4165c
      maxSlipLength( 5.0f ),        // DAT_00f41750
      minWheelLoad( 100.0f ),       // DAT_00f41708
      maxWheelLoad( 1000.0f ),      // DAT_00f41718
      pPositionCursor( nullptr ),
      pColorCursor( nullptr ),
      pUV0Cursor( nullptr ),
      pUV1Cursor( nullptr ),
      pIndexCursor( nullptr ),
      currentVertexIndex( 0 ) {
    // FUN_00714b50
    gpMngCarTireSkid = this;
}

MngCarTireSkid::~MngCarTireSkid() {
    // FUN_00714af0
    gpMngCarTireSkid = nullptr;
    if ( renderList.isInitialized() ) {
        renderList.destroy();
    }
}

bool MngCarTireSkid::initialize( TestDriveGameInstance * ) {
    // FUN_00714e90
    bEnabled = false;

    TireSkidResources &resources = gpMngResources->getTireSkidResources();
    if ( !resources.isLoaded() ) {
        return false;
    }

    pMaterial = resources.getRender2DM().getMaterialAtIndex( 0 );
    if ( pMaterial == nullptr ) {
        return false;
    }

    // One CarTireSkid per tracked car, each owning one TireSkid per wheel, each
    // TireSkid owning a ring buffer of kMaxPointsPerSkid samples.
    pCarSkids = new CarTireSkid[kMaxCars];
    for ( int32_t car = 0; car < kMaxCars; car++ ) {
        CarTireSkid &carSkid = pCarSkids[car];
        carSkid.NumSkids = kWheelsPerCar;
        carSkid.pSkids = new TireSkid[kWheelsPerCar];
        for ( int32_t wheel = 0; wheel < kWheelsPerCar; wheel++ ) {
            TireSkid &skid = carSkid.pSkids[wheel];
            skid.NumPoints = 0;
            skid.NumLastPoints = 0;
            skid.NumPointsMax = kMaxPointsPerSkid;
            skid.StartIndex = 0;
            skid.EndIndex = 0;
            skid.pArrayPoints = new SkidPoint[kMaxPointsPerSkid]();
            skid.pLastAdded = nullptr;
            skid.pLastLastAdded = nullptr;
            skid.Current.In.setZero();
            skid.Current.Out.setZero();
            skid.Current.Color = 0;
            skid.Current.bLast = false;
            skid.Current.bGrounded = false;
            skid.pCarState = nullptr;
        }
    }

    DrawStreams streams;
    streams.Diffuse = 1;
    streams.UVMap = 2;
    streams.bIndex16 = 1;
    if ( renderList.initialize( 1, 0x2000, 0x4000, true, &streams,
                                "CarTireSkid", true ) ) {
        bEnabled = true;
    }
    return bEnabled;
}

void MngCarTireSkid::terminate() {
    // FUN_00713f00
    bEnabled = false;

    if ( renderList.isInitialized() ) {
        renderList.destroy();
    }

    if ( pCarSkids != nullptr ) {
        for ( int32_t car = 0; car < kMaxCars; car++ ) {
            CarTireSkid &carSkid = pCarSkids[car];
            if ( carSkid.pSkids != nullptr ) {
                for ( int32_t wheel = 0; wheel < carSkid.NumSkids; wheel++ ) {
                    delete[] carSkid.pSkids[wheel].pArrayPoints;
                }
                delete[] carSkid.pSkids;
            }
        }
        delete[] pCarSkids;
        pCarSkids = nullptr;
    }

    pMaterial = nullptr;
}

void MngCarTireSkid::tick( float deltaTime, float ) {
    // FUN_00714cc0
    if ( !bEnabled ) {
        return;
    }

    if ( gpCarState != nullptr && deltaTime > 0.0f /* FLOAT_00f41660 */ ) {
        updateSkids( deltaTime );
    }

    renderList.reset();
    renderList.setActiveMaterial( pMaterial );
    renderList.setIdentityTransform();

    buildGeometry();
}

void MngCarTireSkid::updateSkids( float /*deltaTime*/ ) {
    // FUN_00714cc0
    PhysicsLock physLock;

    CarState *pCarStates = gpCarState->getCarStates();
    int32_t numTrackedCars = 0;
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        CarState &carState = pCarStates[i];

        PhysicsVehicle *pPhysicsVehicle = carState.getPhysicsVehicle();
        if ( pPhysicsVehicle == nullptr || !pPhysicsVehicle->isActive() ||
             carState.getActiveMode() == CarState::eActiveMode::CSAM_Paused ) {
            continue;
        }

        if ( !carState.FUN_00406ce0() &&
             carState.getOwner() != CarState::eOwner::CSO_NetPlayer ) {
            continue;
        }

        updateCarSkids( pCarSkids[numTrackedCars], &carState );

        numTrackedCars++;
        if ( numTrackedCars >= kMaxCars ) {
            break;
        }
    }
}

void MngCarTireSkid::updateCarSkids( CarTireSkid &carSkid,
                                     CarState *pCarState ) {
    // FUN_0042ea10
    for ( int32_t wheel = 0; wheel < carSkid.NumSkids; wheel++ ) {
        updateWheelSkid( pCarState, wheel, carSkid.pSkids[wheel] );
    }
}

void MngCarTireSkid::updateWheelSkid( CarState *pCarState,
                                      int32_t wheelIndex,
                                      TireSkid &skid ) {
    // FUN_007140c0
    if ( wheelIndex >= (int32_t)pCarState->getNumWheels() ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
    const PhysicsVehicle::WheelReadout physicsWheel =
        pPhysicsVehicle->getWheelReadout( (uint32_t)wheelIndex );
    const WheelState &wheelState = pCarState->getWheel( (uint32_t)wheelIndex );

    skid.Current.Color = 0;
    skid.Current.bGrounded = false;

    if ( skid.pCarState != pCarState ) {
        EndSkidTrail( skid );
        skid.pCarState = pCarState;
    }

    bool bSkidding = false;
    bool bAppendPoint = false;

    const float absOmega = std::fabs( wheelState.Omega );
    const float absSpeedLongitudinal =
        std::fabs( wheelState.LongitudinalSpeed );
    const float absSlipAngle = std::fabs( wheelState.SlipAngle );
    const float absSlipRatio = std::fabs( wheelState.SlipRatio );

    const float loadFactor =
        Saturate( ( physicsWheel.SuspensionLoad - minWheelLoad ) /
                  ( maxWheelLoad - minWheelLoad ) );
    const float slipFactor =
        Saturate( ( std::sqrt( wheelState.SlipAngle * wheelState.SlipAngle +
                               wheelState.SlipRatio * wheelState.SlipRatio ) -
                    minSleepLength ) /
                  ( maxSlipLength - minSleepLength ) );

    bool bClassified = false;
    if ( wheelState.bGrounded ) {
        if ( !wheelState.bOnRoad ) {
            // Dirt
            if ( ( kOffRoadMinOmega < absOmega ||
                   kOffRoadMinSpeedLongitudinal < absSpeedLongitudinal ) &&
                 ( kOffRoadMinSlipAngle < absSlipAngle ||
                   kOffRoadMinSlipRatio < absSlipRatio ) ) {
                bSkidding = true;
                bAppendPoint = true;
                bClassified = true;

                const uint32_t red = (uint32_t)(uint8_t)FloatToInt(
                    groundRed * wheelState.GroundColor.x() );
                const uint32_t green = (uint32_t)(uint8_t)FloatToInt(
                    groundGreen * wheelState.GroundColor.y() );
                const uint32_t blue = (uint32_t)(uint8_t)FloatToInt(
                    groundBlue * wheelState.GroundColor.z() );
                const uint32_t alpha =
                    (uint32_t)(uint8_t)( kOffRoadAlphaBase -
                                         FloatToInt( slipFactor * loadFactor *
                                                     kOffRoadAlphaScale ) );

                skid.Current.Color =
                    ( alpha << 24 ) | ( blue << 16 ) | ( green << 8 ) | red;
                skid.Current.bGrounded = true;
            }
        } else {
            // Asphalt
            if ( ( minOmega < absOmega ||
                   minSpeedLongitudinal < absSpeedLongitudinal ) &&
                 ( minSlipAngle < absSlipAngle ||
                   minSlipRatio < absSlipRatio ) ) {
                bSkidding = true;
                bAppendPoint = true;

                const uint32_t alpha = (uint32_t)(uint8_t)FloatToInt(
                    slipFactor * loadFactor * kOnRoadAlphaScale );
                skid.Current.Color = ( alpha << 24 );
            }

            const int32_t numWheels = (int32_t)pCarState->getNumWheels();
            const bool bRearAxle =
                numWheels >= 2 &&
                ( wheelIndex >> ( ( numWheels / 2 ) - 1 ) ) == 1;
            const float scrubAngle =
                ( pCarState->getBody()->bIsBike && bRearAxle )
                    ? std::fabs( GetBikeScrubAngle( *pCarState ) )
                    : 0.0f;
            if ( kBikeScrubMinAngle < scrubAngle ) {
                bSkidding = true;
                bAppendPoint = true;
                bClassified = true;

                float ramp = ( ( scrubAngle - kBikeScrubMinAngle ) *
                               kOnRoadAlphaScale ) /
                             ( kBikeScrubMaxAngle - kBikeScrubMinAngle );
                if ( kOnRoadAlphaScale <= ramp ) {
                    ramp = kOnRoadAlphaScale;
                } else if ( ramp <= 0.0f ) {
                    ramp = 0.0f;
                }
                skid.Current.Color =
                    ( (uint32_t)(uint8_t)FloatToInt( ramp ) << 24 );
            }
            bClassified = bClassified || bSkidding;
        }
    }

    if ( !bClassified ) {
        bAppendPoint = ( skid.pLastAdded != nullptr );
    }

    Eigen::Vector4f travelDirection = physicsWheel.ContactVelocity;
    if ( travelDirection.x() * travelDirection.x() +
             travelDirection.z() * travelDirection.z() <
         kMinTravelSpeedSq ) {
        travelDirection = physicsWheel.ForwardAxis;
    }
    travelDirection.y() = 0.0f;

    Eigen::Vector4f lateral =
        Vector4DCross( travelDirection, physicsWheel.UpAxis );
    const float lateralLength = lateral.norm();
    if ( lateralLength <= kEpsilon ) {
        return;
    }
    lateral /= lateralLength;

    Eigen::Vector4f groundPosition = wheelState.ProjectedPositionOnGround;
    groundPosition.y() += kSkidGroundOffset;

    skid.Current.In =
        groundPosition + lateral * ( physicsWheel.Width * kHalfWidthIn );
    skid.Current.Out =
        groundPosition + lateral * ( physicsWheel.Width * kHalfWidthOut );
    skid.Current.bLast = true;

    if ( skid.pLastAdded != nullptr ) {
        const Eigen::Vector4f lastCenter = SkidPointCenter( *skid.pLastAdded );
        Eigen::Vector4f step = SkidPointCenter( skid.Current ) - lastCenter;
        const float stepLength = step.norm();
        if ( stepLength > kEpsilon ) {
            step /= stepLength;
        }

        if ( stepLength <= kMaxPointGap ) {
            if ( bSkidding ) {
                Eigen::Vector4f previousStep =
                    ( skid.pLastLastAdded == nullptr )
                        ? Eigen::Vector4f( -step )
                        : ( lastCenter -
                            SkidPointCenter( *skid.pLastLastAdded ) );
                const float previousLength = previousStep.norm();
                if ( previousLength > kEpsilon ) {
                    previousStep /= previousLength;
                }

                const float alignment = step.dot( previousStep );
                float minimumStep = 1.0f - ( alignment - 1.0f ) *
                                               kCurvatureCoeff *
                                               kCurvatureScale;
                if ( minimumStep < 1.0f ) {
                    minimumStep = Max( minimumStep, kMinPointGap );
                } else {
                    minimumStep = 1.0f;
                }

                if ( stepLength < minimumStep ) {
                    return;
                }
            }
        } else {
            // The wheel jumped too far to keep drawing
            EndSkidTrail( skid );
        }
    }

    if ( !bAppendPoint ) {
        return;
    }

    SkidPoint &point = skid.pArrayPoints[skid.EndIndex];
    point.In = skid.Current.In;
    point.Out = skid.Current.Out;
    point.Color = skid.Current.Color;
    point.bLast = false;
    point.bGrounded = skid.Current.bGrounded;

    SkidPoint *pAppended = &point;
    if ( bSkidding ) {
        if ( skid.pLastAdded == nullptr ) {
            // Fade the leading end in.
            point.Color &= 0x00ffffff;
        }
        skid.pLastLastAdded = skid.pLastAdded;
    } else {
        point.bLast = true;
        skid.NumLastPoints++;
        pAppended = nullptr;
        skid.pLastLastAdded = nullptr;
    }
    skid.pLastAdded = pAppended;

    if ( skid.NumPoints < skid.NumPointsMax ) {
        skid.NumPoints++;
    }
    skid.EndIndex++;
    if ( skid.EndIndex >= skid.NumPointsMax ) {
        skid.EndIndex = 0;
    }
    if ( skid.EndIndex == skid.StartIndex ) {
        if ( skid.pArrayPoints[skid.StartIndex].bLast ) {
            skid.NumLastPoints--;
        }
        skid.StartIndex++;
        if ( skid.StartIndex >= skid.NumPointsMax ) {
            skid.StartIndex = 0;
        }
        skid.NumPoints--;
    }
}

void MngCarTireSkid::buildGeometry() {
    // FUN_00714cc0
    int32_t numVertices = 0;
    int32_t numStrips = 0;
    for ( int32_t car = 0; car < kMaxCars; car++ ) {
        const CarTireSkid &carSkid = pCarSkids[car];
        for ( int32_t wheel = 0; wheel < carSkid.NumSkids; wheel++ ) {
            const TireSkid &skid = carSkid.pSkids[wheel];
            int32_t numPoints = skid.NumPoints;
            if ( ( skid.Current.Color >> 24 ) != 0 ) {
                numPoints++;
            }
            if ( numPoints > 1 ) {
                numVertices += numPoints * 2;
                numStrips += skid.NumLastPoints + 1;
            }
        }
    }

    const int32_t numIndices = numVertices + numStrips * 2;
    if ( !renderList.beginPrimitive( PT_TriangleStrip, numVertices ) ) {
        return;
    }

    ActivePrimitive &primitive = renderList.getActivePrimitive();
    primitive.pPrimitive->NumVertex = numVertices;
    primitive.pPrimitive->NumIndex = numIndices;
    renderList.commitPrimitive();

    pPositionCursor = primitive.pVertexPosition;
    pColorCursor = primitive.pVertexDiffuse[0];
    pUV0Cursor = primitive.pVertexUV[0];
    pUV1Cursor = primitive.pVertexUV[1];
    pIndexCursor = primitive.pVertexIndices16;
    currentVertexIndex = 0;

    for ( int32_t car = 0; car < kMaxCars; car++ ) {
        const CarTireSkid &carSkid = pCarSkids[car];
        for ( int32_t wheel = 0; wheel < carSkid.NumSkids; wheel++ ) {
            emitSkid( carSkid.pSkids[wheel] );
        }
    }
}

void MngCarTireSkid::emitSkid( const TireSkid &skid ) {
    // FUN_00713fe0
    const bool bHasCurrent = ( skid.Current.Color >> 24 ) != 0;
    int32_t numPoints = skid.NumPoints;
    if ( bHasCurrent ) {
        numPoints++;
    }
    if ( numPoints <= 1 ) {
        return;
    }

    float texU = 1.0f;
    int32_t stripBase = currentVertexIndex;
    if ( skid.StartIndex != skid.EndIndex ) {
        int32_t idx = skid.StartIndex;
        do {
            const SkidPoint &point = skid.pArrayPoints[idx];
            emitSkidPoint( point, texU );
            stripBase = currentVertexIndex;
            texU = 1.0f - texU;
            if ( point.bLast ) {
                // Close the current strip with a degenerate edge.
                pIndexCursor[0] = (uint16_t)( stripBase - 1 );
                pIndexCursor[1] = (uint16_t)stripBase;
                pIndexCursor += 2;
                texU = 1.0f;
            }
            idx++;
            if ( idx >= skid.NumPointsMax ) {
                idx = 0;
            }
        } while ( idx != skid.EndIndex );
    }

    if ( bHasCurrent ) {
        emitSkidPoint( skid.Current, texU );
        stripBase = currentVertexIndex;
    }

    // Final degenerate edge stitching this skid to the next.
    pIndexCursor[0] = (uint16_t)( stripBase - 1 );
    pIndexCursor[1] = (uint16_t)stripBase;
    pIndexCursor += 2;
}

void MngCarTireSkid::emitSkidPoint( const SkidPoint &point, float texU ) {
    // FUN_0042ea60
    const uint32_t r = point.Color & 0xff;
    const uint32_t g = ( point.Color >> 8 ) & 0xff;
    const uint32_t b = ( point.Color >> 16 ) & 0xff;
    const uint32_t a = ( point.Color >> 24 ) & 0xff;
    const uint32_t packed = ( a << 24 ) | ( r << 16 ) | ( g << 8 ) | b;

    const float grounded = point.bGrounded ? 1.0f : 0.0f;

    pPositionCursor[0] = point.In.head<3>();
    pPositionCursor[1] = point.Out.head<3>();
    pPositionCursor += 2;

    pColorCursor[0] = packed;
    pColorCursor[1] = packed;
    pColorCursor += 2;

    pUV0Cursor[0] = Eigen::Vector2f( texU, 0.0f );
    pUV0Cursor[1] = Eigen::Vector2f( texU, 1.0f );
    pUV0Cursor += 2;

    pUV1Cursor[0] = Eigen::Vector2f( grounded, 0.0f );
    pUV1Cursor[1] = Eigen::Vector2f( grounded, 0.0f );
    pUV1Cursor += 2;

    pIndexCursor[0] = (uint16_t)currentVertexIndex;
    pIndexCursor[1] = (uint16_t)( currentVertexIndex + 1 );
    pIndexCursor += 2;
    currentVertexIndex += 2;
}

void MngCarTireSkid::draw() {
    // FUN_00714c80
    if ( bEnabled ) {
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->enqueueDynamicDrawList( &renderList );
    }
}

bool MngCarTireSkid::onWorldStreamed( Eigen::Vector4f *param_1,
                                      Eigen::Vector4f *,
                                      Eigen::Vector4f * ) {
    // FUN_00714a20
    if ( pCarSkids == nullptr ) {
        return true;
    }

    for ( int32_t car = 0; car < kMaxCars; car++ ) {
        const CarTireSkid &carSkid = pCarSkids[car];
        for ( int32_t wheel = 0; wheel < carSkid.NumSkids; wheel++ ) {
            ShiftSkid( carSkid.pSkids[wheel], *param_1 );
        }
    }
    return true;
}
