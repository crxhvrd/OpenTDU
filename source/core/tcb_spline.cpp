#include "shared.h"
#include "tcb_spline.h"

#include "maths_helper.h"

// Five point Gauss-Legendre quadrature
static constexpr int32_t kNumQuadraturePoints = 5;
static constexpr float kQuadratureNodes[kNumQuadraturePoints] = {
    0.046910077f, 0.23076534f, 0.5f, 0.76923466f, 0.95308992f }; // DAT_00f9c0cc
static constexpr float kQuadratureWeights[kNumQuadraturePoints] = {
    0.11846344f, 0.23931434f, 0.28444445f, 0.23931434f,
    0.11846344f }; // DAT_00f9c0e0

static constexpr float kArcLengthEpsilon = 1.0e-6f; // FLOAT_00f41888
static constexpr int32_t kMaxArcLengthIterations = 0x20;

static Eigen::Vector4f
EvaluatePolygon( const TCBSplinePolygon<Eigen::Vector4f> &polygon,
                 const float t ) {
    // FUN_006d8e20
    return polygon.K[0] +
           ( polygon.K[1] + ( polygon.K[2] + polygon.K[3] * t ) * t ) * t;
}

static Eigen::Vector4f
EvaluatePolygonDerivative( const TCBSplinePolygon<Eigen::Vector4f> &polygon,
                           const float t ) {
    // FUN_006d8d70
    return polygon.K[1] +
           ( polygon.K[2] * kTwo + polygon.K[3] * ( kThree * t ) ) * t;
}

// Arc length of the polygon over [0; tMax].
static float
ComputePolygonLength( const TCBSplinePolygon<Eigen::Vector4f> &polygon,
                      const float tMax = kOne ) {
    // FUN_006d9320
    float length = 0.0f;
    for ( int32_t i = 0; i < kNumQuadraturePoints; i++ ) {
        const Eigen::Vector4f derivative =
            EvaluatePolygonDerivative( polygon, kQuadratureNodes[i] * tMax );
        length += kQuadratureWeights[i] * derivative.norm();
    }
    return length * tMax;
}

template <typename T>
static const TCBSplinePolygon<T> *
FindPolygonAtTime( const TCBSplinePolygon<T> *pPolygons,
                   const int32_t numPolys,
                   const float time,
                   float *pPolygonTime ) {
    // FUN_006d8a50 (inlined)
    if ( time <= pPolygons[0].TMin ) {
        *pPolygonTime = 0.0f;
        return &pPolygons[0];
    }

    if ( pPolygons[numPolys - 1].TMax <= time ) {
        *pPolygonTime = kOne;
        return &pPolygons[numPolys - 1];
    }

    int32_t index = 0;
    while ( index < numPolys && pPolygons[index].TMax <= time ) {
        index++;
    }

    *pPolygonTime =
        ( time - pPolygons[index].TMin ) * pPolygons[index].TInvRange;
    return &pPolygons[index];
}

TCBKeyQuat::TCBKeyQuat()
    : Position( 0.0f ),
      Quat( Eigen::Quaternionf::Identity() ),
      T( 0.0f ),
      C( 0.0f ),
      B( 0.0f ) {
    // FUN_006d8390
}

TCBKeyPos::TCBKeyPos()
    : Position( 0.0f ),
      Vec4( Eigen::Vector4f::Zero() ),
      T( 0.0f ),
      C( 0.0f ),
      B( 0.0f ) {
    // FUN_006d8c20
}

void TCBSplinePosition::initialize( uint32_t param_1 ) {
    // FUN_006d8c60
    MaxNumKeys = param_1;
    pKeys = new TCBKeyPos[param_1];
    NumPolys = 0;
    pPolygons = new TCBSplinePolygon<Eigen::Vector4f>[MaxNumKeys - 1];
    pLengths = new float[MaxNumKeys << 2];
}

void TCBSplinePosition::compute() {
    // FUN_006d9540
    OTDU_ASSERT( NumKeys >= 2 );
    if ( NumKeys < 2 ) {
        NumPolys = 0;
        return;
    }

    int32_t prevIndex = 0;
    int32_t index = 0;
    int32_t nextNextIndex = 1;

    if ( NumKeys == 2 ) {
        NumPolys = 1;
    } else {
        NumPolys = NumKeys - 1;

        computeKey( 0, 0, 2 );

        for ( int32_t i = 2; i <= NumKeys - 2; i++ ) {
            computeKey( i - 2, i - 1, i + 1 );
        }

        prevIndex = NumKeys - 3;
        index = NumKeys - 2;
        nextNextIndex = NumKeys - 1;
    }
    computeKey( prevIndex, index, nextNextIndex );

    pLengths[0] = 0.0f;
    for ( int32_t i = 0; i < NumPolys; i++ ) {
        pLengths[i + 1] = pLengths[i] + ComputePolygonLength( pPolygons[i] );
    }
    TotalLength = pLengths[NumPolys];
}

