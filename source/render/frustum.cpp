#include "shared.h"
#include "frustum.h"

#include "core/maths_helper.h"

double DOUBLE_00facb40 = 0.0; // DAT_00facb40
double DOUBLE_00facb38 = 0.0; // DAT_00facb38

Frustum::Frustum()
    : zLimits( 1.0f, 5000.0f, 0.0f, 0.0f ),
      zProjection( 1.0f, 5000.0f, 0.0f, 0.0f ),
      projectionMatrix( Matrix4x4f_t::Identity() ),
      projectionWMatrix( Matrix4x4f_t::Identity() ),
      orthoProjectionMatrix( Matrix4x4f_t::Identity() ),
      pCamera( nullptr ),
      wProjection( 0.0f ),
      orthoWidth( 1.0f ),
      orthoHeight( 1.0f ),
      offsetX( 0.0f ),
      offsetY( 0.0f ),
      bIs2DProjection( false ) {
    // FUN_0050bd80
    updateProjectionMatrices();

    DOUBLE_00facb40 = 0.0;
    DOUBLE_00facb38 = 0.0;
}

Frustum::~Frustum() {}

void Frustum::updateProjectionMatrices() {
    // FUN_00404450
    Matrix4x4f_t local_90 = Matrix4x4f_t::Identity();
    local_90.row( 0 )[3] = (float)DOUBLE_00facb38;
    local_90.row( 1 )[3] = (float)DOUBLE_00facb40;

    projectionMatrix = Matrix4x4f_t::Identity();
    projectionMatrix.row( 2 )[3] = -1.0f;
    projectionMatrix.row( 3 )[3] = 0.0f;

    orthoProjectionMatrix = Matrix4x4f_t::Identity();
    orthoProjectionMatrix.row( 0 )[0] = 2.0f / orthoWidth;
    orthoProjectionMatrix.row( 1 )[1] = 2.0f / orthoHeight;

    float fVar1 = zProjection.x();
    float fVar7 = zProjection.y();
    float fVar9 = fVar1 - fVar7;
    float fVar8 = 1.0f / fVar9;
    fVar7 = fVar8 * fVar7;

    orthoProjectionMatrix.row( 2 )[2] = fVar8;
    orthoProjectionMatrix.row( 3 )[2] = ( -1.0f / fVar9 ) * fVar1;

    projectionMatrix.row( 2 )[2] = fVar7;
    projectionMatrix.row( 3 )[2] = fVar7 * fVar1;

    if ( pCamera != nullptr ) {
        float fVar6 = tan( pCamera->getFOV() * 0.5f );

        projectionMatrix.row( 0 )[0] =
            ( 1.0f / fVar6 ) / pCamera->getAspectRatio();
        projectionMatrix.row( 1 )[1] = ( 1.0f / fVar6 );
    }

    projectionMatrix = projectionMatrix * local_90;
}

void Frustum::setPlanes( float nearPlane, float farPlane ) {
    zLimits.x() = nearPlane;
    zProjection.x() = nearPlane;
    zLimits.y() = farPlane;
    zProjection.y() = farPlane;

    updateProjectionMatrices();
}

void Frustum::setCamera( Camera *param_2, bool param_3 ) {
    pCamera = param_2;
    bIs2DProjection = param_3;
}

void Frustum::setOrthoDimensions( float width, float height ) {
    orthoWidth = width;
    orthoHeight = height;

    orthoProjectionMatrix.col( 0 )[0] = 2.0f / orthoWidth;
    orthoProjectionMatrix.col( 1 )[1] = 2.0f / orthoHeight;
}

int32_t Frustum::testBoundingSphere( Eigen::Vector4f &param_2, float param_3 ) {
    // FUN_0050b980
    float fVar2 = 0.0f;
    float fVar3 = 0.0f;
    float fVar4 = 0.0f;
    float fVar5 = 0.0f;
    float fVar6 = 0.0f;
    float fVar7 = 0.0f;

    if ( !bIs2DProjection ) {
        fVar2 = 0.0f - param_2.z();
        fVar3 = abs( param_2.x() ) * pCamera->getInvAspectRatio();
        fVar6 = abs( param_2.y() ) * pCamera->getInvAspectRatio();
        fVar4 = abs( param_2.z() );
        if ( ( zLimits.x() <= fVar2 + param_3 ) &&
             ( fVar2 - param_3 <= zLimits.y() ) ) {
            float fVar5 = pCamera->getCosHalfFov() * fVar6 -
                          pCamera->getSinHalfFov() * fVar4;
            fVar2 = pCamera->getTanHalfFov() * fVar4;
            fVar4 = pCamera->getCosHalfFov() * fVar3 -
                    pCamera->getSinHalfFov() * fVar4;
            if ( ( fVar2 <= fVar3 ) || ( fVar2 <= fVar6 ) ) {
                if ( param_3 < fVar4 ) {
                    return 0;
                }
                if ( param_3 < fVar5 ) {
                    return 0;
                }
            } else if ( ( param_3 < abs( fVar5 ) ) &&
                        ( param_3 < abs( fVar4 ) ) ) {
                return 1;
            }
            return 2;
        }
    } else {
        fVar3 = orthoWidth * 0.5f;
        fVar4 = orthoHeight * 0.5f;
        fVar6 = param_2.x() + param_3;
        fVar2 = param_3 + param_2.y();
        fVar5 = param_2.x() - param_3;
        fVar7 = param_2.y() - param_3;
        if ( ( ( ( 0.0f - fVar3 <= fVar6 ) && ( 0.0f - fVar4 <= fVar2 ) ) &&
               ( fVar5 <= fVar3 ) ) &&
             ( fVar7 <= fVar4 ) ) {
            if ( fVar3 < fVar6 ) {
                return 2;
            }
            if ( fVar4 < fVar2 ) {
                return 2;
            }
            if ( fVar5 < 0.0 - fVar3 ) {
                return 2;
            }
            if ( fVar7 < 0.0 - fVar4 ) {
                return 2;
            }
            return 1;
        }
    }

    return 0;
}

