#include "shared.h"

#include "vehicle_simulation.h"
#include "core/maths_helper.h"

#include <algorithm>
#include <cmath>

static constexpr float kSlipLongScale = 100.0f; // DAT_00f41708 (inside cf50)
static constexpr float kRadToDeg = 57.29578f;   // DAT_00f41b0c
static constexpr float kLoadToPerMille =
    0.001f; // FLOAT_00f4167c (load -> cf50/ceb0 scale)

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
    w.TireForceLong = PacejkaLongitudinal( tire, loadPM, rawLongSlip );
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
    const float inertia = ( w.Inertia > 1e-6f ) ? w.Inertia : 1e-6f;
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

void StepPowertrain( PhysicsVehicleState &v, float throttle, float dt ) {
    // FUN_0043aba0
    PhysicsEngineSim &e = v.Engine;
    PhysicsGearboxSim &g = v.GearBox;
    PhysicsClutchSim &c = v.Clutch;
    e.Throttle = throttle;
    if ( dt <= 0.0f ) {
        return;
    }

    const int gear = ( g.CurrentGear >= 0 && g.CurrentGear < g.NumGears )
                         ? g.CurrentGear
                         : 1;
    const bool neutral = ( gear == 1 );
    const float gearRatio = g.Ratios[gear] * g.FinalDrive;

    const float wheelRadius = std::max( v.Wheels[0].Radius, 0.05f );
    v.ReflectedWheelSpeed =
        neutral ? 0.0f : gearRatio * ( v.ForwardSpeed / wheelRadius );

    e.TorqueOut = EngineTorqueAt( e, e.Omega, throttle );

    const bool wantLock =
        !neutral &&
        ( gear > 2 || std::fabs( v.ReflectedWheelSpeed ) > e.OmegaIdle );
    const float targetEngage =
        neutral ? 0.0f : ( ( wantLock || throttle > 0.05f ) ? 1.0f : 0.0f );
    c.Engagement += Clamp( targetEngage - c.Engagement, -c.EngageRate * dt,
                           c.EngageRate * dt );
    c.Engagement = Clamp( c.Engagement, 0.0f, 1.0f );
    const float engage = neutral ? 0.0f : c.Engagement;
    const bool locked = ( engage > 0.98f ) && wantLock;

    if ( locked ) {
        e.Omega = v.ReflectedWheelSpeed;
        if ( e.Omega < e.OmegaIdle * 0.4f )
            e.Omega = e.OmegaIdle * 0.4f;
    } else {
        e.Omega += dt * ( e.TorqueOut * ( 1.0f - engage ) ) / e.Inertia;
        if ( e.Omega < e.OmegaIdle )
            e.Omega = e.OmegaIdle;
    }
    if ( e.Omega > e.OmegaMax )
        e.Omega = e.OmegaMax;

    const float transmitted = e.TorqueOut * engage;
    const float gearboxOut = transmitted * gearRatio * g.TransEff;
    for ( int i = 0; i < v.NumWheels; ++i ) {
        v.Wheels[i].DriveTorque = 0.0f;
    }
    if ( !neutral ) {
        if ( v.bCenterDrive ) {
            const float cs = v.DiffCenter.SplitRatio;
            const float frontT = gearboxOut * cs;
            const float rearT = gearboxOut * ( 1.0f - cs );
            v.Wheels[0].DriveTorque =
                v.DiffFront.Gain * v.DiffFront.SplitRatio * frontT;
            v.Wheels[1].DriveTorque =
                v.DiffFront.Gain * ( 1.0f - v.DiffFront.SplitRatio ) * frontT;
            v.Wheels[2].DriveTorque =
                v.DiffRear.Gain * v.DiffRear.SplitRatio * rearT;
            v.Wheels[3].DriveTorque =
                v.DiffRear.Gain * ( 1.0f - v.DiffRear.SplitRatio ) * rearT;
        } else if ( v.bFrontDrive ) {
            v.Wheels[0].DriveTorque =
                v.DiffFront.Gain * v.DiffFront.SplitRatio * gearboxOut;
            v.Wheels[1].DriveTorque = v.DiffFront.Gain *
                                      ( 1.0f - v.DiffFront.SplitRatio ) *
                                      gearboxOut;
        } else if ( v.bRearDrive ) {
            v.Wheels[2].DriveTorque =
                v.DiffRear.Gain * v.DiffRear.SplitRatio * gearboxOut;
            v.Wheels[3].DriveTorque =
                v.DiffRear.Gain * ( 1.0f - v.DiffRear.SplitRatio ) * gearboxOut;
        }
    }
    v.ReactionTorque = 0.0f;
    for ( int i = 0; i < v.NumWheels; ++i ) {
        PhysicsWheelState &w = v.Wheels[i];
        if ( w.DriveTorque == 0.0f ) {
            continue;
        }
        const float maxTorque = w.GripLong * w.NormalLoad * 1.2f * w.Radius;
        w.DriveTorque = Clamp( w.DriveTorque, -maxTorque, maxTorque );
    }

    if ( g.bAutomatic ) {
        if ( g.ShiftTimer > 0.0f ) {
            g.ShiftTimer -= dt;
        } else if ( e.Omega > e.OmegaMax * 0.92f && throttle > 0.1f &&
                    gear < g.NumGears - 1 ) {
            g.CurrentGear = gear + 1;
            g.ShiftTimer = g.UpshiftDelay;
        } else if ( e.Omega < e.OmegaMax * 0.45f && gear > 2 ) {
            g.CurrentGear = gear - 1;
            g.ShiftTimer = g.DownshiftDelay;
        }
    }
}
