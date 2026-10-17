#include "shared.h"

#include "vehicle_simulation.h"
#include "core/maths_helper.h"
#include "physics/physics_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

static constexpr float kSlipLongScale = 100.0f; // DAT_00f41708 (inside cf50)
static constexpr float kLatSlipMinSpeed = 2.0f; // m/s
static constexpr float kLoadToPerMille =
    0.001f; // FLOAT_00f4167c (load -> cf50/ceb0 scale)

static constexpr float kMaxCoupling = 0.0f;

static constexpr float kZeroSpeedSlipRatio = 20.0f;    // DAT_00f03ee0
static constexpr float kWattToPS = 0.0013597f;         // DAT_00f41694 = 1/735.5
static constexpr float kEngineBrakeRampStart = -0.25f; // DAT_00f41b10

static float WheelLongitudinalSlipRatio( float radius,
                                         float vLong,
                                         float vLat,
                                         float omega ) {
    // FUN_0043cff0
    const float speed = std::sqrt( vLong * vLong + vLat * vLat );
    if ( speed == 0.0f ) { // FLOAT_00f41660
        return kZeroSpeedSlipRatio;
    }
    return ( omega * radius - vLong ) / speed;
}

// Debug override for the driveline inertia divisor mode (see FUN_0043aba0)
int gVehicleDrivelineInertiaMode = -1;

float gVehicleCouplingCoefE = -1.0f;
float gVehicleCouplingCoefW = -1.0f;
float gVehicleMaxCoupling = kMaxCoupling;

static constexpr float kShiftDecisionThrottle = 1.0f; // DAT_00f4165c
static constexpr float kShiftLockoutFrac = 0.5f;      // DAT_00fabf9c
static constexpr float kDownshiftSlowMult = 3.0f;     // DAT_00f416b4
static constexpr float kShiftThrottleRate = 50.0f;    // DAT_00fabf84
static constexpr float kShiftGaugeRelax = 0.075f;     // DAT_00fabf94
static constexpr float kShiftGaugeMax = 10.0f;        // DAT_00f4166c
static constexpr float kBogAngAccel =
    -104.72f; // DAT_00fabf98(-1000rpm/s) * DAT_00f418ac(0.10472) = rad/s^2
static constexpr float kShiftLoadRefG =
    5.0f; // DAT_00f41750 (~g/2): mass -> reference-load scale (traction gate)
static constexpr float kShiftLoadFrac =
    0.15f; // FLOAT_00fabfa0: near-lift load fraction
static constexpr float kShiftBrakeTrigger =
    0.2f; // DAT_00f41730: brake > 0.2 forces a downshift evaluation
static constexpr float kShiftHandbrakeMax = 0.25f; // DAT_00f416dc
int gVehicleDiffTypeOverride =
    0; // debug: 0=per-diff (defaults open); 1 open / 2 LSD / 4 spool
float gVehicleDiffLockCoef =
    20.0f; // debug: LSD/spool lock coef when overriding
static constexpr float kDiffTractionGateF = 0.001f; // FLOAT_00f4167c
static constexpr float kDiffGripEps = 0.0001f;   // DAT_00f41760 (ratio guard)
static constexpr float kDiffRatioInf = 1.08e19f; // FLOAT_00f41898 (ratio ~inf)
static constexpr float kClutchThrottleArm = 0.05f;  // DAT_00f41778
static constexpr float kClutchThrottleDrop = 0.1f;  // FLOAT_00f41668
static constexpr float kThousandRpmRad = 104.72f;   // FLOAT_00f41894 (1000rpm)
static constexpr float kFreeRollBrakeTorque = 0.1f; // FLOAT_00f41668
static constexpr float kFreeRollDriveTorque = 1.0f; // FLOAT_00f4165c
static constexpr float kHandbrakeInputThreshold = 0.1f; // FLOAT_00f41668

bool gVehicleEngineStateMachine = true;
static constexpr float kEngineOmegaFloor = 0.1f; // FLOAT_00f41668
static constexpr float kEngineStallFrac =
    0.25f; // stall below this*idle; DAT_00f416dc
static constexpr float kTorqueSmoothRate = 100.0f;

static void StepClutch( PhysicsClutchSim &c,
                        const PhysicsEngineSim &e,
                        int gear,
                        float throttle,
                        float dt ) {
    // FUN_00440540
    const float idle = e.OmegaIdle;
    const float omega = e.Omega;
    const float a = c.LaunchIdleMult * idle;
    const float b = c.LaunchPeakPowerMult * e.PeakPowerOmega;
    float launchRPM = ( b - a ) * ( throttle - 0.5f ) * 2.0f + a;
    launchRPM = Clamp( launchRPM, std::min( a, b ), std::max( a, b ) );

    if ( c.bEnabled && !c.bManualControl ) {
        switch ( c.State ) {
        case 0: // disengaged
            c.Target = 0.0f;
            if ( throttle > kClutchThrottleArm ) {
                c.State = 1;
                c.Timer = 0.0f;
            }
            break;
        case 1: // armed: wait until the engine revs past launchRPM
            if ( launchRPM < omega ) {
                c.State = 2;
            }
            break;
        case 2: { // launch/modulate
            c.Timer += dt;
            if ( throttle <= kClutchThrottleDrop ) {
                c.State = 0;
            }
            const float engRate =
                Clamp( ( c.EngageThrottleLo - c.EngageThrottleHi ) * throttle +
                           c.EngageThrottleHi,
                       std::min( c.EngageThrottleHi, c.EngageThrottleLo ),
                       std::max( c.EngageThrottleHi, c.EngageThrottleLo ) );
            const float dOmega = e.AngAccel;
            const float decelRef = ( e.PeakTorqueOmega - idle ) * 0.2f;
            const float launchFloor =
                std::max( launchRPM - kThousandRpmRad, idle );

            const bool condA =
                ( launchFloor < omega && c.DecelThreshold <= dOmega ) ||
                ( launchRPM < omega );
            if ( condA ) {
                if ( dOmega <= decelRef && omega <= e.PeakPowerOmega ) {
                    c.Target -= dt; // slowing -> ease off
                } else {
                    c.Target += engRate * dt; // engage
                }
            } else {
                if ( omega <= idle * 2.0f ) {
                    c.Target -= dt; // very low -> ease off
                }
                if ( dOmega <= decelRef ) {
                    c.Target -= dt; // decelerating -> ease off
                } else {
                    c.Target += engRate * dt; // engage
                }
            }
            c.Target = Clamp( c.Target, 0.0f, 1.0f );
            if ( c.Target >= c.LockThreshold || gear > 2 ) {
                c.Target = 1.0f;
                c.State = 3;
            }
            break;
        }
        case 3: // locked
            if ( omega <= idle && ( gear == 2 || gear == 0 ) ) {
                c.State = 0; // stalling in 1st/reverse -> disengage
            }
            break;
        }
    }

    const float d = c.Target - c.Engagement;
    if ( c.EngageRate * dt < d ) {
        c.Engagement += c.EngageRate * dt;
    } else if ( -c.DisengageRate * dt <= d ) {
        c.Engagement = c.Target;
    } else {
        c.Engagement -= c.DisengageRate * dt;
    }
    c.Engagement = Clamp( c.Engagement, 0.0f, 1.0f );
}

static float
PacejkaLongitudinal( const PhysicsTire &tire, float L, float kappa ) {
    // FUN_0043cf50
    const float *C = tire.Long;
    const float D = L * C[1] + C[2]; // load-dependent peak factor
    const float denom = D * C[0];    // D * shape C
    if ( std::fabs( denom ) < 1e-8f ) {
        return 0.0f;
    }
    const float ex =
        std::exp( -( L * C[5] ) ); // via 2^(log2e * x) in the original
    const float B = ( C[9] * L + C[10] + kappa * kSlipLongScale ) *
                    ( ( L * C[3] + C[4] ) * ex ) / denom;
    const float a1 = std::atan( B );
    const float E = ( C[6] * L + C[7] ) * L + C[8];
    const float a2 = std::atan( E * ( a1 - B ) + B );
    const float s = std::sin( a2 * C[0] );
    return D * L * s;
}

static float
PacejkaLateral( const PhysicsTire &tire, float alpha, float camber, float L ) {
    // FUN_0043ceb0
    const float *C = tire.Lat;
    const float D = ( L * C[1] + C[2] ) * L; // load-dependent peak (quadratic)
    const float denom = D * C[0];
    if ( std::fabs( denom ) < 1e-8f || std::fabs( C[4] ) < 1e-8f ) {
        return 0.0f;
    }
    const float s0 = std::sin( 2.0f * std::atan( L / C[4] ) );
    const float B =
        ( L * C[9] + camber * C[8] + C[10] + alpha ) *
        ( ( ( 1.0f - std::fabs( camber ) * C[5] ) * s0 * C[3] ) / denom );
    const float a1 = std::atan( B );
    const float E = L * C[6] + C[7];
    const float a2 = std::atan( E * ( a1 - B ) + B );
    const float s = std::sin( a2 * C[0] );
    const float Sv = ( ( L * C[11] + C[12] ) * camber + C[13] ) * L + C[14];
    return Sv + s * ( -1.0f ) * D; // FLOAT_00fabfb0 = -1.0
}

