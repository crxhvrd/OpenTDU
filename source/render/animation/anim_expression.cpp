#include "shared.h"
#include "anim_expression.h"

#include <cmath>

#include <algorithm>

#include "core/maths_helper.h"
#include "render/animation/animation.h"
#include "render/animation/skeleton.h"

static constexpr float kAnimExpressionEpsilon = 1e-6f; // FLOAT_00f41888

static constexpr float kAnimExpressionAngleEpsilon = 1e-5f; // DAT_00f41710
static constexpr float kPi = 3.1415927f;
static const Eigen::Vector4f kAnimExpressionAxisY( 0.0f,
                                                   1.0f,
                                                   0.0f,
                                                   0.0f ); // DAT_00fac2e0
static const Eigen::Vector4f kAnimExpressionAxisZ( 0.0f,
                                                   0.0f,
                                                   1.0f,
                                                   0.0f ); // DAT_00fac340

static inline Eigen::Vector4f
NormalizeOrFallback( const Eigen::Vector4f &vector,
                     const Eigen::Vector4f &fallback ) {
    // FUN_00679020
    const float length = vector.head<3>().norm();
    if ( length < kAnimExpressionEpsilon ) {
        return fallback;
    }
    return vector / length;
}

static bool InvertSoft( const Matrix4x4f_t &in, Matrix4x4f_t &out ) {
    // FUN_00502410
    const float determinant = in.determinant();
    if ( std::fabs( determinant ) < 1e-12f ) {
        return false;
    }
    out = in.inverse();
    return true;
}

static inline Eigen::Vector3f GetTranslation( const Matrix4x4f_t &m ) {
    return Eigen::Vector3f( m( 3, 0 ), m( 3, 1 ), m( 3, 2 ) );
}

static inline Eigen::Vector3f TransformPoint( const Matrix4x4f_t &m,
                                              const Eigen::Vector3f &p ) {
    const Eigen::Vector4f out =
        Mat4x4fTransform( m, Eigen::Vector4f( p.x(), p.y(), p.z(), 1.0f ) );
    return out.head<3>();
}

Eigen::Quaternionf BuildAimRotation( const Eigen::Vector3f &from,
                                     const Eigen::Vector3f &to,
                                     float damping ) {
    // FUN_00679180
    const float fromLength = from.norm();
    const float toLength = to.norm();
    if ( fromLength > kAnimExpressionEpsilon &&
         toLength > kAnimExpressionEpsilon ) {
        const Eigen::Vector3f a = from / fromLength;
        const Eigen::Vector3f b = to / toLength;
        Eigen::Vector3f axis = a.cross( b );
        const float axisLength = axis.norm();
        if ( axisLength > kAnimExpressionEpsilon ) {
            axis /= axisLength;
            const float dot = std::max( -1.0f, std::min( 1.0f, b.dot( a ) ) );
            const float angle = std::acos( dot ) * damping;
            return Eigen::Quaternionf( Eigen::AngleAxisf( -angle, axis ) );
        }
    }
    return Eigen::Quaternionf::Identity();
}

void AnimTargetParams::setTarget( const Eigen::Vector3f &target,
                                  float transitionLength ) {
    // FUN_00424f40
    NextTarget = Eigen::Vector4f( target.x(), target.y(), target.z(), 0.0f );
    TargetTransitionLength = transitionLength;
    if ( transitionLength == 0.0f ) {
        Target = NextTarget;
    }
    AngularSpeed = 0.0f;
    TargetID = kNoTarget;
}

void AnimTargetParams::timeslice( Animation &animation, float deltaTime ) {
    // FUN_00679640
    if ( TargetID != kNoTarget ) {
        const SkeletonInstance *pSkeleton = animation.getSkeleton();
        const int32_t node =
            ( pSkeleton != nullptr ) ? pSkeleton->findNode( TargetID ) : -1;
        if ( node >= 0 ) {
            const Eigen::Vector3f nodePosition =
                GetTranslation( animation.getNodeWorldMatrix( -1, node ) );
            NextTarget = Eigen::Vector4f( nodePosition.x(), nodePosition.y(),
                                          nodePosition.z(), 0.0f );
        }
    }

    if ( AngularSpeed != 0.0f ) {
        const Eigen::Vector4f currentDir =
            NormalizeOrFallback( Target, kAnimExpressionAxisZ );
        const Eigen::Vector4f nextDir =
            NormalizeOrFallback( NextTarget, kAnimExpressionAxisZ );
        const Eigen::Vector3f cross =
            currentDir.head<3>().cross( nextDir.head<3>() );
        const Eigen::Vector4f axis = NormalizeOrFallback(
            Eigen::Vector4f( cross.x(), cross.y(), cross.z(), 0.0f ),
            kAnimExpressionAxisY );

        const float dot = currentDir.head<3>().dot( nextDir.head<3>() );
        float angle = 0.0f;
        if ( dot < -1.0f ) {
            angle = kPi;
        } else if ( dot <= 1.0f ) {
            angle = std::acos( dot );
        }

        const float step = deltaTime * AngularSpeed;
        if ( angle > step && angle > kAnimExpressionAngleEpsilon ) {
            const float ratio = step / angle;
            const Eigen::Vector3f direction =
                Eigen::AngleAxisf( angle, axis.head<3>() ) *
                currentDir.head<3>();
            const float length = NextTarget.head<3>().norm() * ratio +
                                 ( 1.0f - ratio ) * Target.head<3>().norm();
            Target = Eigen::Vector4f( direction.x(), direction.y(),
                                      direction.z(), 0.0f ) *
                     length;
            return;
        }

        Target = NextTarget;
        return;
    }

    if ( TargetTransitionLength > deltaTime &&
         TargetTransitionLength != 0.0f ) {
        Target +=
            ( ( NextTarget - Target ) / TargetTransitionLength ) * deltaTime;
        TargetTransitionLength -= deltaTime;
        return;
    }

    TargetTransitionLength = 0.0f;
    Target = NextTarget;
}

