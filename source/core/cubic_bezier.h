#pragma once

// Cubic Bezier helper.
struct CubicBezier {
    Eigen::Vector4f ControlPoints[4];

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