static void NormalizeSlipsAndCurves( PhysicsWheelState &w,
                                     float slipAngleDeg,
                                     float rawLongSlip,
                                     float load ) {
    // FUN_0043d290
    const PhysicsTire &tire = w.Tire;
    const int N = tire.LoadTableCount;

    float peakLong = 1.0f;
    float peakLat = 1.0f;
    if ( N >= 2 ) {
        const float step = tire.LoadTableMaxLoad / static_cast<float>( N );
        int i = static_cast<int>( load / step );
        if ( i > N - 2 )
            i = N - 2;
        if ( i < 0 )
            i = 0;
        const float xL = i * step;
        const float span = step; // == (step + xL) - xL

        float lo = tire.PeakLong[i], hi = tire.PeakLong[i + 1];
        peakLong = ( ( hi - lo ) * ( load - xL ) ) / span + lo;
        peakLong = Clamp( peakLong, std::min( lo, hi ), std::max( lo, hi ) );

        lo = tire.PeakLat[i];
        hi = tire.PeakLat[i + 1];
        peakLat = ( ( hi - lo ) * ( load - xL ) ) / span + lo;
        peakLat = Clamp( peakLat, std::min( lo, hi ), std::max( lo, hi ) );
    }
    if ( std::fabs( peakLong ) < 1e-6f )
        peakLong = 1.0f;
    if ( std::fabs( peakLat ) < 1e-6f )
        peakLat = 1.0f;

    float nLong = rawLongSlip / peakLong;
    float nLat = slipAngleDeg / peakLat;
    w.NormLong = nLong;
    w.NormLat = nLat;

    nLat = Clamp( nLat, -tire.MaxSlipAngle, tire.MaxSlipAngle );
    nLong = Clamp( nLong, -tire.MaxSlipRatio, tire.MaxSlipRatio );
    w.NormLongClamped = nLong;
    w.NormLatClamped = nLat;

    const float loadPM = load * kLoadToPerMille;
    const float longSlipCurveInput = rawLongSlip;
    w.TireForceLong = PacejkaLongitudinal( tire, loadPM, longSlipCurveInput );
    const float Fy = PacejkaLateral( tire, slipAngleDeg, 0.0f, loadPM );
    w.TireForceLat = Fy;

    if ( std::fabs( tire.FrictionEllipseThreshold ) < std::fabs( nLong ) ) {
        const float minF = tire.FrictionEllipseMinFactor;
        const float denom = tire.MaxSlipRatio - tire.FrictionEllipseThreshold;
        if ( std::fabs( denom ) > 1e-8f ) {
            float f = ( ( std::fabs( nLong ) - tire.FrictionEllipseThreshold ) *
                        ( minF - 1.0f ) ) /
                          denom +
                      1.0f;
            f = Clamp( f, std::min( 1.0f, minF ), std::max( 1.0f, minF ) );
            w.TireForceLat = Fy * f;
        }
    }
}

void ComputeTireForces( PhysicsWheelState &w,
                        const Matrix4x4f_t &vehicleMatrix ) {
    // FUN_0043dbf0
    w.ContactVelLong = w.WheelLinVel.dot( w.ContactLongAxis );
    w.ContactVelLat = w.WheelLinVel.dot( w.ContactLatAxis );
    w.ContactVelM0 = RowDot( vehicleMatrix, 0, w.WheelLinVel );
    w.ContactVelM1 = RowDot( vehicleMatrix, 1, w.WheelLinVel );
    w.ContactVelM2 = RowDot( vehicleMatrix, 2, w.WheelLinVel );

    const float vLong = w.ContactVelLong;
    const float vLat = w.ContactVelLat;

    // Deadzone / no-load early out
    const bool belowDeadzone = std::fabs( vLong ) < w.DeadbandLong &&
                               std::fabs( vLat ) < w.DeadbandLat &&
                               std::fabs( w.AngularVel ) < w.DeadbandOmega;
    if ( belowDeadzone || w.NormalLoad <= 0.0f ) {
        w.TireForceLong = 0.0f;
        w.TireForceLat = 0.0f;
        w.DragLong = 0.0f;
        w.DragLat = 0.0f;
        w.bTireSlipping = false;
        w.NormLongClamped = 0.0f;
        w.NormLatClamped = 0.0f;
    } else {
        w.bTireSlipping = true;

        const float rawLongSlip =
            WheelLongitudinalSlipRatio( w.Radius, vLong, vLat, w.AngularVel );
        float a;
        if ( vLong > 0.0f ) {
            a = std::atan( vLat / vLong ) * kRadToDeg;
        } else if ( vLong < 0.0f ) {
            a = -std::atan( vLat / vLong ) * kRadToDeg;
        } else {
            a = 0.0f;
        }

        if ( w.SideFlag != 1 ) {
            a = -a;
        }

        NormalizeSlipsAndCurves( w, a, rawLongSlip, w.NormalLoad );

        // Scale the raw curve forces by the grip terms
        const float g = w.DynamicGrip * w.GripMult;
        const float Fy =
            w.GripLat * g * w.TireForceLat * w.GripLong * w.LoadScaleLong;
        const float Fx = g * w.TireForceLong * w.GripLong * w.LoadScaleLong;
        w.TireForceLat = Fy;
        w.TireForceLong = Fx;
        if ( 1.0f < std::fabs( w.BrakeCmdTorque ) ) {
            w.TireForceLong = w.BrakeGripLong * Fx;
            w.TireForceLat = w.BrakeGripLat * Fy;
        }
        w.DragLat =
            -( std::fabs( vLat ) * vLat * w.NormalLoad * w.LoadScaleDrag );
        w.DragLong =
            ( std::fabs( vLong ) * vLong * w.NormalLoad * w.LoadScaleDrag );

        if ( w.SideFlag != 1 ) {
            w.NormLatClamped = -w.NormLatClamped;
            w.NormLat = -w.NormLat;
            w.TireForceLat = -w.TireForceLat;
        }
    }

    w.TireForcePoint = w.ContactPoint;
}

void ComputeSuspensionForce( PhysicsWheelState &w ) {
    // FUN_0043e2f0
    const float loadFwd = w.SuspTotalForce + w.AntiRollForce;
    const float diff = w.SuspTotalForce - w.AntiRollForce;

    w.NormalLoad = std::max( loadFwd, 0.0f );
    if ( w.NormalLoad > w.SuspMaxLoad )
        w.NormalLoad = w.SuspMaxLoad;
    if ( w.NormalLoad < w.SuspMinLoad )
        w.NormalLoad = w.SuspMinLoad;
    const Eigen::Vector4f &axis =
        ( !w.bGroundContact ) ? w.WheelUpAxis : w.SuspAxis;
    w.SuspForce = axis * diff;
    w.SuspForcePoint = w.ContactPoint;
}

float ComputeWheelReactionTorque( const PhysicsWheelState &w,
                                  float deltaTime,
                                  float *pOutLongForce ) {
    // FUN_0043e440
    const float totalInertia = w.Inertia + w.ReflectedInertia;
    const float inertia = ( totalInertia > 1e-6f ) ? totalInertia : 1e-6f;
    const float omega = w.AngularVel;

    float reactTorque = -w.TireForceLong * w.Radius;
    float longForce = w.TireForceLong;

    if ( w.Radius > 1e-4f && deltaTime > 0.0f ) {
        const float omegaFree = w.ContactVelLong / w.Radius;
        const float lockTorque =
            ( omegaFree - omega ) * ( 1.0f / deltaTime ) * inertia;
        if ( ( 0.0f < lockTorque && lockTorque < reactTorque ) ||
             ( lockTorque < 0.0f && reactTorque < lockTorque ) ) {
            reactTorque = lockTorque;
            longForce = -lockTorque / w.Radius;
        }
    }

    if ( pOutLongForce != nullptr ) {
        *pOutLongForce = longForce;
    }

    reactTorque += w.DragLong * ( ( omega > 0.0f ) ? 1.0f : -1.0f ) * w.Radius;
    return reactTorque;
}

void IntegrateWheel( PhysicsWheelState &w, float deltaTime ) {
    // FUN_0043e440
    const float totalInertia = w.Inertia + w.ReflectedInertia;
    const float inertia = ( totalInertia > 1e-6f ) ? totalInertia : 1e-6f;
    float omega = w.AngularVel;
    float reactTorque = ComputeWheelReactionTorque( w, deltaTime );
    if ( std::fabs( w.BrakeTorqueOut ) < kFreeRollBrakeTorque &&
         std::fabs( w.DriveTorque ) <= kFreeRollDriveTorque && w.bFreeRoll &&
         w.Radius > 1e-4f ) {
        w.TireForceLong = 0.0f;
        reactTorque = 0.0f;
        omega = w.ContactVelLong / w.Radius;
        w.AngularVel = omega;
    }

    const float slipCoef = 1.0f - w.ReactionSlipCoef;
    const float loadScale =
        Clamp( 1.0f - w.ReactionSlipCoef * std::fabs( w.NormLatClamped ) * 0.5f,
               std::min( slipCoef, 1.0f ), std::max( slipCoef, 1.0f ) );

    const float helpTorque = w.EspTorqueOut + w.BrakeTorqueOut;
    w.NetTorque = ( helpTorque + reactTorque ) * w.ReactionScale * loadScale;
    w.AngAccel = ( w.NetTorque + w.DriveTorque ) / inertia;
    w.AngularVel = w.AngAccel * deltaTime + omega;

    w.SpinAngle += w.AngularVel * deltaTime;
    if ( kTwoPI < w.SpinAngle ) {
        w.SpinAngle -= kTwoPI;
    }
}