void AnimExpression::advanceDamping( float deltaTime ) {
    Flags = 0;
    if ( DampingTransitionLength > deltaTime ) {
        Damping +=
            ( ( NextDamping - Damping ) / DampingTransitionLength ) * deltaTime;
        DampingTransitionLength -= deltaTime;
        return;
    }
    DampingTransitionLength = 0.0f;
    Damping = NextDamping;
}

void AnimIK3NParams::timeslice( Animation &animation, float deltaTime ) {
    // FUN_00679950
    advanceDamping( deltaTime );
    if ( Damping < kAnimExpressionEpsilon ) {
        return;
    }

    TargetParams.timeslice( animation, deltaTime );

    const SkeletonInstance *pSkeleton = animation.getSkeleton();
    if ( pSkeleton == nullptr ) {
        Flags |= 1;
        return;
    }

    const int32_t rootNode = pSkeleton->findNode( NodesID[0] );
    const int32_t midNode = pSkeleton->findNode( NodesID[1] );
    const int32_t endNode = pSkeleton->findNode( NodesID[2] );
    if ( rootNode < 0 || midNode < 0 || endNode < 0 ) {
        Flags |= 1;
        return;
    }

    const Matrix4x4f_t rootWorld = animation.getNodeWorldMatrix( -1, rootNode );
    const Matrix4x4f_t midWorld = animation.getNodeWorldMatrix( -1, midNode );
    const Matrix4x4f_t endWorld = animation.getNodeWorldMatrix( -1, endNode );

    Matrix4x4f_t rootInverse;
    if ( !InvertSoft( rootWorld, rootInverse ) ) {
        Flags |= 1;
        return;
    }

    const Eigen::Vector3f targetLocal =
        TransformPoint( rootInverse, TargetParams.Target.head<3>() );
    const Eigen::Vector3f midLocal =
        TransformPoint( rootInverse, GetTranslation( midWorld ) );

    float targetDistance = targetLocal.norm();
    const float upperLength = midLocal.norm();
    if ( targetDistance <= kAnimExpressionEpsilon ||
         upperLength <= kAnimExpressionEpsilon ) {
        Flags |= 1;
        return;
    }

    const Eigen::Vector3f targetDirection = targetLocal / targetDistance;
    const Eigen::Vector3f midDirection = midLocal / upperLength;

    Eigen::Vector3f planeNormal = targetDirection.cross( midDirection );
    const float planeNormalLength = planeNormal.norm();
    if ( planeNormalLength <= kAnimExpressionEpsilon ) {
        Flags |= 1;
        return;
    }
    planeNormal /= planeNormalLength;
    const Eigen::Vector3f bendDirection = planeNormal.cross( targetDirection );

    const float lowerLength =
        ( GetTranslation( endWorld ) - GetTranslation( midWorld ) ).norm();

    if ( targetDistance > lowerLength + upperLength ) {
        targetDistance = ( lowerLength + upperLength ) - kAnimExpressionEpsilon;
    }
    const float upperLengthSq = upperLength * upperLength;
    const float along = ( targetDistance * targetDistance +
                          ( upperLengthSq - lowerLength * lowerLength ) ) /
                        ( targetDistance + targetDistance );
    if ( along > targetDistance || along > upperLength ||
         along < kAnimExpressionEpsilon ) {
        Flags |= 1;
        return;
    }

    float offsetSq = upperLengthSq - along * along;
    if ( offsetSq < 0.0f ) {
        offsetSq = 0.0f;
    }

    const Eigen::Vector3f solvedMid =
        targetDirection * along + bendDirection * std::sqrt( offsetSq );
    animation.aimNodeAt( rootNode, midNode, solvedMid, Damping );

    Matrix4x4f_t midInverse;
    if ( !InvertSoft( animation.getNodeWorldMatrix( -1, midNode ),
                      midInverse ) ) {
        Flags |= 1;
        return;
    }

    const Eigen::Vector3f targetInMid =
        TransformPoint( midInverse, TargetParams.Target.head<3>() );
    animation.aimNodeAt( midNode, endNode, targetInMid, Damping );
}