void Frustum::adjustProjectionMatrix( float param_1, float param_2 ) {
    projectionMatrix.row( 0 )[0] = param_1;
    projectionMatrix.row( 1 )[1] = param_2;
}

const Matrix4x4f_t &Frustum::getProjectionMatrix() const {
    return projectionMatrix;
}

const Matrix4x4f_t &Frustum::getOrthoProjectionMatrix() const {
    return orthoProjectionMatrix;
}

void Frustum::setIs2DFrustum( const bool param_1 ) {
    bIs2DProjection = param_1;
}

void ProjectBoundingBoxToScreenRect( Eigen::Vector4f &outRect,
                                     const Matrix4x4f_t &boundingBox,
                                     const Matrix4x4f_t &worldToView,
                                     const Frustum &frustum ) {
    // FUN_006d0f70
    static constexpr int32_t kNumCorners = 8;
    static constexpr int32_t kNumEdges = 12;
    static constexpr int32_t kMaxClippedPoints = kNumEdges * 3;

    static const Eigen::Vector4f kUnitBoxCorners[kNumCorners] = {
        Eigen::Vector4f( -0.5f, -0.5f, -0.5f, 1.0f ),
        Eigen::Vector4f( 0.5f, -0.5f, -0.5f, 1.0f ),
        Eigen::Vector4f( 0.5f, 0.5f, -0.5f, 1.0f ),
        Eigen::Vector4f( -0.5f, 0.5f, -0.5f, 1.0f ),
        Eigen::Vector4f( -0.5f, -0.5f, 0.5f, 1.0f ),
        Eigen::Vector4f( 0.5f, -0.5f, 0.5f, 1.0f ),
        Eigen::Vector4f( 0.5f, 0.5f, 0.5f, 1.0f ),
        Eigen::Vector4f( -0.5f, 0.5f, 0.5f, 1.0f ),
    };

    static constexpr int32_t kBoxEdges[kNumEdges][2] = {
        {0, 3},
        {3, 2},
        {2, 1},
        {1, 0},
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4},
        {5, 1},
        {2, 6},
        {0, 4},
        {7, 3},
    };

    const Matrix4x4f_t boxToView = boundingBox * worldToView;

    Eigen::Vector4f viewCorners[kNumCorners];
    for ( int32_t i = 0; i < kNumCorners; i++ ) {
        viewCorners[i] = TDUMat4x4MulVec( boxToView, kUnitBoxCorners[i] );
    }

    const float nearPlane = frustum.getZProjection().x();

    TDUPlane clipPlane;
    clipPlane.Normal = Eigen::Vector4f( 0.0f, 0.0f, -1.0f, 0.0f );
    clipPlane.DistanceFromOrigin = nearPlane;

    Eigen::Vector4f clippedPoints[kMaxClippedPoints];
    int32_t numPoints = 0;
    for ( int32_t edge = 0; edge < kNumEdges; edge++ ) {
        Eigen::Vector4f &pointA = viewCorners[kBoxEdges[edge][0]];
        Eigen::Vector4f &pointB = viewCorners[kBoxEdges[edge][1]];
        pointA.w() = 1.0f;
        pointB.w() = 1.0f;

        const float depthA = pointA.z() + nearPlane;
        const float depthB = pointB.z() + nearPlane;
        if ( depthA > 0.0f && depthB > 0.0f ) {
            continue;
        }

        if ( depthA <= 0.0f ) {
            clippedPoints[numPoints++] = pointA;
        }
        if ( depthB <= 0.0f ) {
            clippedPoints[numPoints++] = pointB;
        }

        Eigen::Vector4f intersection;
        if ( IntersectSegmentWithPlane( pointB, clipPlane, intersection,
                                        pointA ) ) {
            clippedPoints[numPoints++] = intersection;
        }
    }

    for ( int32_t i = 0; i < numPoints; i++ ) {
        clippedPoints[i] =
            TDUMat4x4MulVec( frustum.getProjectionMatrix(), clippedPoints[i] );
        clippedPoints[i] /= clippedPoints[i].w();
    }

    float minX = 100000.0f;  // FLOAT_00f418e0
    float minY = 100000.0f;  // FLOAT_00f418e0
    float maxX = -100000.0f; // DAT_00f418dc
    float maxY = -100000.0f; // DAT_00f418dc
    for ( int32_t i = 0; i < numPoints; i++ ) {
        Eigen::Vector4f &point = clippedPoints[i];
        point =
            point.cwiseProduct( Eigen::Vector4f( 0.5f, -0.5f, 1.0f, 1.0f ) ) +
            Eigen::Vector4f( 0.5f, 0.5f, 0.0f, 0.0f );

        minX = std::min( minX, point.x() );
        maxX = std::max( maxX, point.x() );
        minY = std::min( minY, point.y() );
        maxY = std::max( maxY, point.y() );
    }

    outRect.x() = minX;
    outRect.y() = minY;
    outRect.z() = std::max( maxX - minX, 0.0f );
    outRect.w() = std::max( maxY - minY, 0.0f );
}