static float
InterpTorque( const PhysicsEngineSim::CurvePoint *pts, int n, float omega ) {
    // FUN_0043f230 (inlined)
    if ( n <= 0 ) {
        return 0.0f;
    }
    if ( omega <= pts[0].Omega ) {
        return pts[0].Torque;
    }
    for ( int i = 0; i < n - 1; ++i ) {
        if ( omega < pts[i + 1].Omega ) {
            const float o0 = pts[i].Omega, o1 = pts[i + 1].Omega;
            const float t0 = pts[i].Torque, t1 = pts[i + 1].Torque;
            if ( o1 - o0 < 1e-6f ) {
                return t0;
            }
            const float y = t0 + ( t1 - t0 ) * ( omega - o0 ) / ( o1 - o0 );
            return Clamp( y, std::min( t0, t1 ), std::max( t0, t1 ) );
        }
    }
    return pts[n - 1].Torque;
}

float ThrottlePosForTorque( const PhysicsEngineSim &e,
                            float targetTorque,
                            float omega ) {
    // FUN_0043f460
    if ( e.CurveCount < 2 ) {
        return 0.0f;
    }
    if ( omega < 0.0f ) {
        omega = e.Omega;
    }
    const float tMax = InterpTorque( e.CurveMax, e.CurveCount, omega );
    const float tMin = InterpTorque( e.CurveMin, e.CurveCount, omega );
    const float span = tMax - tMin;
    if ( std::fabs( span ) < 1e-8f ) {
        return 0.0f;
    }
    return Clamp( ( targetTorque - tMin ) / span, 0.0f, 1.0f );
}

float EngineTorqueAt( const PhysicsEngineSim &e,
                      float omega,
                      float throttle,
                      bool bApplyIdleFloor ) {
    // FUN_0043f230
    if ( e.CurveCount < 2 ) {
        return 0.0f;
    }
    if ( e.ThrottleIdle > throttle && bApplyIdleFloor ) {
        throttle = e.ThrottleIdle;
    }
    if ( omega < 0.0f )
        omega = e.Omega;
    if ( omega > e.OmegaMax )
        omega = e.OmegaMax;
    const float tMax = InterpTorque( e.CurveMax, e.CurveCount, omega );
    const float tMin = InterpTorque( e.CurveMin, e.CurveCount, omega );
    float t = tMin + throttle * ( tMax - tMin );
    t = Clamp( t, std::min( tMin, tMax ), std::max( tMin, tMax ) );
    return t * e.TorqueScale;
}

void BuildEngineCurves( PhysicsEngineSim &e,
                        const float *pairs,
                        int numPairs,
                        float bhp,
                        float packBoost,
                        float frictionCoef ) {
    // FUN_008345e0
    e.CurveCount = 0;
    if ( pairs == nullptr || numPairs < 2 ) {
        return;
    }
    const int n = std::min( numPairs, PhysicsEngineSim::kMaxCurve );

    float peakTorque = -1.0e30f, omegaPeakTorque = 0.0f;
    int iPeakTorque = 0;
    float peakPowerPS = 0.0f, torqueAtPeakPower = 0.0f, omegaPeakPower = 0.0f;
    for ( int i = 0; i < n; ++i ) {
        const float om = pairs[2 * i], tq = pairs[2 * i + 1];
        if ( tq > peakTorque ) {
            peakTorque = tq;
            omegaPeakTorque = om;
            iPeakTorque = i;
        }
        const float powerPS = tq * om * kWattToPS;
        if ( powerPS > peakPowerPS ) {
            peakPowerPS = powerPS;
            torqueAtPeakPower = tq;
            omegaPeakPower = om;
        }
    }
    const float normScale =
        ( peakPowerPS > 1e-6f ) ? ( bhp * packBoost ) / peakPowerPS : 1.0f;
    e.PeakTorqueOmega = omegaPeakTorque;
    e.PeakPowerOmega = omegaPeakPower;
    e.PeakTorque = peakTorque * normScale;
    e.CurveCount = n;
    for ( int i = 0; i < n; ++i ) {
        e.CurveMax[i].Omega = pairs[2 * i];
        e.CurveMax[i].Torque = pairs[2 * i + 1] * normScale;
    }

    const float rho =
        ( peakTorque > 1e-6f ) ? torqueAtPeakPower / peakTorque : 1.0f;
    const float kf = frictionCoef;
    const float brakeFloor = -( kf * torqueAtPeakPower );
    const float rStart = kf * kEngineBrakeRampStart; // -0.25 * kf
    const float rEnd = -kf;
    for ( int i = 0; i < n; ++i ) {
        e.CurveMin[i].Omega = e.CurveMax[i].Omega;
        if ( i < iPeakTorque && iPeakTorque > 0 ) {
            float r =
                ( (float)i * ( rEnd - rStart ) ) / (float)iPeakTorque + rStart;
            r = Clamp( r, std::min( rStart, rEnd ), std::max( rStart, rEnd ) );
            e.CurveMin[i].Torque = e.CurveMax[i].Torque * rho * r;
        } else {
            e.CurveMin[i].Torque = brakeFloor;
        }
    }
}

static void SplitDiff( const PhysicsDifferentialSim &d,
                       float tIn,
                       float reactA,
                       float reactB,
                       float speedA,
                       float speedB,
                       float &outA,
                       float &outB,
                       int32_t &state ) {
    // FUN_004402a0
    outA = 0.0f;
    outB = 0.0f;
    const float bias = d.SplitRatio;

    if ( d.Type == 1 || d.Type == 2 ) {
        state = 1;
        const bool gripCond =
            tIn > 0.0f && ( std::fabs( reactA ) >= kDiffTractionGateF ||
                            std::fabs( reactB ) >= kDiffTractionGateF );
        if ( gripCond ) {
            const float gripA = std::max( 0.0f, -reactA );
            const float gripB = std::max( 0.0f, -reactB );
            const float half = bias * tIn;
            float ratio = kDiffRatioInf;
            float gripDiff;
            if ( gripA <= gripB ) {
                if ( gripA > kDiffGripEps ) {
                    ratio = gripB / gripA;
                }
                gripDiff = gripB - gripA;
                const float clamped = std::min( half, gripA + gripB );
                outB = clamped;
                outA = half + std::max( 0.0f, half - clamped );
            } else {
                if ( gripB > kDiffGripEps ) {
                    ratio = gripA / gripB;
                }
                gripDiff = gripA - gripB;
                const float clamped = std::min( half, gripA + gripB );
                outA = clamped;
                outB = half + std::max( 0.0f, half - clamped );
            }
            if ( d.Type == 1 ) {
                return;
            }

            if ( std::fabs( gripDiff ) <= d.Deadband ) {
                return;
            }
            if ( ratio < d.RatioThreshold ) {
                const float lockT = ( speedA - speedB ) * d.LockCoef;
                outA -= lockT;
                outB += lockT;
                state = 2;
                return;
            }
            state = 3;
        }
        outA = d.Gain * bias * tIn;
        outB = d.Gain * ( 1.0f - bias ) * tIn;
        return;
    }

    if ( d.Type == 4 ) {
        const float lockT = ( speedA - speedB ) * d.LockCoef;
        outA = ( bias * tIn - lockT ) * d.Gain;
        outB = ( ( 1.0f - bias ) * tIn + lockT ) * d.Gain;
        state = 2;
        return;
    }
}

static void ApplyAxleDiff( const PhysicsDifferentialSim &d,
                           float tIn,
                           PhysicsWheelState &wA,
                           PhysicsWheelState &wB ) {
    PhysicsDifferentialSim eff = d;
    if ( gVehicleDiffTypeOverride != 0 ) {
        eff.Type = gVehicleDiffTypeOverride;
        eff.LockCoef = gVehicleDiffLockCoef;
    }
    int32_t state = 0;
    SplitDiff( eff, tIn, wA.NetTorque, wB.NetTorque, wA.AngularVel,
               wB.AngularVel, wA.DriveTorque, wB.DriveTorque, state );
    if ( state == 3 ) {
        const float omega = ( wA.AngularVel + wB.AngularVel ) * 0.5f;
        wA.AngularVel = omega;
        wB.AngularVel = omega;
    }
}

static void StepEngineState( PhysicsEngineSim &e,
                             const PhysicsConfig &config,
                             float dt,
                             bool bAdvanceTimers ) {
    // FUN_0043f660
    if ( !gVehicleEngineStateMachine ) {
        e.State = eEngineState::ES_Running; // debug: always-on
    }

    switch ( e.State ) {
    case eEngineState::ES_Off:
        e.Omega = 0.0f;
        e.TorqueOut = 0.0f;
        e.IgnTimer = bAdvanceTimers ? dt : 0.0f;
        e.bIgnitionOnce = false;
        if ( e.IgnitionOn ) {
            e.State = eEngineState::ES_Crank;
        }
        break;
    case eEngineState::ES_Crank:
        e.TorqueOut = 0.0f;
        if ( !e.IgnitionOn ) {
            e.State = eEngineState::ES_Off;
            break;
        }
        if ( e.CrankDuration < e.IgnTimer ) {
            e.State = eEngineState::ES_Running;
            e.IgnTimer = e.RevLimiterCutTime;
            e.Omega = e.OmegaIdle;
            e.bIgnitionOnce = false;
        }
        if ( bAdvanceTimers ) {
            e.IgnTimer += dt;
        }
        break;
    case eEngineState::ES_Running:
        if ( e.OmegaMax < e.Omega && e.RevLimiterCutTime < e.IgnTimer ) {
            e.IgnTimer = 0.0f;
            e.Omega = e.OmegaMax;
        }
        if ( e.IgnTimer < e.RevLimiterCutTime ) {
            e.Throttle = 0.0f; // also read by the clutch
        }
        e.TorqueOut =
            EngineTorqueAt( e, -1.0f, e.Throttle, e.bApplyIdleThrottleFloor );
        e.IgnTimer += dt;
        if ( e.Omega < kEngineOmegaFloor ) {
            e.Omega = kEngineOmegaFloor;
        }
        if ( e.bIgnitionOnce && gVehicleEngineStateMachine ) {
            e.bIgnitionOnce = false;
            e.State = eEngineState::ES_Stalling;
        }
        break;
    case eEngineState::ES_Stalling:
        e.TorqueOut = EngineTorqueAt( e, -1.0f, 0.0f, false );
        if ( e.Omega < e.OmegaIdle * kEngineStallFrac ) {
            e.State = eEngineState::ES_Off;
        }
        break;
    }

    const float inertia = ( e.TorqueOut <= e.SmoothedTorque )
                              ? config.TorqueInertiaDown
                              : config.TorqueInertiaUp;
    const float netTorque = e.TorqueOut - e.FrictionLoss * e.Omega;
    e.SmoothedTorque += ( netTorque - e.SmoothedTorque ) * ( 1.0f - inertia ) *
                        dt * kTorqueSmoothRate;
}