void TCBSplinePosition::computeKey( int32_t prevIndex,
                                    int32_t index,
                                    int32_t nextNextIndex ) {
    // FUN_006d8fa0
    const int32_t nextIndex = index + 1;

    const TCBKeyPos &prevKey = pKeys[prevIndex];
    const TCBKeyPos &key = pKeys[index];
    const TCBKeyPos &nextKey = pKeys[nextIndex];
    const TCBKeyPos &nextNextKey = pKeys[nextNextIndex];

    const Eigen::Vector4f delta = nextKey.Vec4 - key.Vec4;

    const float adjust = ( ( nextKey.Position - key.Position ) * kTwo ) /
                         ( nextKey.Position - prevKey.Position );
    const float tension = kOne - key.T;
    const float outPrevWeight =
        ( key.C + kOne ) * ( key.B + kOne ) * adjust * tension * kHalf;
    const float outNextWeight =
        ( kOne - key.C ) * ( kOne - key.B ) * adjust * tension * kHalf;

    const float nextAdjust = ( ( nextKey.Position - key.Position ) * kTwo ) /
                             ( nextNextKey.Position - key.Position );
    const float nextTension = kOne - nextKey.T;
    const float inPrevWeight = ( nextKey.B + kOne ) * ( kOne - nextKey.C ) *
                               nextAdjust * nextTension * kHalf;
    const float inNextWeight = ( kOne - nextKey.B ) * ( nextKey.C + kOne ) *
                               nextAdjust * nextTension * kHalf;

    const Eigen::Vector4f outTangent =
        delta * outNextWeight + ( key.Vec4 - prevKey.Vec4 ) * outPrevWeight;
    const Eigen::Vector4f inTangent =
        ( nextNextKey.Vec4 - nextKey.Vec4 ) * inNextWeight +
        delta * inPrevWeight;

    TCBSplinePolygon<Eigen::Vector4f> &polygon = pPolygons[index];
    polygon.K[0] = key.Vec4;
    polygon.K[1] = outTangent;
    polygon.K[2] = delta * kThree - outTangent * kTwo - inTangent;
    polygon.K[3] = delta * kMinusTwo + outTangent + inTangent;

    polygon.TMin = key.Position;
    polygon.TMax = nextKey.Position;
    polygon.TInvRange = kOne / ( nextKey.Position - key.Position );
}

const TCBSplinePolygon<Eigen::Vector4f> *
TCBSplinePosition::findPolygon( float time, float *pPolygonTime ) const {
    // FUN_006d8a50
    return FindPolygonAtTime( pPolygons, NumPolys, time, pPolygonTime );
}

Eigen::Vector4f TCBSplinePosition::evaluate( float time ) const {
    // FUN_008d7650 (inlined)
    float polygonTime = 0.0f;
    const TCBSplinePolygon<Eigen::Vector4f> *pPolygon =
        findPolygon( time, &polygonTime );
    return EvaluatePolygon( *pPolygon, polygonTime );
}

Eigen::Vector4f TCBSplinePosition::evaluatePolygon( int32_t polygonIndex,
                                                    float t ) const {
    return EvaluatePolygon( pPolygons[polygonIndex], t );
}

Eigen::Vector4f
TCBSplinePosition::evaluatePolygonDerivative( int32_t polygonIndex,
                                              float t ) const {
    return EvaluatePolygonDerivative( pPolygons[polygonIndex], t );
}

void TCBSplinePosition::positionAL( float distance,
                                    int32_t *pOutPolygonIndex,
                                    float *pOutPolygonTime ) const {
    // FUN_006d93a0
    if ( distance <= 0.0f ) {
        *pOutPolygonIndex = 0;
        *pOutPolygonTime = 0.0f;
        return;
    }

    if ( TotalLength <= distance ) {
        *pOutPolygonIndex = NumPolys - 1;
        *pOutPolygonTime = kOne;
        return;
    }

    int32_t index = 0;
    while ( index < NumPolys && pLengths[index + 1] < distance ) {
        index++;
    }

    if ( index >= NumPolys ) {
        *pOutPolygonIndex = NumPolys - 1;
        *pOutPolygonTime = kOne;
        return;
    }

    const float segmentTarget = distance - pLengths[index];
    const float segmentLength = pLengths[index + 1] - pLengths[index];
    float polygonTime = segmentTarget / segmentLength;

    for ( int32_t i = 0; i < kMaxArcLengthIterations; i++ ) {
        const float error =
            ComputePolygonLength( pPolygons[index], polygonTime ) -
            segmentTarget;
        if ( std::fabs( error ) <= kArcLengthEpsilon ) {
            break;
        }

        const float speed =
            EvaluatePolygonDerivative( pPolygons[index], polygonTime ).norm();
        polygonTime -= error / speed;
    }

    *pOutPolygonIndex = index;
    *pOutPolygonTime = polygonTime;
}

