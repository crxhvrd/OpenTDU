#include "shared.h"
#include "vehicle_particles.h"

#include "core/maths_helper.h"

#include "render/instance.h"
#include "render/particles/gs_particles.h"

#include "game/material_types.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"

#include "physics/vehicle/physics_vehicle.h"

#include "world/gs_world_streaming.h"

#include <cmath>

bool gEnableParticles = true; // DAT_00f7f199

constexpr float kSpeedToKmh = 3.6f;                // DAT_00f41768
constexpr float kMinDebrisCarSpeed = 10.8f;        // DAT_00f41f24
constexpr float kMinDebrisWheelSpeed = 3.0f;       // DAT_00f41f20
constexpr float kBurnMinFraction = 0.02f;          // DAT_00f7f19c
constexpr float kSmokeMinOmega = 5.0f;             // DAT_00f7f1a0
constexpr float kSmokeMinSpeedLongitudinal = 5.0f; // DAT_00f7f1a4
constexpr float kSmokeMinSlipAngle = 0.7f;         // DAT_00f7f1a8
constexpr float kSmokeMinSlipRatio = 3.5f;         // DAT_00f7f1ac
constexpr float kShadowFloor = 0.5f;               // DAT_00f41674
constexpr float kShadowCap = 0.75f;                // DAT_00f416a4
constexpr float kRotationEpsilon = 1.0e-5f;        // DAT_00f41710
constexpr float kWheelParticleYOffset = 0.2f;      // DAT_00f41730

// Speed (km/h) -> { key, GenDensity, GenSize, rotation } curves used by the
// wheel debris emitters.
static const Eigen::Vector4f kDustCurve[] = {
    // DAT_00f7cf08
    Eigen::Vector4f( 0.0f, 1.0f, 0.1f, -1.0471976f ),
    Eigen::Vector4f( 60.0f, 1.0f, 0.5f, 0.0f ),
    Eigen::Vector4f( 80.0f, 1.0f, 1.0f, 0.0f ),
    Eigen::Vector4f( -1.0f, 0.0f, 0.0f, 0.0f ),
};
static const Eigen::Vector4f kGrassCurve[] = {
    // DAT_00f7cf48
    Eigen::Vector4f( 0.0f, 1.0f, 0.2f, -1.0471976f ),
    Eigen::Vector4f( 80.0f, 1.0f, 0.5f, 0.0f ),
    Eigen::Vector4f( 120.0f, 1.0f, 1.0f, 0.0f ),
    Eigen::Vector4f( -1.0f, 0.0f, 0.0f, 0.0f ),
};
static const Eigen::Vector4f kGravelCurve[] = {
    // DAT_00f7cf88
    Eigen::Vector4f( 0.0f, 1.0f, 0.1f, -1.0471976f ),
    Eigen::Vector4f( 80.0f, 1.0f, 0.4f, 0.0f ),
    Eigen::Vector4f( 120.0f, 1.0f, 1.0f, 0.0f ),
    Eigen::Vector4f( -1.0f, 0.0f, 0.0f, 0.0f ),
};

static void StopParticleInstance( ParticleInstance *&pInstance,
                                  bool bImmediate ) {
    if ( pInstance != nullptr ) {
        pInstance->State =
            bImmediate ? eParticleState::PS_Dead : eParticleState::PS_Killed;
        pInstance->stop();
        pInstance = nullptr;
    }
}

static float GetWheelBurnFraction( const CarState *, uint32_t ) {
    // FUN_008b7eb0
    OTDU_UNIMPLEMENTED;
    return 0.0f;
}

static void ApplySpeedCurve( ParticleInstance *pInstance,
                             float speed,
                             const Eigen::Vector4f *pCurve ) {
    // FUN_008b7de0
    const Eigen::Vector4f *pLow = pCurve;
    const Eigen::Vector4f *pHigh = pCurve;
    float density = pCurve->y();
    float size = pCurve->z();
    float rotation = pCurve->w();

    if ( pCurve->x() >= 0.0f ) {
        const Eigen::Vector4f *pCur = pCurve;
        for ( ;; ) {
            pHigh = pCur;
            if ( speed < pCur->x() ) {
                break;
            }
            pHigh = pCur + 1;
            pLow = pCur;
            if ( ( pCur + 1 )->x() < 0.0f ) {
                break;
            }
            pCur = pCur + 1;
        }

        if ( pLow != pHigh && pHigh->x() >= 0.0f ) {
            const float frac =
                ( speed - pLow->x() ) / ( pHigh->x() - pLow->x() );
            rotation = ( pHigh->w() - pLow->w() ) * frac + pLow->w();
            density = ( pHigh->y() - pLow->y() ) * frac + pLow->y();
            size = ( pHigh->z() - pLow->z() ) * frac + pLow->z();
        } else {
            density = pLow->y();
            size = pLow->z();
            rotation = pLow->w();
        }
    }

    pInstance->Density = density;
    pInstance->Scale = size;
    if ( std::fabs( rotation ) > kRotationEpsilon ) {
        pInstance->Matrix = MakeRotationMatX( rotation ) * pInstance->Matrix;
    }
}