// DAT_016a6ab8
struct ShiftCandidate {
    int Gear = 0;
    float Force = 0.0f; // torque at WOT * ratio * finalDrive
    float Omega = 0.0f; // engine speed this gear implies at the current road
                        // speed
};

static float ShiftDecisionTorque( const PhysicsEngineSim &e, float omega ) {
    return EngineTorqueAt( e, omega, kShiftDecisionThrottle, false );
}

static float
GearOmegaFromWheel( const PhysicsGearboxSim &g, int gear, float wheelOmega ) {
    const float k = ( gear >= 0 && gear < g.NumGears )
                        ? g.Ratios[gear] * g.FinalDrive
                        : 0.0f;
    return k * wheelOmega;
}

static void StepShiftRevMatch( PhysicsGearboxSim &g,
                               PhysicsEngineSim &e,
                               float targetOmega,
                               float duration,
                               float dt ) {
    const float span = std::max( duration, 1e-6f );
    const float phase =
        Clamp( ( g.ShiftTimer - duration ) * kHalfPI / ( 0.0f - span ), 0.0f,
               kHalfPI );

    const float from = g.ShiftStartOmega;
    float aim = from + ( targetOmega - from ) * std::sin( phase );
    aim = Clamp( aim, std::min( from, targetOmega ),
                 std::max( from, targetOmega ) );

    const float step = kShiftThrottleRate * dt;
    const float throttle = Clamp( ( aim <= e.Omega ) ? g.ShiftThrottle - step
                                                     : g.ShiftThrottle + step,
                                  0.0f, 1.0f );
    e.Throttle = throttle;
    g.ShiftThrottle = throttle;
}

static void RetargetShift( PhysicsGearboxSim &g,
                           const ShiftCandidate *pCandidates,
                           int numCandidates,
                           float midOmega ) {
    for ( int i = 0; i < numCandidates; ++i ) {
        if ( pCandidates[i].Gear >= g.CurrentGear ) {
            continue;
        }
        if ( pCandidates[i].Omega < midOmega ) {
            if ( g.bAutomatic ) {
                g.ShiftSubstate = pCandidates[i].Gear - g.CurrentGear;
            }
            return;
        }
    }
}

static void CommitShift( PhysicsGearboxSim &g,
                         PhysicsEngineSim &e,
                         PhysicsClutchSim &c,
                         bool bUpshift ) {
    g.ShiftState = PhysicsGearboxSim::SS_Settle;
    const bool bInRange = bUpshift
                              ? ( g.CurrentGear < g.NumGears - g.ShiftSubstate )
                              : ( g.CurrentGear + g.ShiftSubstate >= 0 );
    if ( bInRange ) {
        g.CurrentGear += g.ShiftSubstate;
    }
    g.ShiftCompleted = 1.0f;
    c.Target = 1.0f;
    g.ShiftTimer =
        ( bUpshift ? g.UpshiftDelay : g.DownshiftDelay ) * kShiftLockoutFrac;
    if ( !g.bShiftLockout ) {
        g.ShiftTimer = -1.0f;
    }
    e.Throttle = g.ShiftThrottleSaved;
}

static void EvaluateAutoShift( PhysicsVehicleState &v,
                               float brake,
                               float handbrake,
                               float dt,
                               const ShiftCandidate *pCandidates,
                               int numCandidates,
                               float currentForce,
                               float midOmega,
                               float roadOmega,
                               int drivenBase ) {
    // FUN_0043a1d1
    PhysicsEngineSim &e = v.Engine;
    PhysicsGearboxSim &g = v.GearBox;
    const PhysicsClutchSim &c = v.Clutch;

    const int gear = g.CurrentGear;
    const float omega = e.Omega;
    const float redline = e.OmegaMax;

    const float wheelOmega = 0.5f * ( v.Wheels[drivenBase].AngularVel +
                                      v.Wheels[drivenBase + 1].AngularVel );
    const float wheelOverRoad =
        std::fabs( wheelOmega ) / std::max( std::fabs( roadOmega ), 1e-4f );
    g.WheelOverRoad = wheelOverRoad;
    const bool slipOkForUpshift = ( wheelOverRoad < g.UpshiftThreshold );

    const bool bogging =
        ( e.AngAccel < kBogAngAccel ) && ( omega < 0.5f * e.PeakPowerOmega );

    const float upOmega =
        ( 1.0f - g.UpshiftBand ) * e.PeakPowerOmega + g.UpshiftBand * redline;
    const bool nearRedline = ( omega > upOmega ) || ( omega > redline - 1.0f );

    g.ShiftGauge += ( 1.0f - kShiftGaugeRelax ) *
                    ( kShiftGaugeMax - g.ShiftGauge ) * dt * 100.0f;

    g.ShiftBlockReason =
        !nearRedline                      ? PhysicsGearboxSim::SB_BelowBand
        : gear >= g.NumGears - 1          ? PhysicsGearboxSim::SB_TopGear
        : e.Throttle <= 0.1f              ? PhysicsGearboxSim::SB_Throttle
        : handbrake >= kShiftHandbrakeMax ? PhysicsGearboxSim::SB_Handbrake
        : !slipOkForUpshift               ? PhysicsGearboxSim::SB_WheelSlip
        : !( c.State == 3 || gear > 2 )   ? PhysicsGearboxSim::SB_ClutchOpen
                                          : PhysicsGearboxSim::SB_None;

    if ( gear < g.NumGears - 1 && e.Throttle > 0.1f &&
         handbrake < kShiftHandbrakeMax ) {
        if ( e.AngAccel > 0.0f ) { // closing on the upshift point
            g.ShiftGauge =
                Clamp( kShiftGaugeMax -
                           kShiftGaugeMax * omega / std::max( upOmega, 1e-4f ),
                       0.0f, kShiftGaugeMax );
        }
        if ( slipOkForUpshift && nearRedline && ( c.State == 3 || gear > 2 ) &&
             g.bAutomatic ) {
            g.ShiftSubstate = 1;
            g.ShiftState = PhysicsGearboxSim::SS_UpshiftStart;
        }
    }

    if ( g.ShiftState == PhysicsGearboxSim::SS_UpshiftStart ) {
        return;
    }
    if ( !( brake > kShiftBrakeTrigger || e.Throttle < kShiftBrakeTrigger ||
            bogging ) ) {
        return;
    }

    if ( gear < 4 ) {
        if ( gear > 2 ) { // 2nd gear: a plain rpm rule, no torque comparison
            if ( e.AngAccel < 0.0f ) {
                g.ShiftGauge =
                    Clamp( kShiftGaugeMax -
                               kShiftGaugeMax * ( omega - redline ) /
                                   ( g.DownshiftBand * redline - redline ),
                           0.0f, kShiftGaugeMax );
            }
            if ( omega < g.DownshiftBand * redline && g.bAutomatic ) {
                g.ShiftState = PhysicsGearboxSim::SS_DownshiftStart;
                g.ShiftSubstate = -1;
            }
        }
        return;
    }

    if ( e.AngAccel < 0.0f ) {
        const float belowOmega =
            GearOmegaFromWheel( g, gear - 1, v.Wheels[drivenBase].AngularVel );
        g.ShiftGauge =
            Clamp( kShiftGaugeMax - kShiftGaugeMax * ( belowOmega - redline ) /
                                        ( e.PeakPowerOmega - redline ),
                   0.0f, kShiftGaugeMax );
    }

    for ( int i = 0; i < numCandidates; ++i ) {
        if ( pCandidates[i].Gear < gear &&
             pCandidates[i].Force > currentForce &&
             pCandidates[i].Omega < midOmega ) {
            if ( g.bAutomatic ) {
                g.ShiftState = PhysicsGearboxSim::SS_DownshiftStart;
                g.ShiftSubstate = pCandidates[i].Gear - g.CurrentGear;
            }
            return;
        }
    }
}

