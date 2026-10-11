#include "shared.h"
#include "ai_oriented_box.h"

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
