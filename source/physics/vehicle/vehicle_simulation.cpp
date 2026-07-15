#include "shared.h"

#include "vehicle_simulation.h"
#include "core/maths_helper.h"

#include <algorithm>
#include <cmath>

static constexpr float kSlipLongScale = 100.0f; // DAT_00f41708 (inside cf50)
static constexpr float kRadToDeg = 57.29578f;   // DAT_00f41b0c
static constexpr float kLoadToPerMille =
    0.001f; // FLOAT_00f4167c (load -> cf50/ceb0 scale)

static constexpr float kGearboxInertia = 0.05f;
static constexpr float kDiffBaseInertia = 0.05f;
static constexpr float kMaxCoupling = 4000.0f;
static constexpr float kClutchLockBand = 8.0f;  // rad/s
static constexpr float kLaunchSlipBand = 50.0f; // rad/s
static constexpr float kSlipTransmitFrac = 0.8f;
static constexpr float kLaunchRpmFrac = 0.6f;
static constexpr float kShiftRedlineFrac = 0.97f; // force upshift near limiter
static constexpr float kShiftDownRpmFrac =
    0.45f; // downshift below this*redline
static constexpr float kShiftDecisionThrottle = 0.25f; // min throttle for eval
static constexpr float kShiftDuration = 0.30f;         // shift window, secs
static constexpr float kShiftLockout = 0.15f; // post-shift re-shift block, secs
static constexpr float kShiftDeclutch = 0.90f;    // engagement dip at mid-shift
static constexpr float kShiftThrottleCut = 0.80f; // throttle dip at mid-shift

bool gVehicleDiff = true;
static constexpr float kDiffTractionGate = 0.5f; // FLOAT_00f4167c: min |react|
static constexpr float kClutchThrottleArm = 0.05f; // DAT_00f41778
static constexpr float kClutchThrottleDrop = 0.1f; // FLOAT_00f41668
static constexpr float kThousandRpmRad = 104.72f;  // FLOAT_00f41894 (1000rpm)
static constexpr float kClutchDecel = -26.19f;     // clutch decel threshold

bool gVehicleEngineStateMachine = true;
static constexpr float kIgnitionBlipThrottle = 0.35f; // start-blip throttle
static constexpr float kIdleGovGain =
    3.0f; // idle-governor throttle / omega deficit
static constexpr float kEngineStallFrac =
    0.2f; // stall below this*idle (DAT_00f416dc)