VehicleParticles::VehicleParticles() { reset(); }

VehicleParticles::~VehicleParticles() {}

void VehicleParticles::update( VehicleBase *pVehicle ) {
    // FUN_008b7eb0
    CarState *pCarState = pVehicle->pCarState;
    if ( gpParticles == nullptr || pCarState == nullptr ) {
        return;
    }

    if ( !gEnableParticles ) {
        stopAll( true );
        return;
    }

    PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    if ( pPhysics == nullptr || !pPhysics->isActive() ) {
        // TODO: INCOMPLETE (Original additionally gates on
        // IsPaused()/ready/disabled flags read)
        stopAll( false );
        return;
    }

    const uint32_t numWheels = pCarState->getNumWheels();
    const CarBodyState &body = *pCarState->getBody();
    const float speedKmh = body.SpeedInKmh * kSpeedToKmh;
    const bool bIsTraffic =
        pCarState->getOwner() == CarState::eOwner::CSO_Traffic;

    const Eigen::Vector4f shiftOffset =
        ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() )
            ? gpWorldStreaming->getPosition()
            : Eigen::Vector4f::Zero();

    const int32_t frontWheelA = 0;
    const int32_t frontWheelB = static_cast<int32_t>( numWheels ) / 4;

    for ( uint32_t w = 0; w < numWheels; w++ ) {
        const bool bFrontWheel = ( static_cast<int32_t>( w ) == frontWheelA ||
                                   static_cast<int32_t>( w ) == frontWheelB );
        const WheelState &wheel = pCarState->getWheel( w );

        bool bOnRoadMode = wheel.bOnRoad;
        bool wantDust = false;
        bool wantGrass = false;
        bool wantGravel = false;

        if ( !bOnRoadMode && wheel.bGrounded ) {
            const eMaterialType material = wheel.ContactMaterial;
            if ( material == eMaterialType::MT_Asphalt ) {
                bOnRoadMode = true;
            } else {
                const float wheelTangentialSpeed =
                    std::fabs( wheel.Omega ) * wheel.WheelRadius;
                if ( speedKmh > kMinDebrisCarSpeed ||
                     wheelTangentialSpeed > kMinDebrisWheelSpeed ) {
                    if ( material == eMaterialType::MT_Grass ) {
                        wantDust = true;
                        wantGrass = true;
                        wantGravel = true;
                    } else if ( material == eMaterialType::MT_Dirt ) {
                        wantDust = true;
                        wantGravel = true;
                    }
                }
            }
        }

        bool wantBurn = false;
        bool wantSmoke = false;
        if ( bOnRoadMode && !bFrontWheel && !bIsTraffic ) {
            const float burnFraction = GetWheelBurnFraction( pCarState, w );
            if ( burnFraction >= kBurnMinFraction ) {
                wantBurn = true;
            } else {
                const bool bLowSlip =
                    ( std::fabs( wheel.Omega ) <= kSmokeMinOmega &&
                      std::fabs( wheel.LongitudinalSpeed ) <=
                          kSmokeMinSpeedLongitudinal ) ||
                    ( std::fabs( wheel.SlipAngle ) <= kSmokeMinSlipAngle &&
                      std::fabs( wheel.SlipRatio ) <= kSmokeMinSlipRatio );
                wantSmoke = !bLowSlip;
            }
        }

        if ( bFrontWheel ) {
            wantDust = false;
            wantGrass = false;
            wantGravel = false;
        }

        if ( wantBurn ) {
            if ( burnParticles[w] == nullptr ) {
                burnParticles[w] = gpParticles->activateParticles(
                    eParticleType::PT_Burn, true );
            }
        } else {
            StopParticleInstance( burnParticles[w], false );
        }

        if ( wantSmoke ) {
            if ( smokeParticles[w] == nullptr ) {
                smokeParticles[w] = gpParticles->activateParticles(
                    eParticleType::PT_Smoke, true );
            }
        } else {
            StopParticleInstance( smokeParticles[w], false );
        }

        if ( wantDust ) {
            if ( dustParticles[w] == nullptr ) {
                dustParticles[w] = gpParticles->activateParticles(
                    eParticleType::PT_Dust, true );
            }
        } else {
            StopParticleInstance( dustParticles[w], false );
        }

        if ( wantGrass ) {
            if ( grassParticles[w] == nullptr ) {
                grassParticles[w] = gpParticles->activateParticles(
                    eParticleType::PT_Grass, true );
            }
        } else {
            StopParticleInstance( grassParticles[w], false );
        }

        if ( wantGravel ) {
            if ( gravelParticles[w] == nullptr ) {
                gravelParticles[w] = gpParticles->activateParticles(
                    eParticleType::PT_Gravel, true );
            }
        } else {
            StopParticleInstance( gravelParticles[w], false );
        }

        Matrix4x4f_t wheelMatrix = body.WorldMatrix;
        const Eigen::Vector4f wheelWorldPos =
            TDUMat4x4MulVec( body.OffsetedWorldMatrix,
                             wheel.RelativeWorldPosition ) +
            shiftOffset;
        wheelMatrix( 3, 0 ) = wheelWorldPos.x();
        wheelMatrix( 3, 1 ) = wheelWorldPos.y() + kWheelParticleYOffset;
        wheelMatrix( 3, 2 ) = wheelWorldPos.z();
        wheelMatrix( 3, 3 ) = 1.0f;

        Eigen::Vector4f debrisColor = wheel.getShadowColor();
        if ( debrisColor.x() < kShadowFloor ) {
            debrisColor = Eigen::Vector4f( kShadowFloor, kShadowFloor,
                                           kShadowFloor, 1.0f );
        }
        Eigen::Vector4f tyreColor = debrisColor;
        if ( debrisColor.x() > kShadowCap ) {
            tyreColor =
                Eigen::Vector4f( kShadowCap, kShadowCap, kShadowCap, 1.0f );
        }

        if ( burnParticles[w] != nullptr ) {
            burnParticles[w]->Matrix = wheelMatrix;
            burnParticles[w]->Color = tyreColor;
        }
        if ( smokeParticles[w] != nullptr ) {
            smokeParticles[w]->Matrix = wheelMatrix;
            smokeParticles[w]->Color = tyreColor;
        }
        if ( dustParticles[w] != nullptr ) {
            dustParticles[w]->Matrix = wheelMatrix;
            dustParticles[w]->Color = debrisColor;
            ApplySpeedCurve( dustParticles[w], speedKmh, kDustCurve );
        }
        if ( grassParticles[w] != nullptr ) {
            grassParticles[w]->Matrix = wheelMatrix;
            grassParticles[w]->Color = debrisColor;
            ApplySpeedCurve( grassParticles[w], speedKmh, kGrassCurve );
        }
        if ( gravelParticles[w] != nullptr ) {
            gravelParticles[w]->Matrix = wheelMatrix;
            gravelParticles[w]->Color = debrisColor;
            ApplySpeedCurve( gravelParticles[w], speedKmh, kGravelCurve );
        }
    }

    if ( bIsTraffic ) {
        return;
    }

    const VehicleBaseResources *pResource = pCarState->getResource();
    const bool bEngineIgniting =
        ( pResource != nullptr && pResource->pEngineSim != nullptr &&
          ( pResource->pEngineSim->State == eEngineState::ES_Crank ||
            pResource->pEngineSim->State == eEngineState::ES_RevUp ) );
    if ( !bEngineIgniting ) {
        for ( uint32_t i = 0; i < kNumInstancePerType; i++ ) {
            StopParticleInstance( startExhaustParticles[i], false );
        }
        return;
    }

    for ( uint32_t i = 0; i < kNumInstancePerType; i++ ) {
        Instance *pExhaustDummy = pVehicle->exhaustsDummies[i];
        if ( pExhaustDummy == nullptr ) {
            continue;
        }
        ParticleInstance *&pInstance = startExhaustParticles[i];
        if ( pInstance == nullptr ) {
            pInstance = gpParticles->activateParticles(
                eParticleType::PT_StartLine, true );
        } else if ( pInstance->pParticleInstanciated == nullptr ||
                    pInstance->pParticleInstanciated->Type !=
                        eParticleType::PT_StartLine ) {
            StopParticleInstance( pInstance, false );
            pInstance = gpParticles->activateParticles(
                eParticleType::PT_StartLine, true );
        }
        if ( pInstance != nullptr ) {
            Matrix4x4f_t exhaustMatrix = pExhaustDummy->getModelMatrix();
            exhaustMatrix( 3, 0 ) += shiftOffset.x();
            exhaustMatrix( 3, 1 ) += shiftOffset.y();
            exhaustMatrix( 3, 2 ) += shiftOffset.z();
            pInstance->Matrix = exhaustMatrix;
        }
    }
}

void VehicleParticles::stopAll( bool bImmediate ) {
    // FUN_008b7c90
    if ( gpParticles == nullptr ) {
        return;
    }
    for ( uint32_t w = 0; w < kNumInstancePerType; w++ ) {
        StopParticleInstance( burnParticles[w], bImmediate );
        StopParticleInstance( smokeParticles[w], bImmediate );
        StopParticleInstance( dustParticles[w], bImmediate );
        StopParticleInstance( grassParticles[w], bImmediate );
        StopParticleInstance( gravelParticles[w], bImmediate );
    }
    for ( uint32_t i = 0; i < kNumInstancePerType; i++ ) {
        StopParticleInstance( startExhaustParticles[i], false );
    }
}

void VehicleParticles::reset() {
    burnParticles.fill( nullptr );
    smokeParticles.fill( nullptr );
    dustParticles.fill( nullptr );
    gravelParticles.fill( nullptr );
    grassParticles.fill( nullptr );
    startExhaustParticles.fill( nullptr );
}

void VehicleParticles::setState( bool param_2 ) { stopAll( param_2 ); }