void StepGearbox( PhysicsVehicleState &v,
                  float brake,
                  float handbrake,
                  float dt ) {
    // FUN_00439e70
    PhysicsEngineSim &e = v.Engine;
    PhysicsGearboxSim &g = v.GearBox;
    PhysicsClutchSim &c = v.Clutch;

    if ( v.NumWheels < 2 ) {
        return;
    }

    int grounded = 0, loaded = 0;
    const float loadThreshold =
        ( v.Mass * kShiftLoadRefG / static_cast<float>( v.NumWheels ) ) *
        kShiftLoadFrac;
    for ( int i = 0; i < v.NumWheels; ++i ) {
        if ( v.Wheels[i].bGroundContact ) {
            ++grounded;
            if ( v.Wheels[i].NormalLoad > loadThreshold ) {
                ++loaded;
            }
        }
    }
    const bool bWheelLift = ( loaded < grounded );

    const int drivenBase =
        Min( v.bRearDrive ? ( v.NumWheels / 2 ) : 0, v.NumWheels - 2 );
    const PhysicsWheelState &wheelA = v.Wheels[drivenBase];
    const PhysicsWheelState &wheelB = v.Wheels[drivenBase + 1];

    // Engine speed the road is asking for, through the driven axle.
    const float roadOmega =
        0.5f * ( ( wheelA.ContactVelLong + wheelB.ContactVelLong ) /
                 std::max( wheelA.Radius, 0.05f ) );

    const int curGear = Clamp( g.CurrentGear, 0, g.NumGears - 1 );
    const float currentForce =
        ShiftDecisionTorque( e, -1.0f ) * g.Ratios[curGear] * g.FinalDrive;
    const float midOmega = 0.5f * ( e.OmegaMax + e.PeakTorqueOmega );

    ShiftCandidate candidates[PhysicsGearboxSim::kMaxGears];
    int numCandidates = 0;
    for ( int cg = 2; cg < g.NumGears; ++cg ) {
        const float k = g.Ratios[cg] * g.FinalDrive;
        ShiftCandidate &cand = candidates[numCandidates++];
        cand.Gear = cg;
        cand.Omega = k * roadOmega;
        cand.Force = ShiftDecisionTorque( e, cand.Omega ) * k;
    }

    switch ( g.ShiftState ) {
    case PhysicsGearboxSim::SS_Settle: {
        g.ShiftGauge = kShiftGaugeMax;
        g.ShiftTimer -= dt;
        g.ReportedGear = g.CurrentGear;
        g.ShiftHalfTime = 0.0f;
        g.ShiftSubstate = 0;
        if ( g.ShiftTimer < 0.0f || g.bAutomatic ) {
            g.ShiftState = PhysicsGearboxSim::SS_InGear;
        }
        return;
    }

    case PhysicsGearboxSim::SS_InGear: {
        g.ReportedGear = g.CurrentGear;
        g.ShiftHalfTime = 0.0f;
        g.ShiftSubstate = 0;

        if ( g.bAutomatic && g.bWheelLiftPrev && !bWheelLift ) {
            g.ShiftGauge = kShiftGaugeMax;
            g.ShiftState = PhysicsGearboxSim::SS_Settle;
            g.ShiftTimer = g.UpshiftDelay * kShiftLockoutFrac;
            g.bWheelLiftPrev = false;
            return;
        }

        if ( g.bAutomatic && !( g.CurrentGear == 1 && !bWheelLift ) ) {
            if ( g.CurrentGear < 1 || c.State != 3 || bWheelLift ) {
                g.ShiftGauge = kShiftGaugeMax;
                g.bWheelLiftPrev = bWheelLift;
                return;
            }
        }

        EvaluateAutoShift( v, brake, handbrake, dt, candidates, numCandidates,
                           currentForce, midOmega, roadOmega, drivenBase );
        g.bWheelLiftPrev = bWheelLift;
        return;
    }

    case PhysicsGearboxSim::SS_UpshiftStart: {
        g.ShiftTimer = g.UpshiftDelay;
        g.ReportedGear = g.CurrentGear;
        g.ShiftHalfTime = g.UpshiftDelay * 0.5f;
        g.ShiftState = PhysicsGearboxSim::SS_Upshifting;
        g.ShiftStartOmega = e.Omega;
        g.ShiftThrottle = e.Throttle;
        g.ShiftThrottleSaved = e.Throttle;
        return;
    }

    case PhysicsGearboxSim::SS_Upshifting: {
        g.ShiftTimer -= dt;
        g.ShiftCompleted = 0.0f;
        c.Target = 0.0f;

        StepShiftRevMatch( g, e,
                           GearOmegaFromWheel( g,
                                               g.CurrentGear + g.ShiftSubstate,
                                               wheelA.AngularVel ),
                           g.UpshiftDelay, dt );

        if ( g.ShiftTimer < 0.0f ) {
            CommitShift( g, e, c, true );
        }
        g.ReportedGear = ( g.ShiftTimer <= g.UpshiftDelay * 0.5f )
                             ? ( g.CurrentGear + g.ShiftSubstate )
                             : 1;
        return;
    }

    case PhysicsGearboxSim::SS_DownshiftStart: {
        g.ReportedGear = g.CurrentGear;
        g.ShiftDuration = ( brake <= kShiftHandbrakeMax )
                              ? g.DownshiftDelay * kDownshiftSlowMult
                              : g.DownshiftDelay;
        g.ShiftTimer = g.ShiftDuration;
        g.ShiftState = PhysicsGearboxSim::SS_Downshifting;
        g.ShiftStartOmega = e.Omega;
        g.ShiftThrottle = e.Throttle;
        g.ShiftThrottleSaved = e.Throttle;
        g.ShiftHalfTime = g.ShiftDuration * 0.5f;
        return;
    }

    case PhysicsGearboxSim::SS_Downshifting: {
        g.ShiftTimer -= dt;
        g.ShiftCompleted = 0.0f;
        c.Target = 0.0f; // @0x0043a8b2 / 0x0043a92b

        if ( g.CurrentGear > 2 ) {
            g.ShiftTimer -= dt;

            StepShiftRevMatch(
                g, e,
                GearOmegaFromWheel( g, g.CurrentGear - 1, wheelA.AngularVel ),
                g.ShiftDuration, dt );
        }

        if ( g.ShiftTimer > g.UpshiftDelay * 0.5f ) {
            RetargetShift( g, candidates, numCandidates, midOmega );
        }
        if ( g.ShiftTimer < 0.0f ) {
            CommitShift( g, e, c, false );
        }
        g.ReportedGear = ( g.ShiftTimer <= g.UpshiftDelay * 0.5f )
                             ? ( g.CurrentGear + g.ShiftSubstate )
                             : 1;
        return;
    }

    default:
        return;
    }
}

static float
EspPeakInterp( const float *table, int N, float maxLoad, float load ) {
    if ( N < 2 ) {
        return 1.0f;
    }
    const float step = maxLoad / static_cast<float>( N );
    int i = static_cast<int>( load / step );
    if ( i > N - 2 ) {
        i = N - 2;
    }
    if ( i < 0 ) {
        i = 0;
    }
    const float xL = i * step;
    const float lo = table[i], hi = table[i + 1];
    const float v = ( ( hi - lo ) * ( load - xL ) ) / step + lo;
    return Clamp( v, std::min( lo, hi ), std::max( lo, hi ) );
}

static float
EspCombinedSlipFactor( const PhysicsTire &tire, float load, float slipArg ) {
    // FUN_0043d0d0
    const int N = tire.LoadTableCount;
    const float peakGrip =
        EspPeakInterp( tire.PeakLong, N, tire.LoadTableMaxLoad, load );
    float peakSlip =
        EspPeakInterp( tire.PeakLat, N, tire.LoadTableMaxLoad, load );
    if ( std::fabs( peakSlip ) < 1.0e-6f ) {
        peakSlip = 1.0f;
    }
    const float r = slipArg / peakSlip;
    float x = 1.0f - r * r;
    if ( x < 0.0f ) {
        x = -x;
    }
    return std::sqrt( x ) * peakGrip;
}

static float WheelAidSlipArg( const PhysicsWheelState &w ) {
    // FUN_0043d080
    const float vLong = w.ContactVelLong;
    const float vLat = w.ContactVelLat;
    if ( vLong > 0.0f ) {
        return std::atan( vLat / vLong );
    }
    return ( vLong < 0.0f ) ? -std::atan( vLat / vLong ) : 0.0f;
}

static float
WheelAidTargetOmega( float slipTerm, float radius, float vLong, float vLat ) {
    // FUN_0043d050
    const float speed = std::sqrt( vLong * vLong + vLat * vLat );
    return ( speed * slipTerm + vLong ) / radius;
}

static float ApplyEngineDragControl( const PhysicsWheelState &w,
                                     bool bEnabled,
                                     float reactTorque,
                                     float drive,
                                     float h ) {
    // FUN_0043e440 (inlined)
    const float vLong = w.ContactVelLong;
    if ( !bEnabled || drive >= 0.0f || w.AngularVel <= 0.0f || vLong <= 1.0f ) {
        return drive;
    }

    const float radius = std::max( w.Radius, 0.05f );
    const float curve =
        EspCombinedSlipFactor( w.Tire, w.NormalLoad, WheelAidSlipArg( w ) );
    const float target = WheelAidTargetOmega( -( curve * 0.5f ), radius, vLong,
                                              w.ContactVelLat );

    const float vClamp = Clamp( std::fabs( vLong ), 0.0f, 1.0f );
    const float effInertia = w.Inertia + w.ReflectedInertia;
    const float candidate =
        ( vClamp * target - w.AngularVel ) * ( effInertia / h ) -
        ( reactTorque + drive );

    if ( candidate <= 0.0f ) {
        return drive;
    }
    return std::min( drive + candidate, 0.0f );
}