void TCBSplineRotation::initialize( uint32_t param_1 ) {
    // FUN_006d89b0
    MaxNumKeys = param_1;
    pKeys = new TCBKeyQuat[param_1];
    NumPolys = 0;
    pPolygons = new TCBSplinePolygon<Eigen::Quaternionf>[MaxNumKeys - 1];
}

void TCBSplineRotation::compute() {
    // FUN_006d88b0
    for ( int32_t i = 1; i < NumKeys; i++ ) {
        if ( pKeys[i].Quat.coeffs().dot( pKeys[i - 1].Quat.coeffs() ) < 0.0f ) {
            pKeys[i].Quat.coeffs() *= kMinusOne;
        }
    }

    OTDU_ASSERT( NumKeys >= 2 );
    if ( NumKeys < 2 ) {
        NumPolys = 0;
        return;
    }

    if ( NumKeys == 2 ) {
        NumPolys = 1;
        computeKey( 0, 0, 1 );
        return;
    }

    NumPolys = NumKeys - 1;

    computeKey( 0, 0, 2 );

    for ( int32_t i = 2; i <= NumKeys - 2; i++ ) {
        computeKey( i - 2, i - 1, i + 1 );
    }

    computeKey( NumKeys - 3, NumKeys - 2, NumKeys - 1 );
}

void TCBSplineRotation::computeKey( int32_t prevIndex,
                                    int32_t index,
                                    int32_t nextNextIndex ) {
    // FUN_006d8410
    const int32_t nextIndex = index + 1;

    const TCBKeyQuat &prevKey = pKeys[prevIndex];
    const TCBKeyQuat &key = pKeys[index];
    const TCBKeyQuat &nextKey = pKeys[nextIndex];
    const TCBKeyQuat &nextNextKey = pKeys[nextNextIndex];

    const Eigen::Vector4f logPrev =
        QuaternionLog( prevKey.Quat.conjugate() * key.Quat );
    const Eigen::Vector4f logCurrent =
        QuaternionLog( key.Quat.conjugate() * nextKey.Quat );
    const Eigen::Vector4f logNext =
        QuaternionLog( nextKey.Quat.conjugate() * nextNextKey.Quat );

    const float adjust = ( ( nextKey.Position - key.Position ) * kTwo ) /
                         ( nextKey.Position - prevKey.Position );
    const float tension = kOne - key.T;
    const float outPrevWeight =
        ( key.B + kOne ) * ( key.C + kOne ) * adjust * tension * kHalf;
    const float outNextWeight =
        ( kOne - key.B ) * ( kOne - key.C ) * adjust * tension * kHalf;

    const float nextAdjust = ( ( nextKey.Position - key.Position ) * kTwo ) /
                             ( nextNextKey.Position - key.Position );
    const float nextTension = kOne - nextKey.T;
    const float inPrevWeight = ( kOne - nextKey.C ) * ( nextKey.B + kOne ) *
                               nextAdjust * nextTension * kHalf;
    const float inNextWeight = ( nextKey.C + kOne ) * ( kOne - nextKey.B ) *
                               nextAdjust * nextTension * kHalf;

    const Eigen::Vector4f inTangent =
        logNext * inNextWeight + logCurrent * inPrevWeight;

    const Eigen::Vector4f outLog =
        ( ( logCurrent * outNextWeight + logPrev * outPrevWeight ) -
          logCurrent ) *
        kHalf;
    const Eigen::Vector4f inLog = ( logCurrent - inTangent ) * kHalf;

    TCBSplinePolygon<Eigen::Quaternionf> &polygon = pPolygons[index];
    polygon.K[0] = key.Quat;
    polygon.K[1] = key.Quat * QuaternionExp( outLog );
    polygon.K[2] = nextKey.Quat * QuaternionExp( inLog );
    polygon.K[3] = nextKey.Quat;

    polygon.TMin = key.Position;
    polygon.TMax = nextKey.Position;
    polygon.TInvRange = kOne / ( nextKey.Position - key.Position );
}

const TCBSplinePolygon<Eigen::Quaternionf> *
TCBSplineRotation::findPolygon( float time, float *pPolygonTime ) const {
    // FUN_006d81a0 (inlined)
    return FindPolygonAtTime( pPolygons, NumPolys, time, pPolygonTime );
}

Eigen::Quaternionf TCBSplineRotation::evaluate( float time ) const {
    // FUN_006d81a0
    float polygonTime = 0.0f;
    const TCBSplinePolygon<Eigen::Quaternionf> *pPolygon =
        findPolygon( time, &polygonTime );

    const Eigen::Quaternionf lower =
        SlerpNoFlip( pPolygon->K[0], pPolygon->K[3], polygonTime );
    const Eigen::Quaternionf upper =
        SlerpNoFlip( pPolygon->K[1], pPolygon->K[2], polygonTime );

    const float blend = ( kOne - polygonTime ) * polygonTime;
    return SlerpNoFlip( lower, upper, blend + blend );
}