void AnimTarget2NParams::timeslice( Animation &animation, float deltaTime ) {
    // FUN_00679f40
    advanceDamping( deltaTime );
    TargetParams.timeslice( animation, deltaTime );

    if ( Damping < kAnimExpressionEpsilon ) {
        return;
    }

    const SkeletonInstance *pSkeleton = animation.getSkeleton();
    if ( pSkeleton == nullptr ) {
        Flags |= 1;
        return;
    }

    const int32_t parentNode = pSkeleton->findNode( NodesID[0] );
    const int32_t childNode = pSkeleton->findNode( NodesID[1] );
    if ( parentNode < 0 || childNode < 0 ) {
        Flags |= 1;
        return;
    }

    Matrix4x4f_t parentInverse;
    if ( !InvertSoft( animation.getNodeWorldMatrix( -1, parentNode ),
                      parentInverse ) ) {
        Flags |= 1;
        return;
    }

    const Eigen::Vector3f targetLocal =
        TransformPoint( parentInverse, TargetParams.Target.head<3>() );
    animation.aimNodeAt( parentNode, childNode, targetLocal, Damping );
}

void AnimAlign1NParams::timeslice( Animation &animation, float deltaTime ) {
    // FUN_00679e30
    advanceDamping( deltaTime );
    if ( Damping < kAnimExpressionEpsilon ) {
        return;
    }

    const SkeletonInstance *pSkeleton = animation.getSkeleton();
    const int32_t node =
        ( pSkeleton != nullptr ) ? pSkeleton->findNode( NodeID ) : -1;
    if ( node < 0 ) {
        Flags |= 1;
        return;
    }

    Matrix4x4f_t worldInverse;
    InvertSoft( animation.getNodeWorldMatrix( -1, node ), worldInverse );

    const Eigen::Vector3f targetLocal =
        worldInverse.block<3, 3>( 0, 0 ).transpose() * Target.head<3>();
    animation.rotateNode(
        node, BuildAimRotation( LocalAxis.head<3>(), targetLocal, Damping ) );
}

void AnimExpressionList::append( AnimExpression *pExpression ) {
    // FUN_00678ec0
    if ( pExpression == nullptr ) {
        return;
    }

    AnimExpression *pTail = this;
    while ( pTail->pNext != nullptr ) {
        pTail = pTail->pNext;
    }
    pTail->pNext = pExpression;
    pExpression->pNext = nullptr;
}

void AnimExpressionList::clear() { pNext = nullptr; }

void AnimLinkParams::timeslice( Animation &animation, float deltaTime ) {
    // FUN_0067a0b0
    advanceDamping( deltaTime );
    TargetParams.timeslice( animation, deltaTime );

    if ( Damping < kAnimExpressionEpsilon ) {
        return;
    }

    const SkeletonInstance *pSkeleton = animation.getSkeleton();
    if ( pSkeleton == nullptr ) {
        Flags |= 1;
        return;
    }

    const int32_t node = pSkeleton->findNode( NodeID );
    if ( node < 0 ) {
        Flags |= 1;
        return;
    }

    Matrix4x4f_t world = animation.getNodeWorldMatrix( -1, node );
    world( 3, 0 ) = TargetParams.Target.x();
    world( 3, 1 ) = TargetParams.Target.y();
    world( 3, 2 ) = TargetParams.Target.z();

    Matrix4x4f_t local = world;
    const int32_t parent = pSkeleton->getParent( node );
    if ( parent >= 0 ) {
        Matrix4x4f_t parentInverse;
        if ( !InvertSoft( animation.getNodeWorldMatrix( -1, parent ),
                          parentInverse ) ) {
            Flags |= 1;
            return;
        }
        local = parentInverse * world;
    }

    animation.setNodeLocalMatrix( node, local, Damping );
}

void AnimExpressionList::timeslice( Animation &animation, float deltaTime ) {
    // FUN_00678e10
    advanceDamping( deltaTime );

    for ( AnimExpression *pExpression = pNext; pExpression != nullptr;
          pExpression = pExpression->pNext ) {
        pExpression->timeslice( animation, deltaTime );
        if ( ( pExpression->Flags & 1 ) != 0 ) {
            Flags |= 1;
        }
    }
}
