#pragma once

struct AiOrientedBox {
    Eigen::Vector4f Center;
    Eigen::Vector4f Axis[3];
    float HalfExtents[3];

    void setFromMatrix( const Matrix4x4f_t &box );
};

bool TestOrientedBoxesOverlap( const Matrix4x4f_t &boxA,
                               const Matrix4x4f_t &boxB );