static void ApplyBrakeAndABS( PhysicsWheelState &w,
                              bool bAbsEnabled,
                              float absMag,
                              float reactTorque,
                              float drive,
                              float h ) {
    // FUN_0043e440 (inlined)
    const float authority = w.HandbrakeCmdTorque + w.BrakeCmdTorque;

    float targetOmega = 0.0f;
    if ( bAbsEnabled ) {
        const float radius = std::max( w.Radius, 0.05f );
        const float curve =
            EspCombinedSlipFactor( w.Tire, w.NormalLoad, WheelAidSlipArg( w ) );
        const float sOmega = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
        const float vClamp = Clamp( std::fabs( w.ContactVelLong ), 0.0f, 1.0f );
        targetOmega =
            vClamp * WheelAidTargetOmega( -( curve * sOmega * absMag ), radius,
                                          w.ContactVelLong, w.ContactVelLat );
    }

    const float effInertia = w.Inertia + w.ReflectedInertia;
    const float candidate =
        ( targetOmega - w.AngularVel ) * ( effInertia / h ) -
        ( reactTorque + drive );
    w.BrakeTorqueOut = Clamp( candidate, std::min( 0.0f, authority ),
                              std::max( 0.0f, authority ) );
}

static void ApplyESP( PhysicsWheelState &w,
                      bool bEnabled,
                      float espMag,
                      float reactTorque,
                      float drive,
                      float h ) {
    // FUN_0043e440 (inlined)
    w.EspState = 0;
    w.EspTorqueOut = 0.0f;

    const float vLong = w.ContactVelLong;
    const float vLat = w.ContactVelLat;
    if ( !bEnabled || !w.bGroundContact || std::fabs( w.AngularVel ) <= 0.1f ||
         std::fabs( vLong ) <= 0.1f ) {
        w.EspTargetOmega = w.AngularVel;
        return;
    }
    w.EspState = 1;

    const float radius = std::max( w.Radius, 0.05f );
    const float curve =
        EspCombinedSlipFactor( w.Tire, w.NormalLoad, WheelAidSlipArg( w ) );
    const float sOmega = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
    const float rawTarget =
        WheelAidTargetOmega( curve * sOmega * espMag, radius, vLong, vLat );

    constexpr float kEspTargetFilter = 0.0f;
    w.EspTargetOmega = kEspTargetFilter * w.EspTargetOmega +
                       ( 1.0f - kEspTargetFilter ) * rawTarget;

    const float effInertia = w.Inertia + w.ReflectedInertia;
    const float espTorque =
        ( w.EspTargetOmega - w.AngularVel ) * ( effInertia / h ) -
        ( reactTorque + drive );

    if ( drive > 0.0f && w.AngularVel > 0.0f ) {
        if ( espTorque <= 0.0f ) {
            w.EspTorqueOut = espTorque - drive;
        }
    } else if ( drive < 0.0f && w.AngularVel < 0.0f && espTorque >= 0.0f ) {
        w.EspTorqueOut = ( -espTorque < drive ) ? -drive : espTorque;
    }

    if ( std::fabs( w.EspTorqueOut ) > 10.0f ) {
        w.EspState = 2;
    }
}

static int ClampedGear( const PhysicsGearboxSim &g ) {
    return ( g.CurrentGear >= 0 && g.CurrentGear < g.NumGears ) ? g.CurrentGear
                                                                : 1;
}

static void ArmIdleThrottleFloor( PhysicsVehicleState &v ) {
    // FUN_0043aba0 (inlined)
    constexpr float kIdleFloorEngagement = 0.5f; // FLOAT_00f41674
    v.Engine.bApplyIdleThrottleFloor =
        ( v.GearBox.CurrentGear == 1 ) ||
        ( v.Clutch.Engagement < kIdleFloorEngagement );
}

static void
IntegrateEngine( PhysicsEngineSim &e, float coupling, float engage, float h ) {
    // FUN_0043f8d0
    e.Omega +=
        ( ( e.TorqueOut - coupling * engage - e.FrictionLoss * e.Omega ) /
          std::max( e.Inertia, 1.0e-4f ) ) *
        h;
    e.AngAccel = ( e.Omega - e.OmegaPrev ) / h;
    e.OmegaPrev = e.Omega;
    if ( !gVehicleEngineStateMachine && e.Omega < e.OmegaIdle ) {
        e.Omega = e.OmegaIdle;
    }
}

static void StepPowertrainSubstep( PhysicsVehicleState &v,
                                   float brake,
                                   float handbrake,
                                   float h ) {
    // FUN_0043aba0 (inlined)
    PhysicsEngineSim &e = v.Engine;
    PhysicsGearboxSim &g = v.GearBox;
    PhysicsClutchSim &c = v.Clutch;

    StepEngineState( e, *v.pConfig, h, false );
    if ( gVehicleEngineStateMachine && e.State == eEngineState::ES_Crank ) {
        g.CurrentGear = 1;
    }

    const int gearIn = ClampedGear( g );
    const float k = g.Ratios[gearIn] * g.FinalDrive;
    float Idl = g.BaseInertia;
    if ( gearIn != 1 ) {
        const int mode = ( gVehicleDrivelineInertiaMode >= 0 )
                             ? gVehicleDrivelineInertiaMode
                             : g.DrivelineInertiaMode;
        float divisor;
        switch ( mode ) {
        case 1:
            divisor = k * k;
            break;
        case 2:
            divisor = std::sqrt( std::fabs( k ) );
            break;
        default:
            divisor = k;
            break;
        }
        const float numer = 2.0f * v.Wheels[0].Inertia + g.DiffInertia;
        Idl += numer / std::max( std::fabs( divisor ), 0.01f );
    }
    v.DrivelineInertia = Idl;

    float coupling = 0.0f;
    {
        const float coefE = ( gVehicleCouplingCoefE > 0.0f )
                                ? gVehicleCouplingCoefE
                                : v.CouplingCoefE;
        const float coefW = ( gVehicleCouplingCoefW > 0.0f )
                                ? gVehicleCouplingCoefW
                                : v.CouplingCoefW;
        const float invIe = 1.0f / std::max( e.Inertia * coefE, 1e-4f );
        const float invIw = 1.0f / std::max( Idl * coefW, 1e-4f );
        const float We = e.Omega;
        const float Te = e.FrictionLoss * We + e.TorqueOut;
        const float Ww = v.ReflectedWheelSpeed;
        const float Tw = v.ReactionTorque;
        const float denom = ( invIe + invIw ) * h;
        if ( std::fabs( denom ) > 1e-8f ) {
            coupling =
                ( Te > 0.0f )
                    ? ( ( ( We + invIe * Te * h ) - ( Ww + invIw * Tw * h ) ) /
                        denom )
                    : ( ( ( We - invIe * Te * h ) - ( Ww - invIw * Tw * h ) ) /
                        denom );
        }
        if ( gVehicleMaxCoupling > 0.0f ) {
            coupling =
                Clamp( coupling, -gVehicleMaxCoupling, gVehicleMaxCoupling );
        }
    }

    StepGearbox( v, brake, handbrake, h );

    if ( !gVehicleEngineStateMachine || e.State == eEngineState::ES_Running ) {
        StepClutch( c, e, g.CurrentGear, e.Throttle, h );
    } else {
        c.Target = 0.0f;
    }

    StepEngineState( e, *v.pConfig, h, true );

    const int gear = ClampedGear( g );
    const bool neutral = ( gear == 1 );
    const float engage = neutral ? 0.0f : c.Engagement;

    IntegrateEngine( e, coupling, engage, h );

    const float gearboxOut =
        g.Ratios[gear] * g.TransEff * g.FinalDrive * engage * coupling;
    v.CouplingTorque = coupling;
    v.ClutchEngage = engage;
    v.DiffInputTorque = gearboxOut;

    if ( v.bBike ) {
        // Type 1: the gearbox drives the rear wheel directly, and the rear
        // wheel keeps the engine inertia in neutral (through 1st gear).
        const float kRear =
            neutral
                ? ( ( g.NumGears >= 3 ) ? g.Ratios[2] * g.FinalDrive : 0.0f )
                : k;
        for ( int i = 0; i < v.NumWheels; ++i ) {
            PhysicsWheelState &w = v.Wheels[i];
            w.DriveTorque = 0.0f;
            w.ReflectedInertia = w.bDriven ? kRear * kRear * Idl : 0.0f;
            if ( w.bDriven && !neutral ) {
                w.DriveTorque = gearboxOut;
            }
        }
        return;
    }

    float reflectedInertia = k * k * Idl + g.DiffInertia;
    if ( v.bCenterDrive ) {
        reflectedInertia += g.DiffInertia;
    }
    for ( int i = 0; i < v.NumWheels; ++i ) {
        PhysicsWheelState &w = v.Wheels[i];
        w.DriveTorque = 0.0f;
        w.ReflectedInertia =
            ( !neutral && w.bDriven ) ? reflectedInertia : 0.0f;
    }
    if ( !neutral ) {
        if ( v.bCenterDrive ) {
            const float cs = v.DiffCenter.SplitRatio;
            ApplyAxleDiff( v.DiffFront, gearboxOut * cs, v.Wheels[0],
                           v.Wheels[1] );
            ApplyAxleDiff( v.DiffRear, gearboxOut * ( 1.0f - cs ), v.Wheels[2],
                           v.Wheels[3] );
        } else if ( v.bFrontDrive ) {
            ApplyAxleDiff( v.DiffFront, gearboxOut, v.Wheels[0], v.Wheels[1] );
        } else if ( v.bRearDrive ) {
            ApplyAxleDiff( v.DiffRear, gearboxOut, v.Wheels[2], v.Wheels[3] );
        }
    }
}

