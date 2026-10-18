#pragma once

class GameObject;

struct AiOrientedBox {
    Eigen::Vector4f Center;
    Eigen::Vector4f Axis[3];
    float HalfExtents[3];

    void setFromMatrix( const Matrix4x4f_t &box );
};

bool TestOrientedBoxesOverlap( const Matrix4x4f_t &boxA,
                               const Matrix4x4f_t &boxB );

// 2D box in the XZ plane (FUN_00909510 layout)
struct AiOrientedBox2D {
    float CenterX;
    float CenterZ;
    float HalfX;
    float HalfZ;
    float AxisXx;
    float AxisXz;
    float AxisZx;
    float AxisZz;

    void setFromObject( GameObject *pObject );
};

bool TestOrientedBoxes2DOverlap( const AiOrientedBox2D &boxA,
                                 const AiOrientedBox2D &boxB );
float ComputeOrientedBoxes2DContactTime( const AiOrientedBox2D &boxA,
                                         const Eigen::Vector4f &velocityA,
                                         const AiOrientedBox2D &boxB,
                                         const Eigen::Vector4f &velocityB,
                                         const float maxTime );
float ComputeObjectsContactTime( GameObject *pObjectA,
                                 GameObject *pObjectB,
                                 const float maxTime );
