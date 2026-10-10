#pragma once

struct QuadraticBezier {
    Eigen::Vector4f ControlPoints[3];

    Eigen::Vector4f evaluate( float t ) const {
        // FUN_0043c910
        const float u = 1.0f - t;
        return ControlPoints[0] * ( u * u ) +
               ControlPoints[1] * ( u * t * 2.0f ) +
               ControlPoints[2] * ( t * t );
    }
};

// Cubic Bezier helper.
struct CubicBezier {
    Eigen::Vector4f ControlPoints[4];

    QuadraticBezier computeDerivative() const {
        // FUN_0043c5d0
        QuadraticBezier derivative;
        derivative.ControlPoints[0] = ControlPoints[1] - ControlPoints[0];
        derivative.ControlPoints[1] = ControlPoints[2] - ControlPoints[1];
        derivative.ControlPoints[2] = ControlPoints[3] - ControlPoints[2];
        return derivative;
    }

    bool findClosestPointXZ( const QuadraticBezier &derivative,
                             const Eigen::Vector4f &point,
                             float &outT,
                             Eigen::Vector4f &outPosition,
                             const int32_t numIterations,
                             const float epsilon,
                             float t ) const {
        // FUN_0043c9c0
        constexpr float kMinCosine = 1e-4f;       // DAT_00f41760
        constexpr float kMaxStep = 0.1f;          // FLOAT_00f41668
        constexpr float kMinFalsePosition = 0.1f; // FLOAT_00f41668
        constexpr float kMaxFalsePosition = 0.9f; // DAT_00f416a0

        float lowerT = 0.0f;
        float lowerSlope = 0.0f;
        float upperT = 1.0f;
        float upperSlope = 0.0f;
        for ( int32_t i = 0; i < numIterations; i++ ) {
            if ( t < lowerT ) {
                t = lowerT;
            }
            if ( upperT < t ) {
                t = upperT;
            }

            const float u = 1.0f - t;
            const float cubicU = u * u * u;
            const float middle = t * 3.0f * u;
            const float middleT = middle * t;
            const float middleU = middle * u;
            const float cubicT = t * t * t;
            outPosition.x() = ControlPoints[2].x() * middleT +
                              ControlPoints[1].x() * middleU +
                              cubicT * ControlPoints[3].x() +
                              cubicU * ControlPoints[0].x();
            outPosition.z() = ControlPoints[2].z() * middleT +
                              ControlPoints[1].z() * middleU +
                              cubicT * ControlPoints[3].z() +
                              cubicU * ControlPoints[0].z();
            outPosition.y() = 0.0f;
            outPosition.w() = 1.0f;

            const float tangentWeight = t * 2.0f * u;
            const float tangentX =
                derivative.ControlPoints[0].x() * u * u +
                derivative.ControlPoints[2].x() * t * t +
                tangentWeight * derivative.ControlPoints[1].x();
            const float tangentZ =
                derivative.ControlPoints[2].z() * t * t +
                derivative.ControlPoints[1].z() * tangentWeight +
                u * u * derivative.ControlPoints[0].z();

            const Eigen::Vector4f delta = point - outPosition;
            const float tangentLengthSq =
                tangentX * tangentX + tangentZ * tangentZ;
            const float deltaLengthSq = delta.squaredNorm();
            if ( deltaLengthSq < epsilon || tangentLengthSq < epsilon ) {
                outT = t;
                return true;
            }

            const float slope =
                ( delta.x() * tangentX + delta.z() * tangentZ ) /
                tangentLengthSq;
            if ( std::abs( ( tangentLengthSq * slope ) /
                           std::sqrt( deltaLengthSq * tangentLengthSq ) ) <
                 kMinCosine ) {
                outT = t;
                return true;
            }

            float stepScale = 1.0f;
            if ( 0.0f < slope && lowerT <= t ) {
                if ( lowerSlope != 0.0f && slope + epsilon < lowerSlope ) {
                    stepScale = ( t - lowerT ) / ( lowerSlope - slope );
                }
                lowerT = t;
                lowerSlope = slope;
                if ( 1.0f <= t ) {
                    outT = 1.0f;
                    outPosition.x() = ControlPoints[3].x();
                    outPosition.z() = ControlPoints[3].z();
                    return false;
                }
            }
            if ( slope < 0.0f && t <= upperT ) {
                if ( upperSlope != 0.0f && upperSlope < slope - epsilon ) {
                    stepScale = ( upperT - t ) / ( slope - upperSlope );
                }
                upperT = t;
                upperSlope = slope;
                if ( t <= 0.0f ) {
                    outT = 0.0f;
                    outPosition.x() = ControlPoints[0].x();
                    outPosition.z() = ControlPoints[0].z();
                    return false;
                }
            }

            if ( upperSlope * lowerSlope == 0.0f ) {
                float step = stepScale * slope;
                if ( kMaxStep < std::abs( step ) ) {
                    step = ( ( step <= 0.0f ) ? -1.0f : 1.0f ) * kMaxStep;
                }
                t = step + t;
            } else {
                float ratio = lowerSlope / ( lowerSlope - upperSlope );
                if ( ratio < kMinFalsePosition ) {
                    ratio = kMinFalsePosition;
                } else if ( kMaxFalsePosition < ratio ) {
                    ratio = kMaxFalsePosition;
                }
                t = ( upperT - lowerT ) * ratio + lowerT;
            }
        }

        outT = t;
        return true;
    }

    Eigen::Vector4f evaluate( float t ) const {
        // FUN_0043c850
        const float u = 1.0f - t;
        const float middle = t * 3.0f * u;
        return ControlPoints[0] * ( u * u * u ) +
               ControlPoints[1] * ( middle * u ) +
               ControlPoints[2] * ( middle * t ) +
               ControlPoints[3] * ( t * t * t );
    }

    Eigen::Vector4f evaluateTangent( float t ) const {
        // FUN_0043c6e0
        const float u = 1.0f - t;
        const Eigen::Vector4f derivative =
            ( ControlPoints[1] - ControlPoints[0] ) * ( u * u ) +
            ( ControlPoints[2] - ControlPoints[1] ) * ( 2.0f * u * t ) +
            ( ControlPoints[3] - ControlPoints[2] ) * ( t * t );

        const float length = derivative.head<3>().norm();
        if ( length <= 0.0f ) {
            return Eigen::Vector4f::Zero();
        }
        return Eigen::Vector4f( derivative.x() / length,
                                derivative.y() / length,
                                derivative.z() / length, 0.0f );
    }

    float computeLength( float step ) const {
        // FUN_0043c4f0
        Eigen::Vector4f previous = evaluate( 0.0f );
        float length = 0.0f;
        float t = 0.0f;
        do {
            t += step;
            const Eigen::Vector4f current = evaluate( t );
            length += ( current - previous ).norm();
            previous = current;
        } while ( t < 1.0f );
        return length;
    }
};