static void StepWheelSubstep( PhysicsVehicleState &v,
                              int i,
                              const Matrix4x4f_t &vehicleMatrix,
                              const VehicleSolveInput &in,
                              float h ) {
    // FUN_0043aba0 (inlined)
    PhysicsWheelState &w = v.Wheels[i];

    if ( !w.bGroundContact ) {
        w.BrakeCmdTorque = 0.0f;
        w.HandbrakeCmdTorque = 0.0f;
        w.BrakeTorqueOut = 0.0f;
        w.EspTorqueOut = 0.0f;
        w.EspState = 0;
        w.NormLongClamped = 0.0f;
        w.NormLatClamped = 0.0f;
        IntegrateWheel( w, h );
        return;
    }

    const float absMag = ( w.AxleIndex == 0 ) ? v.AbsMagFront : v.AbsMagRear;
    const float espMag = ( w.AxleIndex == 0 ) ? v.EspMagFront : v.EspMagRear;

    const bool bHandbrakeWheel = !w.bSteered;
    const bool bHandbraked =
        bHandbrakeWheel && ( in.Handbrake > kHandbrakeInputThreshold );

    const float brakeSign = ( w.AngularVel > 0.0f ) ? 1.0f : -1.0f;
    w.BrakeCmdTorque = -brakeSign * in.Brake * w.BrakeTorque;
    if ( v.bBike ) {
        w.HandbrakeCmdTorque =
            bHandbrakeWheel ? -brakeSign * in.Handbrake * v.HandbrakeTorque
                            : 0.0f;
        if ( w.HandbrakeCmdTorque > 1.0f ) {
            w.BrakeCmdTorque = 0.0f;
        }
    } else {
        w.HandbrakeCmdTorque =
            bHandbrakeWheel ? -brakeSign * in.Handbrake * v.BrakeTorque : 0.0f;
    }

    ComputeTireForces( w, vehicleMatrix );

    const float driveBase = w.DriveTorque;
    const float reactTorque = ComputeWheelReactionTorque( w, h );

    ApplyBrakeAndABS( w, v.bAbsEnabled && !bHandbraked, absMag, reactTorque,
                      driveBase, h );
    w.DriveTorque =
        ApplyEngineDragControl( w, v.bEdcEnabled, reactTorque, driveBase, h );
    ApplyESP( w, in.bEspEnabled && v.bEspEnabled && w.bDriven, espMag,
              reactTorque, w.DriveTorque, h );

    IntegrateWheel( w, h );
}

static void AxleFeedback( const PhysicsWheelState &wA,
                          const PhysicsWheelState &wB,
                          float axleInput,
                          float &outOmega,
                          float &outReaction ) {
    // FUN_0043aba0 (inlined)
    outReaction = 2.0f * ( ( axleInput <= 0.0f )
                               ? std::min( wA.NetTorque, wB.NetTorque )
                               : std::max( wA.NetTorque, wB.NetTorque ) );
    outOmega = ( wA.AngularVel + wB.AngularVel ) * 0.5f;
}

static void UpdateCouplingFeedback( PhysicsVehicleState &v ) {
    // FUN_0043aba0 (inlined)
    float omega = 0.0f;
    float reaction = 0.0f;
    if ( v.bBike ) {
        const PhysicsWheelState &rear = v.Wheels[v.NumWheels - 1];
        reaction = rear.NetTorque;
        omega = rear.AngularVel;
    } else if ( v.NumWheels >= 4 && v.bCenterDrive ) {
        const float cs = v.DiffCenter.SplitRatio;
        float omegaF, reactF, omegaR, reactR;
        AxleFeedback( v.Wheels[0], v.Wheels[1], v.DiffInputTorque * cs, omegaF,
                      reactF );
        AxleFeedback( v.Wheels[2], v.Wheels[3],
                      v.DiffInputTorque * ( 1.0f - cs ), omegaR, reactR );
        reaction = 2.0f * ( ( v.DiffInputTorque <= 0.0f )
                                ? std::min( reactF, reactR )
                                : std::max( reactF, reactR ) );
        omega = ( omegaF + omegaR ) * 0.5f;
    } else if ( v.bFrontDrive ) {
        AxleFeedback( v.Wheels[0], v.Wheels[1], v.DiffInputTorque, omega,
                      reaction );
    } else if ( v.bRearDrive && v.NumWheels >= 4 ) {
        AxleFeedback( v.Wheels[2], v.Wheels[3], v.DiffInputTorque, omega,
                      reaction );
    }

    const PhysicsGearboxSim &g = v.GearBox;
    const int gear = ClampedGear( g );
    const float k = g.Ratios[gear] * g.FinalDrive;
    v.ReactionTorque =
        ( gear == 1 || std::fabs( k ) < 0.01f ) ? 0.0f : reaction / k;
    v.ReflectedWheelSpeed = k * omega;
}

void StepVehicleSolve( PhysicsVehicleState &v,
                       const Matrix4x4f_t &vehicleMatrix,
                       const VehicleSolveInput &in,
                       float dt ) {
    // FUN_0043aba0
    v.Engine.Throttle = in.Throttle;
    if ( dt <= 0.0f ) {
        return;
    }
    ArmIdleThrottleFloor( v );

    const int numSubsteps = std::max( 1, v.PowertrainSubsteps );
    const float h = dt / static_cast<float>( numSubsteps );
    for ( int s = 0; s < numSubsteps; ++s ) {
        StepPowertrainSubstep( v, in.Brake, in.Handbrake, h );
        for ( int i = 0; i < v.NumWheels; ++i ) {
            StepWheelSubstep( v, i, vehicleMatrix, in, h );
        }
        UpdateCouplingFeedback( v );
    }
}

void StepPowertrain( PhysicsVehicleState &v,
                     float throttle,
                     float brake,
                     float handbrake,
                     float dt ) {
    // FUN_0043aba0
    v.Engine.Throttle = throttle;
    if ( dt <= 0.0f ) {
        return;
    }
    ArmIdleThrottleFloor( v );

    const int numSubsteps = std::max( 1, v.PowertrainSubsteps );
    const float h = dt / static_cast<float>( numSubsteps );
    for ( int s = 0; s < numSubsteps; ++s ) {
        StepPowertrainSubstep( v, brake, handbrake, h );
        UpdateCouplingFeedback( v );
    }
}

float EvalHelpCurve( const float *pX, const float *pY, float x, float yScale ) {
    // FUN_0041f1b0
    float y0, y1, x0, x1;
    if ( x < pX[1] ) {
        y0 = pY[0] * yScale;
        y1 = pY[1] * yScale;
        x0 = pX[0];
        x1 = pX[1];
    } else {
        y0 = pY[1] * yScale;
        y1 = pY[2] * yScale;
        x0 = pX[1];
        x1 = pX[2];
    }
    const float span = x1 - x0;
    const float r = ( std::fabs( span ) > 1.0e-6f )
                        ? ( ( x - x0 ) * ( y1 - y0 ) / span + y0 )
                        : y0;
    return Clamp( r, std::min( y0, y1 ), std::max( y0, y1 ) );
}

static constexpr float kAidNoTarget = 99.0f;         // DAT_00f417e8
static constexpr float kAidMagMin = 0.5f;            // FLOAT_00f41674
static constexpr float kAidMagMax = 5.0f;            // DAT_00f41750
static constexpr float kAidMagParked = 10.0f;        // FLOAT_00f4166c
static constexpr float kAidRearAbsFixed = 100.0f;    // DAT_00f41708
static constexpr float kAidFrontAbsOff = 1.5f;       // DAT_00f416b8
static constexpr float kAidEspMinSpeed = 8.333334f;  // DAT_00fab700
static constexpr float kAidEspErrorGain = 0.1f;      // FLOAT_00f41668
static constexpr float kAidEspErrorScale = 1.0f;     // _DAT_00fab6f4
static constexpr float kAidFilterRate = 20.0f;       // _DAT_00f41bec
static constexpr float kAidClutchFactor = -9.0f;     // _DAT_00f41bf0
static constexpr float kAidLaunchDuration = 2.0f;    // DAT_00fab6f8
static constexpr float kAidLaunchSecureScale = 0.5f; // DAT_00fab6f0
static constexpr float kAidLaunchMaxSteer = 0.5f;    // _DAT_00fab6fc
static constexpr float kAidLiftOffSpeed = 10.0f;     // FLOAT_00f4166c
static constexpr float kAidLiftOffLow = 0.75f;       // DAT_00fab6e4
static constexpr float kAidLiftOffHigh = 1.1f;       // DAT_00fab6e0
static constexpr float kAidBikeLow = 0.75f;          // DAT_00fab6ec
static constexpr float kAidBikeHigh = 1.0f;          // DAT_00fab6e8
static constexpr float kAidAbsMinSpeed = 2.0f;       // DAT_00f416b0
static constexpr float kAidAbsRearScale = 2.0f;      // 0x40000000 push
static constexpr float kAidAiAbsSpeed = 2.777778f;   // DAT_00f41bf4
static constexpr float kAidAiEspSlope = -8.5f;       // _DAT_00f41bf8
static constexpr float kAidAiEspSlopeScale = 0.25f;  // DAT_00f416dc
static constexpr float kAidFrontAbsSpeeds[3] = { 5.555556f, 41.666668f,
                                                 55.555557f }; // DAT_00f41be8..
static constexpr float kAidFrontAbsValues[3] = { 2.0f, 1.0f, 0.75f };

static float ComputeElectronicPowerCoef( const PhysicsVehicleState &v,
                                         bool bSimuMode ) {
    if ( bSimuMode ) {
        return 1.0f;
    }
    return EvalHelpCurve( v.pConfig->ElectronicPowerCoefX,
                          v.pConfig->ElectronicPowerCoefY, v.Engine.PeakTorque,
                          1.0f );
}

