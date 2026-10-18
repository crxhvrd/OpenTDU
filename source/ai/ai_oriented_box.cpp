#include "shared.h"
#include "ai_oriented_box.h"

#include "game/game_object.h"

static constexpr float kParallelAxisCosine = 0.999f; // DAT_00f41e54

void AiOrientedBox::setFromMatrix( const Matrix4x4f_t &box ) {
    // FUN_00908bc0
    Center = box.row( 3 );
    for ( int32_t i = 0; i < 3; i++ ) {
        const Eigen::Vector4f axis = box.row( i );
        const float length = axis.norm();
        Axis[i] = axis / length;
        HalfExtents[i] = length * 0.5f;
    }
}

bool TestOrientedBoxesOverlap( const Matrix4x4f_t &boxA,
                               const Matrix4x4f_t &boxB ) {
    // FUN_00909960
    AiOrientedBox a;
    AiOrientedBox b;
    a.setFromMatrix( boxA );
    b.setFromMatrix( boxB );

    const Eigen::Vector4f delta = b.Center - a.Center;
    const float *pA = a.HalfExtents;
    const float *pB = b.HalfExtents;

    float rotation[3][3];
    float absRotation[3][3];
    float projectedDelta[3];
    bool bParallelAxes = false;
    for ( int32_t i = 0; i < 3; i++ ) {
        for ( int32_t j = 0; j < 3; j++ ) {
            rotation[i][j] = a.Axis[i].dot( b.Axis[j] );
            absRotation[i][j] = std::abs( rotation[i][j] );
            if ( absRotation[i][j] > kParallelAxisCosine ) {
                bParallelAxes = true;
            }
        }

        projectedDelta[i] = a.Axis[i].dot( delta );
        const float radiusB = pB[0] * absRotation[i][0] +
                              pB[1] * absRotation[i][1] +
                              pB[2] * absRotation[i][2];
        if ( std::abs( projectedDelta[i] ) > pA[i] + radiusB ) {
            return false;
        }
    }

    for ( int32_t j = 0; j < 3; j++ ) {
        const float radiusA = pA[0] * absRotation[0][j] +
                              pA[1] * absRotation[1][j] +
                              pA[2] * absRotation[2][j];
        if ( std::abs( b.Axis[j].dot( delta ) ) > radiusA + pB[j] ) {
            return false;
        }
    }

    if ( bParallelAxes ) {
        return true;
    }

    for ( int32_t i = 0; i < 3; i++ ) {
        const int32_t i1 = ( i + 1 ) % 3;
        const int32_t i2 = ( i + 2 ) % 3;
        for ( int32_t j = 0; j < 3; j++ ) {
            const int32_t j1 = ( j + 1 ) % 3;
            const int32_t j2 = ( j + 2 ) % 3;
            const float distance =
                std::abs( projectedDelta[i2] * rotation[i1][j] -
                          projectedDelta[i1] * rotation[i2][j] );
            const float radius =
                pA[i1] * absRotation[i2][j] + pA[i2] * absRotation[i1][j] +
                pB[j1] * absRotation[i][j2] + pB[j2] * absRotation[i][j1];
            if ( distance > radius ) {
                return false;
            }
        }
    }
    return true;
}

bool TestOrientedBoxes2DOverlap( const AiOrientedBox2D &boxA,
                                 const AiOrientedBox2D &boxB ) {
    // FUN_00909510
    const float deltaX = boxA.CenterX - boxB.CenterX;
    const float deltaZ = boxA.CenterZ - boxB.CenterZ;

    const float cosXX =
        fabsf( boxB.AxisXz * boxA.AxisXz + boxA.AxisXx * boxB.AxisXx );
    const float cosZX =
        fabsf( boxA.AxisZz * boxB.AxisXz + boxB.AxisXx * boxA.AxisZx );
    if ( fabsf( deltaX * boxB.AxisXx + deltaZ * boxB.AxisXz ) >
         cosXX * boxA.HalfX + cosZX * boxA.HalfZ + boxB.HalfX ) {
        return false;
    }

    const float cosXZ =
        fabsf( boxB.AxisZz * boxA.AxisXz + boxB.AxisZx * boxA.AxisXx );
    const float cosZZ =
        fabsf( boxA.AxisZz * boxB.AxisZz + boxB.AxisZx * boxA.AxisZx );
    if ( fabsf( deltaZ * boxB.AxisZz + deltaX * boxB.AxisZx ) >
         cosXZ * boxA.HalfX + cosZZ * boxA.HalfZ + boxB.HalfZ ) {
        return false;
    }

    if ( fabsf( deltaX * boxA.AxisXx + deltaZ * boxA.AxisXz ) >
         cosXZ * boxB.HalfZ + cosXX * boxB.HalfX + boxA.HalfX ) {
        return false;
    }

    if ( fabsf( deltaZ * boxA.AxisZz + deltaX * boxA.AxisZx ) >
         cosZZ * boxB.HalfZ + cosZX * boxB.HalfX + boxA.HalfZ ) {
        return false;
    }
    return true;
}

void AiOrientedBox2D::setFromObject( GameObject *pObject ) {
    // FUN_0090aa20 (inlined)
    const Eigen::Vector4f &dimensions = pObject->getDimensions();
    const Matrix4x4f_t &world = pObject->getWorldMatrix();
    CenterX = world( 3, 0 );
    CenterZ = world( 3, 2 );
    HalfX = dimensions.x() * 0.5f;
    HalfZ = dimensions.z() * 0.5f;
    AxisXx = world( 0, 0 );
    AxisXz = world( 0, 2 );
    AxisZx = world( 2, 0 );
    AxisZz = world( 2, 2 );
}