static void StepClutch( PhysicsClutchSim &c,
                        const PhysicsEngineSim &e,
                        int gear,
                        float throttle,
                        float dt ) {
    // FUN_00440540
    const bool neutral = ( gear == 1 );
    const float idle = e.OmegaIdle;
    const float omega = e.Omega;

    // launchRPM = clamp(lerp(idleMult*idle, redlineMult*redline, (thr-0.5)*2)).
    const float a = c.LaunchIdleMult * idle;
    const float b = c.LaunchRedlineMult * e.OmegaMax;
    float launchRPM = ( b - a ) * ( throttle - 0.5f ) * 2.0f + a;
    launchRPM = Clamp( launchRPM, std::min( a, b ), std::max( a, b ) );

    if ( neutral ) {
        c.State = 0;
        c.Target = 0.0f;
    } else {
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
                ( launchFloor < omega && kClutchDecel <= dOmega ) ||
                ( launchRPM < omega );
            if ( condA ) {
                if ( dOmega <= decelRef && omega <= e.OmegaMax ) {
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
        default:
            c.State = 0;
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
    w.TireForceLong = PacejkaLongitudinal( tire, loadPM, nLong );
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
    } else {
        w.bTireSlipping = true;

        const float rawLongSlip = w.ContactNormal.dot( w.ForwardDir );

        // Slip angle
        float a;
        if ( vLong <= 0.0f ) {
            a = ( vLong < 0.0f ) ? -std::atan( vLat / vLong ) : 0.0f;
        } else {
            a = std::atan( vLat / vLong );
        }
        a *= kRadToDeg;
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
        if ( 1.0f < std::fabs( w.LatHelpA ) ) { // low-speed / locked scale
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
        ( !w.bGroundContact ) ? w.ContactNormal : w.SpinAxis;
    const float appliedForce = Clamp( diff, -w.SuspMaxLoad, w.SuspMaxLoad );
    w.SuspForce = axis * appliedForce;
    w.SuspForcePoint = w.ContactPoint;
}

void IntegrateWheel( PhysicsWheelState &w, float deltaTime ) {
    // FUN_0043e440
    const float totalInertia = w.Inertia + w.ReflectedInertia;
    const float inertia = ( totalInertia > 1e-6f ) ? totalInertia : 1e-6f;
    const float omega = w.AngularVel;
    float reactTorque = -w.TireForceLong * w.Radius;

    if ( w.Radius > 1e-4f && deltaTime > 0.0f ) {
        const float omegaFree = w.ContactVelLong / w.Radius;
        const float lockTorque =
            ( omegaFree - omega ) * ( 1.0f / deltaTime ) * inertia;
        if ( ( 0.0f < lockTorque && lockTorque < reactTorque ) ||
             ( lockTorque < 0.0f && reactTorque < lockTorque ) ) {
            reactTorque = lockTorque;
        }
    }

    reactTorque += w.DragLong * ( ( omega > 0.0f ) ? 1.0f : -1.0f ) * w.Radius;

    const float helpTorque = w.LatHelpA + w.LatHelpB;
    w.NetTorque = helpTorque + reactTorque;
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

static float
EngineTorqueAt( const PhysicsEngineSim &e, float omega, float throttle ) {
    // FUN_0043f230
    if ( e.CurveCount < 2 ) {
        return 0.0f;
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
    float peakPowerPS = 0.0f, torqueAtPeakPower = 0.0f;
    for ( int i = 0; i < n; ++i ) {
        const float om = pairs[2 * i], tq = pairs[2 * i + 1];
        if ( tq > peakTorque ) {
            peakTorque = tq;
            omegaPeakTorque = om;
            iPeakTorque = i;
        }
        const float powerPS =
            tq * om * 0.0013597f; // DAT_00f41694 = 1/735.5 (W->PS)
        if ( powerPS > peakPowerPS ) {
            peakPowerPS = powerPS;
            torqueAtPeakPower = tq;
        }
    }
    const float normScale =
        ( peakPowerPS > 1e-6f ) ? ( bhp * packBoost ) / peakPowerPS : 1.0f;
    e.PeakTorqueOmega = omegaPeakTorque;
    e.CurveCount = n;
    for ( int i = 0; i < n; ++i ) {
        e.CurveMax[i].Omega = pairs[2 * i];
        e.CurveMax[i].Torque = pairs[2 * i + 1] * normScale;
    }

    const float rho =
        ( peakTorque > 1e-6f ) ? torqueAtPeakPower / peakTorque : 1.0f;
    const float kf = frictionCoef;
    const float brakeFloor = -( kf * torqueAtPeakPower );
    for ( int i = 0; i < n; ++i ) {
        e.CurveMin[i].Omega = e.CurveMax[i].Omega;
        if ( i < iPeakTorque && iPeakTorque > 0 ) {
            float r = -kf * ( 0.25f + 0.75f * (float)i / (float)iPeakTorque );
            r = Clamp( r, -kf, -0.25f * kf );
            e.CurveMin[i].Torque = e.CurveMax[i].Torque * rho * r;
        } else {
            e.CurveMin[i].Torque = brakeFloor;
        }
    }
}

static void SplitDiffOpen( float tIn,
                           float bias,
                           float reactA,
                           float reactB,
                           float &outA,
                           float &outB ) {
    // FUN_004402a0
    const float half = bias * tIn;
    if ( gVehicleDiff && tIn > 0.0f &&
         ( std::fabs( reactA ) >= kDiffTractionGate ||
           std::fabs( reactB ) >= kDiffTractionGate ) ) {
        const float gripA = std::max( 0.0f, -reactA );
        const float gripB = std::max( 0.0f, -reactB );
        const float total = gripA + gripB;
        if ( gripA <= gripB ) { // wheel A lighter -> surplus goes to A
            const float bOut = std::min( half, total );
            outB = bOut;
            outA = half + std::max( 0.0f, half - bOut );
        } else { // wheel B lighter -> surplus goes to B
            const float aOut = std::min( half, total );
            outA = aOut;
            outB = half + std::max( 0.0f, half - aOut );
        }
        return;
    }
    outA = half;
    outB = ( 1.0f - bias ) * tIn;
}

static bool StepEngineState( PhysicsEngineSim &e, float dt ) {
    // FUN_0043f660
    if ( !gVehicleEngineStateMachine ) {
        e.State = eEngineState::ES_Running; // always-on (pre-SM behaviour)
        return true;
    }
    switch ( e.State ) {
    case eEngineState::ES_Off:
        e.Omega = 0.0f;
        e.IgnTimer = 0.0f;
        if ( e.IgnitionOn ) {
            e.State = eEngineState::ES_Crank;
        }
        return false; // no torque
    case eEngineState::ES_Crank:
        if ( !e.IgnitionOn ) {
            e.State = eEngineState::ES_Off;
            return false;
        }
        e.IgnTimer += dt;
        if ( e.IgnTimer >= e.CrankDuration ) { // crank done -> fire
            e.State = eEngineState::ES_RevUp;
            e.Omega = e.OmegaIdle; // snap to idle
            e.IgnTimer = 0.0f;
        }
        return false; // cranking: starter only, no engine torque
    case eEngineState::ES_RevUp:
        e.IgnTimer += dt;
        if ( e.Omega < 0.1f ) {
            e.Omega = 0.1f; // floor (FLOAT_00f41668)
        }
        if ( e.IgnTimer >= e.RevUpDuration ) {
            e.State = eEngineState::ES_Running;
        }
        return true; // firing (start blip)
    case eEngineState::ES_Running:
        if ( e.Omega < e.OmegaIdle * kEngineStallFrac ) {
            e.State = eEngineState::ES_Off; // lugged -> stall (auto re-cranks)
            return false;
        }
        return true;
    }
    return true;
}

void StepPowertrain( PhysicsVehicleState &v, float throttle, float dt ) {
    // FUN_0043aba0
    PhysicsEngineSim &e = v.Engine;
    PhysicsGearboxSim &g = v.GearBox;
    PhysicsClutchSim &c = v.Clutch;
    e.Throttle = throttle;
    if ( dt <= 0.0f ) {
        return;
    }
    const float omega0 = e.Omega;

    float shiftEngageMult = 1.0f;
    float shiftThrottleMult = 1.0f;
    if ( g.ShiftState == 2 ) { // mid-shift
        const float prevTimer = g.ShiftTimer;
        g.ShiftTimer -= dt;
        const float p =
            1.0f - Clamp( g.ShiftTimer / kShiftDuration, 0.0f, 1.0f );
        const float blend = std::sin( 3.14159265f * p ); // 0 -> 1 -> 0 notch
        shiftEngageMult = 1.0f - blend * kShiftDeclutch;
        shiftThrottleMult = 1.0f - blend * kShiftThrottleCut;
        const float half = kShiftDuration * 0.5f;
        if ( prevTimer > half && g.ShiftTimer <= half ) {
            const int tgt = g.CurrentGear + g.ShiftSubstate;
            if ( tgt >= 2 && tgt < g.NumGears ) {
                g.CurrentGear = tgt;
            }
        }
        if ( g.ShiftTimer <= 0.0f ) {
            g.ShiftState = 1;
            g.ShiftTimer = kShiftLockout; // brief re-shift lockout
        }
    } else if ( g.ShiftTimer > 0.0f ) {
        g.ShiftTimer -= dt; // post-shift lockout countdown
    }

    const int gear = ( g.CurrentGear >= 0 && g.CurrentGear < g.NumGears )
                         ? g.CurrentGear
                         : 1;
    const bool neutral = ( gear == 1 );
    const float k = g.Ratios[gear] * g.FinalDrive; // ratio * finalDrive
    const float h = dt; // one substep per OnStep call

    // Engine ignition/idle/stall state machine (sets e.State; zeroes Omega
    // while off/cranking). engineFiring = producing torque (RevUp/Running).
    const bool engineFiring = StepEngineState( e, dt );

    // Throttle to the engine: eased during a shift; a start blip during RevUp;
    // an idle governor at zero throttle in Running (holds idle without a hard
    // clamp, so a severe lug can stall).
    float engThrottle = throttle * shiftThrottleMult;
    if ( gVehicleEngineStateMachine ) {
        if ( e.State == eEngineState::ES_RevUp ) {
            engThrottle = std::max( engThrottle, kIgnitionBlipThrottle );
        } else if ( e.State == eEngineState::ES_Running &&
                    e.Omega < e.OmegaIdle ) {
            const float gov =
                Clamp( ( e.OmegaIdle - e.Omega ) / e.OmegaIdle * kIdleGovGain,
                       0.0f, 1.0f );
            engThrottle = std::max( engThrottle, gov );
        }
    }
    e.TorqueOut =
        engineFiring ? EngineTorqueAt( e, e.Omega, engThrottle ) : 0.0f;

    StepClutch( c, e, gear, throttle, dt );
    float engage = neutral ? 0.0f : c.Engagement;
    engage *= shiftEngageMult;
    if ( !engineFiring ) {
        engage = 0.0f;
    }

    const float wheelBaseI = v.Wheels[0].Inertia;
    const float Idl =
        kGearboxInertia + ( neutral ? 0.0f
                                    : ( 2.0f * wheelBaseI + kDiffBaseInertia ) /
                                          std::max( std::fabs( k ), 0.01f ) );
    v.DrivelineInertia = Idl;

    bool slipping = !neutral && ( c.State != 3 );

    float coupling = 0.0f;
    if ( !neutral ) {
        const float invIe =
            1.0f / std::max( e.Inertia * v.CouplingCoefE, 1e-4f );
        const float invIw = 1.0f / std::max( Idl * v.CouplingCoefW, 1e-4f );
        const float We = e.Omega;
        const float Te = e.FrictionLoss * We + e.TorqueOut;
        const float Ww = v.ReflectedWheelSpeed;
        const float Tw = v.ReactionTorque;
        const float denom = ( invIe + invIw ) * h;
        float impulseCoupling = 0.0f;
        if ( std::fabs( denom ) > 1e-8f ) {
            impulseCoupling =
                ( Te > 0.0f )
                    ? ( ( ( We + invIe * Te * h ) - ( Ww + invIw * Tw * h ) ) /
                        denom )
                    : ( ( ( We - invIe * Te * h ) - ( Ww - invIw * Tw * h ) ) /
                        denom );
        }

        coupling = impulseCoupling;
        coupling = Clamp( coupling, -kMaxCoupling, kMaxCoupling );
    }

    if ( engineFiring ) {
        const float omegaRaw =
            e.Omega +
            h * ( e.TorqueOut - coupling * engage - e.FrictionLoss * e.Omega ) /
                std::max( e.Inertia, 1e-4f );
        e.AngAccel = ( omegaRaw - omega0 ) / dt;
        e.Omega = omegaRaw;
        if ( !gVehicleEngineStateMachine && e.Omega < e.OmegaIdle ) {
            e.Omega = e.OmegaIdle;
        }
        if ( e.Omega > e.OmegaMax ) {
            e.Omega = e.OmegaMax; // rev limiter
        }
    } else {
        e.AngAccel = 0.0f;
    }

    // Drive torque = ratio*transEff*finalDrive*engage*coupling -> diff split.
    const float gearboxOut = k * g.TransEff * engage * coupling;

    // Effective wheel inertia = k^2*Idl + diffBaseI + wheelBaseI (driven).
    const float reflectedInertia =
        neutral ? 0.0f : ( k * k * Idl + kDiffBaseInertia );
    for ( int i = 0; i < v.NumWheels; ++i ) {
        v.Wheels[i].DriveTorque = 0.0f;
        v.Wheels[i].ReflectedInertia =
            v.Wheels[i].bDriven ? reflectedInertia : 0.0f;
    }
    if ( !neutral ) {
        if ( v.bCenterDrive ) {
            const float cs = v.DiffCenter.SplitRatio;
            SplitDiffOpen( v.DiffFront.Gain * gearboxOut * cs,
                           v.DiffFront.SplitRatio, v.Wheels[0].NetTorque,
                           v.Wheels[1].NetTorque, v.Wheels[0].DriveTorque,
                           v.Wheels[1].DriveTorque );
            SplitDiffOpen( v.DiffRear.Gain * gearboxOut * ( 1.0f - cs ),
                           v.DiffRear.SplitRatio, v.Wheels[2].NetTorque,
                           v.Wheels[3].NetTorque, v.Wheels[2].DriveTorque,
                           v.Wheels[3].DriveTorque );
        } else if ( v.bFrontDrive ) {
            SplitDiffOpen( v.DiffFront.Gain * gearboxOut,
                           v.DiffFront.SplitRatio, v.Wheels[0].NetTorque,
                           v.Wheels[1].NetTorque, v.Wheels[0].DriveTorque,
                           v.Wheels[1].DriveTorque );
        } else if ( v.bRearDrive ) {
            SplitDiffOpen( v.DiffRear.Gain * gearboxOut, v.DiffRear.SplitRatio,
                           v.Wheels[2].NetTorque, v.Wheels[3].NetTorque,
                           v.Wheels[2].DriveTorque, v.Wheels[3].DriveTorque );
        }
    }

    if ( g.bAutomatic && g.ShiftState == 1 && g.ShiftTimer <= 0.0f ) {
        if ( c.State == 3 && std::fabs( g.Ratios[gear] ) > 0.01f ) {
            const float ratioCur = g.Ratios[gear];
            const float dThr = std::max( throttle, kShiftDecisionThrottle );
            const float Fcur =
                EngineTorqueAt( e, e.Omega, dThr ) * std::fabs( ratioCur );

            bool shifted = false;
            // Upshift: the next-taller gear pulls at least as hard (crossover),
            // or the engine is at the limiter.
            if ( gear < g.NumGears - 1 && throttle > 0.1f ) {
                const float ratioUp = g.Ratios[gear + 1];
                const float omegaUp = e.Omega * ( ratioUp / ratioCur );
                const float Fup =
                    EngineTorqueAt( e, omegaUp, dThr ) * std::fabs( ratioUp );
                const bool redlineGuard =
                    e.Omega >= e.OmegaMax * kShiftRedlineFrac;
                if ( ( Fup >= Fcur || redlineGuard ) &&
                     omegaUp > e.OmegaIdle ) {
                    g.ShiftState = 2;    // begin the shift window
                    g.ShiftSubstate = 1; // upshift
                    g.ShiftTimer = kShiftDuration;
                    shifted = true;
                }
            }
            // Downshift: a lower gear pulls clearly better (UpshiftThreshold
            // margin = hysteresis so it can't immediately re-upshift), or the
            // engine is lugging below the low-RPM floor.
            if ( !shifted && gear > 2 ) {
                const float ratioDn = g.Ratios[gear - 1];
                const float omegaDn = e.Omega * ( ratioDn / ratioCur );
                const float Fdn =
                    EngineTorqueAt( e, omegaDn, dThr ) * std::fabs( ratioDn );
                const bool lowRpm = e.Omega < e.OmegaMax * kShiftDownRpmFrac;
                if ( ( Fdn > Fcur * g.UpshiftThreshold &&
                       omegaDn < e.OmegaMax * kShiftRedlineFrac ) ||
                     lowRpm ) {
                    g.ShiftState = 2;     // begin the shift window
                    g.ShiftSubstate = -1; // downshift
                    g.ShiftTimer = kShiftDuration;
                }
            }
        }
    }
}