void StepElectronics( PhysicsVehicleState &v, const ElectronicsInput &in ) {
    // FUN_0041b920
    const PhysicsConfig &c = *v.pConfig;
    const float speed = in.Speed;
    const int32_t slot = ( !v.bCenterDrive && v.bFrontDrive ) ? 0 : 1;

    const float powerCoef = ComputeElectronicPowerCoef( v, in.bSimuMode );
    if ( v.ElectronicIndex < 0 ) {
        v.ElectronicScale = 0.0f;
    } else {
        const float value = ( v.ElectronicIndex < 9 )
                                ? v.ElectronicValues[v.ElectronicIndex]
                                : 0.0f;
        v.ElectronicScale = ( slot == 0 ) ? 1.0f : value * powerCoef;
    }

    v.AbsMagFront = 1.0f;
    v.bEspEnabled = true;
    v.bAbsEnabled = true;
    v.bEdcEnabled = true;

    if ( v.ElectronicMode == 3 ) {
        v.bEspEnabled = false;
        v.EspMagFront = 1.0f;
        v.EspMagRear = 1.0f;
        v.bAbsEnabled = true;
        v.AbsMagFront = 1.0f;
        v.AbsMagRear = 1.0f;
        return;
    }

    if ( v.ElectronicScale < 0.0f || v.ElectronicMode == 0 ) {
        v.bEspEnabled = false;
        if ( in.bSimuMode ) {
            v.bEdcEnabled = false;
        }
        v.bAbsEnabled = true;
        v.AbsMagFront = kAidFrontAbsOff;
        if ( v.HandbrakeAbsInput > kAidEspErrorGain ) {
            v.AbsMagRear = kAidRearAbsFixed;
        } else {
            v.AbsMagRear = EvalHelpCurve( c.ActiveABSSpeed, c.ActiveABSValue,
                                          speed, kAidAbsRearScale );
        }
        return;
    }

    if ( v.ElectronicMode == 2 ) {
        if ( 1.0f > speed ) {
            v.bEspEnabled = false;
        } else {
            const float mag =
                Clamp( ( speed - 1.0f ) * kAidAiEspSlope * kAidAiEspSlopeScale +
                           kAidMagParked,
                       kAidFrontAbsOff, kAidMagParked );
            v.bEspEnabled = true;
            v.EspMagFront = mag;
            v.EspMagRear = mag;
        }
        v.AbsMagRear =
            ( kAidAiAbsSpeed > speed ) ? kAidMagMax : c.ActiveABSValue[0];
        return;
    }

    if ( v.GearBox.CurrentGear == 0 ) {
        v.bEspEnabled = false;
        v.bAbsEnabled = false;
        v.EspMagRear = kAidMagParked;
        v.AbsMagRear = kAidMagParked;
        return;
    }

    // ESP: front/rear wheel speed imbalance
    const int32_t numWheels = v.NumWheels;
    const int32_t shift = numWheels / 2 - 1;
    const int32_t sideOffset = numWheels / 4;
    const int32_t rearBase = 1 << shift;
    const float sumFront =
        v.Wheels[sideOffset].ContactVelM0 + v.Wheels[0].ContactVelM0;
    const float sumRear = v.Wheels[rearBase + sideOffset].ContactVelM0 +
                          v.Wheels[rearBase].ContactVelM0;
    const float signFront = ( sumFront > 0.0f ) ? 1.0f : -1.0f;
    const float signRear = ( sumRear > 0.0f ) ? 1.0f : -1.0f;
    const float imbalance = ( signFront == signRear ) ? ( sumFront - sumRear )
                                                      : ( sumRear + sumFront );
    v.EspGripDiff = imbalance; // DAT_010d6f0c / DAT_010d6f10 (player only)
    const float absImbalance = std::fabs( imbalance );

    const float threshold =
        EvalHelpCurve( c.TCStraightTriggerAlt, c.ActiveTCTrigger, speed,
                       ( v.ElectronicIndex >= 0 && v.ElectronicIndex < 9 )
                           ? v.ElectronicValues[v.ElectronicIndex]
                           : 0.0f );
    const float gain =
        EvalHelpCurve( c.TCStraightTriggerMid, c.ActiveTCValue, speed, 1.0f );
    v.EspThreshold = threshold;

    const float base = ( v.Wheels[sideOffset * slot + rearBase].NormLong +
                         v.Wheels[slot << shift].NormLong ) *
                       0.5f;

    float espTarget = kAidNoTarget;
    if ( absImbalance > threshold && speed > kAidEspMinSpeed &&
         v.LaunchState == 0 ) {
        const float from = v.bEspIntervening ? v.EspMagRear : base;
        espTarget = Clamp( ( absImbalance - threshold ) * kAidEspErrorGain *
                                   kAidEspErrorScale * -gain +
                               from,
                           kAidMagMin, kAidMagMax );
        v.bEspEnabled = true;
        v.bEspIntervening = true;
    } else {
        v.bEspIntervening = false;
    }

    float slipCap = kAidNoTarget;
    if ( v.LaunchState == 0 ) {
        const int32_t level = v.ElectronicIndex;
        if ( level == 0 && in.bBike ) {
            slipCap = Lerp2Clamped( in.Throttle, 0.0f, 1.0f, kAidBikeLow,
                                    kAidBikeHigh );
        } else if ( !v.bThrottleFloored ) {
            slipCap = Lerp2Clamped( in.Throttle, 0.0f, 1.0f, kAidLiftOffLow,
                                    kAidLiftOffHigh );
            const float absSpeed = std::fabs( speed );
            if ( kAidLiftOffSpeed > absSpeed ) {
                slipCap = Lerp2Clamped( absSpeed, 0.0f, kAidLiftOffSpeed,
                                        kAidMagMax, slipCap );
            }
        } else if ( kAidEspMinSpeed > speed ) {
            espTarget = kAidNoTarget;
            bool bStartLaunch = false;
            if ( level == 0 ) {
                bStartLaunch = ( v.Clutch.State == 3 );
            } else {
                bStartLaunch =
                    std::fabs( in.FilteredSteer ) <= kAidLaunchMaxSteer &&
                    kAidEspMinSpeed * kAidMagMin <= speed;
            }
            if ( bStartLaunch ) {
                v.LaunchState = 1;
                v.LaunchStartMag = base;
                v.LaunchTimer = kAidLaunchDuration;
                if ( level == 0 ) {
                    v.LaunchTimer = kAidLaunchSecureScale * kAidLaunchDuration;
                }
            }
        } else {
            slipCap = EvalHelpCurve( c.TCStraightTrigger, c.TCStraightValue,
                                     speed, v.ElectronicScale );
        }
    } else if ( v.LaunchState == 1 ) {
        if ( v.LaunchTimer > 0.0f ) {
            espTarget = kAidNoTarget;
            const float t =
                Clamp( v.LaunchTimer / kAidLaunchDuration, 0.0f, 1.0f );
            const float endMag = c.TCStraightValue[0] * v.ElectronicScale;
            const float startMag = v.LaunchStartMag;
            const float ramp =
                ( endMag - startMag ) * ( 1.0f - std::sin( t * kHalfPI ) ) +
                startMag;
            slipCap = Clamp( ramp, std::min( startMag, endMag ),
                             std::max( startMag, endMag ) );
            v.LaunchTimer -= in.DeltaTime;
        } else {
            v.LaunchState = 0;
        }
    }

    float target = kAidNoTarget;
    if ( slipCap > espTarget && espTarget != kAidNoTarget ) {
        target = espTarget;
    } else if ( slipCap != kAidNoTarget ) {
        const float clutchFactor =
            Clamp( v.Clutch.Engagement * kAidClutchFactor + kAidMagParked, 1.0f,
                   kAidMagParked );
        target = clutchFactor * slipCap;
    }

    if ( target != kAidNoTarget ) {
        v.EspMagFiltered +=
            ( target - v.EspMagFiltered ) * in.DeltaTime * kAidFilterRate;
        v.bEspEnabled = true;
    } else {
        v.EspMagFiltered = base;
        v.bEspEnabled = false;
    }
    if ( slot == 0 ) {
        v.EspMagFront = v.EspMagFiltered;
    } else {
        v.EspMagRear = v.EspMagFiltered;
    }

    // ABS + lateral brake grip
    if ( !v.bBrakeFloored && std::fabs( speed ) > kAidAbsMinSpeed ) {
        v.AbsMagRear = 1.0f;
        v.AbsMagFront = 1.0f;
        v.Wheels[rearBase].BrakeGripLat = v.Wheels[rearBase].BrakeGripLatFast;
        v.Wheels[rearBase + sideOffset].BrakeGripLat =
            v.Wheels[rearBase + sideOffset].BrakeGripLatFast;
        return;
    }

    v.AbsMagRear = EvalHelpCurve( c.ActiveABSSpeed, c.ActiveABSValue, speed,
                                  kAidAbsRearScale );
    v.AbsMagFront =
        EvalHelpCurve( kAidFrontAbsSpeeds, kAidFrontAbsValues, speed, 1.0f );
    for ( int32_t i = 0; i < numWheels; i++ ) {
        PhysicsWheelState &w = v.Wheels[i];
        const bool bFront = ( w.AxleIndex == 0 );
        const float speedSlow = bFront ? c.LatBrakeFrictionSpeedSlowFront
                                       : c.LatBrakeFrictionSpeedSlowRear;
        const float speedFast = bFront ? c.LatBrakeFrictionSpeedFastFront
                                       : c.LatBrakeFrictionSpeedFastRear;
        w.BrakeGripLat = Lerp2Clamped( speed, speedSlow, speedFast,
                                       w.BrakeGripLatSlow, w.BrakeGripLatFast );
    }
}