static bool SweepIntervals( const float radiusA,
                            const float centerB,
                            const float radiusB,
                            const float speed,
                            const float maxTime,
                            float &tFirst,
                            float &tLast ) {
    // FUN_0090a5d0 (inlined)
    const float minB = centerB - radiusB;
    const float maxB = centerB + radiusB;
    if ( -radiusA > maxB ) {
        if ( speed <= 0.0f ) {
            return false;
        }
        const float invSpeed = 1.0f / speed;
        tFirst = std::max( tFirst, ( -radiusA - maxB ) * invSpeed );
        if ( tFirst > maxTime ) {
            return false;
        }
        tLast = std::min( tLast, ( radiusA - minB ) * invSpeed );
    } else if ( minB > radiusA ) {
        if ( speed >= 0.0f ) {
            return false;
        }
        const float invSpeed = 1.0f / speed;
        tFirst = std::max( tFirst, ( radiusA - minB ) * invSpeed );
        if ( tFirst > maxTime ) {
            return false;
        }
        tLast = std::min( tLast, ( -radiusA - maxB ) * invSpeed );
    } else if ( speed > 0.0f ) {
        tLast = std::min( tLast, ( radiusA - minB ) / speed );
    } else if ( speed < 0.0f ) {
        tLast = std::min( tLast, ( -radiusA - maxB ) / speed );
    } else {
        return true;
    }
    return tFirst <= tLast;
}

float ComputeOrientedBoxes2DContactTime( const AiOrientedBox2D &boxA,
                                         const Eigen::Vector4f &velocityA,
                                         const AiOrientedBox2D &boxB,
                                         const Eigen::Vector4f &velocityB,
                                         const float maxTime ) {
    // FUN_0090a5d0
    static constexpr float kStaticVelocity = 1e-5f;    // DAT_00f41710
    static constexpr float kExitTimeMargin = 10000.0f; // DAT_00f416c4

    const Eigen::Vector4f relativeVelocity = velocityA - velocityB;
    if ( ( relativeVelocity.array().square() <=
           kStaticVelocity * kStaticVelocity )
             .all() ) {
        return TestOrientedBoxes2DOverlap( boxA, boxB ) ? 0.0f : -1.0f;
    }

    const float axesA[2][2] = {
        {boxA.AxisXx, boxA.AxisXz},
        {boxA.AxisZx, boxA.AxisZz}
    };
    const float axesB[2][2] = {
        {boxB.AxisXx, boxB.AxisXz},
        {boxB.AxisZx, boxB.AxisZz}
    };
    const float extentsA[2] = { boxA.HalfX, boxA.HalfZ };
    const float extentsB[2] = { boxB.HalfX, boxB.HalfZ };

    const float deltaX = boxB.CenterX - boxA.CenterX;
    const float deltaZ = boxB.CenterZ - boxA.CenterZ;
    const float speedX = velocityB.x() - velocityA.x();
    const float speedZ = velocityB.z() - velocityA.z();
    float tFirst = 0.0f;
    float tLast = maxTime + kExitTimeMargin;

    float absC[2][2];
    for ( int32_t i = 0; i < 2; i++ ) {
        const float *pAxis = axesA[i];
        absC[i][0] = fabsf( axesB[0][0] * pAxis[0] + axesB[0][1] * pAxis[1] );
        absC[i][1] = fabsf( axesB[1][0] * pAxis[0] + axesB[1][1] * pAxis[1] );
        const float center = pAxis[1] * deltaZ + pAxis[0] * deltaX;
        const float speed = pAxis[1] * speedZ + pAxis[0] * speedX;
        const float radiusB =
            absC[i][0] * extentsB[0] + absC[i][1] * extentsB[1];
        if ( !SweepIntervals( extentsA[i], center, radiusB, speed, maxTime,
                              tFirst, tLast ) ) {
            return -1.0f;
        }
    }

    for ( int32_t j = 0; j < 2; j++ ) {
        const float *pAxis = axesB[j];
        const float radiusA =
            absC[0][j] * extentsA[0] + absC[1][j] * extentsA[1];
        const float center = deltaX * pAxis[0] + pAxis[1] * deltaZ;
        const float speed = speedX * pAxis[0] + pAxis[1] * speedZ;
        if ( !SweepIntervals( radiusA, center, extentsB[j], speed, maxTime,
                              tFirst, tLast ) ) {
            return -1.0f;
        }
    }
    return tFirst;
}

float ComputeObjectsContactTime( GameObject *pObjectA,
                                 GameObject *pObjectB,
                                 const float maxTime ) {
    // FUN_0090aa20
    AiOrientedBox2D boxA;
    boxA.setFromObject( pObjectA );
    AiOrientedBox2D boxB;
    boxB.setFromObject( pObjectB );

    const Eigen::Vector4f velocityA = pObjectA->getLinearVelocity();
    const Eigen::Vector4f velocityB = pObjectB->getLinearVelocity();
    const float contactTime = ComputeOrientedBoxes2DContactTime(
        boxA, velocityA, boxB, velocityB, maxTime );
    if ( contactTime < 0.0f ) {
        return contactTime;
    }

    Eigen::Vector4f accelerationA;
    pObjectA->retrieveAcceleration( accelerationA );
    Eigen::Vector4f accelerationB;
    pObjectB->retrieveAcceleration( accelerationB );
    const Eigen::Vector4f predictedA =
        velocityA + accelerationA * contactTime * 0.5f;
    const Eigen::Vector4f predictedB =
        velocityB + accelerationB * contactTime * 0.5f;
    if ( ComputeOrientedBoxes2DContactTime( boxA, predictedA, boxB, predictedB,
                                            maxTime ) < 0.0f ) {
        return -1.0f;
    }
    return contactTime;
}
