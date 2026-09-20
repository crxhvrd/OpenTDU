#include "shared.h"
#include "animation.h"

#include <cmath>

#include <algorithm>

#include "core/maths_helper.h"
#include "render/animation/skeleton.h"
#include "render/animation/clip.h"
#include "filesystem/binary.h"
#include "render/animation/anim_expression.h"
#include "game/scenaric/gs_scenaric.h"

static AnimationTransform ParseAnimTransform( const float *p ) {
    AnimationTransform rts;
    rts.Rotation =
        Eigen::Quaternionf( p[3], p[0], p[1], p[2] ); // (w,x,y,z) ctor
    rts.Translation = Eigen::Vector3f( p[4], p[5], p[6] );
    rts.Scale = Eigen::Vector3f( p[8], p[9], p[10] );
    return rts;
}

static float BracketKeyframe( int32_t numKeys,
                              const float *times,
                              float time,
                              int32_t *pLower,
                              int32_t *pUpper ) {
    // FUN_005130a0
    if ( times[numKeys - 1] <= time ) {
        *pLower = numKeys - 1;
        *pUpper = 0;
        const float d = times[0] - times[numKeys - 1];
        return d <= 0.0f ? 0.0f : ( time - times[numKeys - 1] ) / d;
    }
    int32_t i = numKeys >> 1;
    while ( time < times[i] && i > 1 ) {
        i >>= 1;
    }
    if ( i < 1 ) {
        i = 1;
    }
    const float *p = times + i;
    while ( *p <= time ) {
        p++;
    }
    const int32_t upper = static_cast<int32_t>( p - times );
    *pUpper = upper;
    const int32_t lower = upper - 1;
    *pLower = lower;
    if ( lower == 0 ) {
        return *p <= 0.0f ? 0.0f : time / *p;
    }
    const float d = *p - p[-1];
    return d <= 0.0f ? 0.0f : ( time - p[-1] ) / d;
}

static constexpr float kAnimTimeEpsilon = 1e-6f; // FLOAT_00f41888
static constexpr uint8_t kSubChannelAdditive = 0x10;

static inline float ClampToChannelDuration( const float *times, float time ) {
    return ( times[0] <= time ) ? times[0] - kAnimTimeEpsilon : time;
}

static const uint8_t *
ParseRotation( const uint8_t *pSub, float time, Eigen::Quaternionf &out ) {
    // FUN_006787d0
    const uint16_t numKeys = *reinterpret_cast<const uint16_t *>( pSub );
    const uint8_t subFlags = pSub[2];
    if ( numKeys == 0 || ( subFlags & 7 ) != 0 ) {
        return nullptr;
    }
    const float *times = reinterpret_cast<const float *>( pSub + 4 );
    const float *keys = times + numKeys; // 4 floats / key (x,y,z,w)
    const bool bAdditive = ( subFlags & kSubChannelAdditive ) != 0;
    const Eigen::Quaternionf incoming = out;

    if ( numKeys == 1 ) {
        out = Eigen::Quaternionf( keys[3], keys[0], keys[1], keys[2] );
    } else {
        time = ClampToChannelDuration( times, time );
        int32_t lo = 0;
        int32_t hi = 0;
        const float frac = BracketKeyframe( numKeys, times, time, &lo, &hi );
        const float *k0 = keys + lo * 4;
        const float *k1 = keys + hi * 4;
        const Eigen::Quaternionf q0( k0[3], k0[0], k0[1], k0[2] );
        const Eigen::Quaternionf q1( k1[3], k1[0], k1[1], k1[2] );
        out = SlerpNoFlip( q0, q1, frac );
    }

    if ( bAdditive ) {
        out = incoming * out;
    }
    return reinterpret_cast<const uint8_t *>( keys + numKeys * 4 );
}

enum class eVec3Combine { Add, Multiply };

static const uint8_t *ParseVec3( const uint8_t *pSub,
                                 float time,
                                 Eigen::Vector3f &out,
                                 eVec3Combine combine ) {
    const uint16_t numKeys = *reinterpret_cast<const uint16_t *>( pSub );
    const uint8_t subFlags = pSub[2];
    if ( numKeys == 0 || ( subFlags & 7 ) != 0 ) {
        return nullptr;
    }
    const float *times = reinterpret_cast<const float *>( pSub + 4 );
    const float *keys = times + numKeys; // 3 floats / key

    const bool bAdditive = ( subFlags & kSubChannelAdditive ) != 0;
    const Eigen::Vector3f incoming = out;

    if ( numKeys == 1 ) {
        out = Eigen::Vector3f( keys[0], keys[1], keys[2] );
    } else {
        time = ClampToChannelDuration( times, time );
        int32_t lo = 0;
        int32_t hi = 0;
        const float frac = BracketKeyframe( numKeys, times, time, &lo, &hi );
        const Eigen::Vector3f v0( keys[lo * 3], keys[lo * 3 + 1],
                                  keys[lo * 3 + 2] );
        const Eigen::Vector3f v1( keys[hi * 3], keys[hi * 3 + 1],
                                  keys[hi * 3 + 2] );
        out = v0 + ( v1 - v0 ) * frac;
    }

    if ( bAdditive ) {
        if ( combine == eVec3Combine::Add ) {
            out += incoming;
        } else {
            out = out.cwiseProduct( incoming );
        }
    }
    return reinterpret_cast<const uint8_t *>( keys + numKeys * 3 );
}

static void PropagateScale( Matrix4x4f_t &local,
                            const Matrix4x4f_t &parentWorld ) {
    // FUN_00676130
    float f[3];
    for ( int32_t j = 0; j < 3; j++ ) {
        const float len2 = parentWorld.row( j ).squaredNorm();
        f[j] = ( len2 > 1e-12f ) ? 1.0f / std::sqrt( len2 ) : 1.0f;
    }
    for ( int32_t r = 0; r < 3; r++ ) {
        local( r, 0 ) *= f[0];
        local( r, 1 ) *= f[1];
        local( r, 2 ) *= f[2];
    }
}

void AnimPoseBuffer::resetFromBindPose( const SkeletonInstance *pSkeleton ) {
    // FUN_00678750
    const int32_t numNodes = pSkeleton->getNumNodes();
    transforms.assign( numNodes + 1, AnimationTransform() );
    weights.assign( numNodes + 1, kAnimNodeUntouched );

    for ( int32_t i = 0; i < numNodes; i++ ) {
        at( i ) = ParseAnimTransform( pSkeleton->getBindPose( i ) );
    }
}

void AnimPoseBuffer::seedFromParent( const AnimPoseBuffer &parent,
                                     int32_t numNodes ) {
    // FUN_00678750
    transforms.assign( numNodes + 1, AnimationTransform() );
    weights.assign( numNodes + 1, kAnimNodeUntouched );

    for ( int32_t i = 0; i < numNodes; i++ ) {
        at( i ) = parent.at( i );
    }
}

void AnimPoseBuffer::clearWeights() {
    // FUN_004247a0
    std::fill( weights.begin(), weights.end(), kAnimNodeUntouched );
}

void AnimPoseBuffer::clear() {
    transforms.clear();
    transforms.shrink_to_fit();
    weights.clear();
    weights.shrink_to_fit();
}

Animation::Animation() {}

void Animation::initialize( SkeletonInstance *pSkel, ClipInstance *pClipIn ) {
    // FUN_009b3d50
    pSkeleton = pSkel;
    setClip( pClipIn );
    if ( pSkeleton == nullptr ) {
        numNodes = 0;
        return;
    }
    numNodes = pSkeleton->getNumNodes();
    scratch.resetFromBindPose( pSkeleton );
    palette.assign( numNodes, Matrix4x4f_t::Identity() );

    evaluate( 0.0f );
}

void Animation::setClip( ClipInstance *pNewClip ) {
    AnimStateBlendEntry entry;
    entry.pClip = pNewClip;
    entry.Weight = kAnimWeightFull;
    entry.Ratio = kAnimRatioFollowCurve;
    setBlendSet( &entry, pNewClip != nullptr ? 1 : 0 );
}

void Animation::setNumChannels( int32_t count ) {
    // FUN_009b3c80
    const int32_t numChannels = std::max( count, 1 );
    channels.resize( numChannels );
    for ( int32_t i = 0; i < numChannels; i++ ) {
        channels[i].MatchID = i;
    }
}

AnimChannel *Animation::getChannel( int32_t matchID ) {
    // FUN_00677460
    for ( AnimChannel &channel : channels ) {
        if ( channel.MatchID == matchID ) {
            return &channel;
        }
    }
    return nullptr;
}

void Animation::playChannel( int32_t matchID,
                             uint32_t clipID,
                             const AnimStateBlendEntry *pEntries,
                             int32_t count ) {
    // FUN_00401400
    AnimChannel *pChannel = getChannel( matchID );
    if ( pChannel == nullptr ) {
        return;
    }

    const bool bSameClip = ( pChannel->State != AnimChannel::ACS_Free &&
                             pChannel->ClipID == clipID );

    pChannel->BlendSet.assign( pEntries, pEntries + std::max( count, 0 ) );
    pChannel->ClipID = clipID;
    pChannel->State = AnimChannel::ACS_Playing;
    if ( !bSameClip ) {
        pChannel->Progress = 0.0f;
    }
    refreshBlendDuration( *pChannel );
}

void Animation::stopChannel( int32_t matchID ) {
    AnimChannel *pChannel = getChannel( matchID );
    if ( pChannel != nullptr ) {
        pChannel->State = AnimChannel::ACS_Free;
    }
}

void Animation::setChannelLayer( int32_t matchID,
                                 int32_t layerMode,
                                 float weight ) {
    AnimChannel *pChannel = getChannel( matchID );
    if ( pChannel != nullptr ) {
        pChannel->LayerMode = layerMode;
        pChannel->LayerWeight = weight;
    }
}

void Animation::setChannelBlendRatios( int32_t matchID,
                                       const AnimStateBlendEntry *pEntries,
                                       int32_t count ) {
    AnimChannel *pChannel = getChannel( matchID );
    if ( pChannel == nullptr ) {
        return;
    }

    const int32_t numShared =
        std::min( static_cast<int32_t>( pChannel->BlendSet.size() ), count );
    for ( int32_t i = 0; i < numShared; i++ ) {
        pChannel->BlendSet[i].Weight = pEntries[i].Weight;
        pChannel->BlendSet[i].Ratio = pEntries[i].Ratio;
    }
    refreshBlendDuration( *pChannel );
}

void Animation::setBlendSet( const AnimStateBlendEntry *pEntries,
                             int32_t count ) {
    if ( channels.empty() ) {
        setNumChannels( 1 );
    }
    playChannel( 0, 0, pEntries, count );
    channels[0].Progress = 0.0f;

    if ( pSkeleton == nullptr ) {
        return;
    }

    evaluate( 0.0f );
}

void Animation::setBlendRatios( const AnimStateBlendEntry *pEntries,
                                int32_t count ) {
    setChannelBlendRatios( 0, pEntries, count );
}

void Animation::refreshBlendDuration( AnimChannel &channel ) {
    // FUN_0067a8c0
    channel.Duration = 0.0f;
    for ( const AnimStateBlendEntry &entry : channel.BlendSet ) {
        if ( entry.pClip == nullptr ) {
            continue;
        }
        channel.Duration +=
            entry.Weight * entry.pClip->getTotalDurationLooped();
    }
}

void Animation::terminate() {
    // FUN_007fa7f0 (inlined)
    pSkeleton = nullptr;
    channels.clear();
    channels.shrink_to_fit();
    numNodes = 0;

    pose.clear();
    scratch.clear();
    layerBuffer.clear();
    palette.clear();
    palette.shrink_to_fit();
}

void Animation::update( float deltaTime ) {
    // FUN_004012a0
    if ( pSkeleton == nullptr ) {
        return;
    }
    for ( AnimChannel &channel : channels ) {
        if ( channel.State != AnimChannel::ACS_Playing ||
             channel.Duration <= 0.0f ) {
            continue;
        }
        channel.Progress += deltaTime / channel.Duration;
        if ( channel.Progress >= 1.0f ) {
            channel.Progress = std::fmod( channel.Progress, 1.0f );
        }
    }
    evaluate( deltaTime );
}

void Animation::evaluate( float deltaTime ) {
    // FUN_004012a0
    pose.resetFromBindPose( pSkeleton );

    for ( const AnimChannel &channel : channels ) {
        if ( channel.State != AnimChannel::ACS_Playing ||
             channel.BlendSet.empty() ) {
            continue;
        }

        if ( channel.LayerMode == AnimChannel::ALM_Direct ) {
            evaluateChannel( channel, pose );
            continue;
        }

        layerBuffer.seedFromParent( pose, numNodes );
        evaluateChannel( channel, layerBuffer );

        mergeLayer( channel.LayerMode, channel.LayerWeight );
    }

    if ( pExpressionList != nullptr ) {
        pExpressionList->timeslice( *this, deltaTime );
    }

    buildBoneMatrices();
}

void Animation::evaluateChannel( const AnimChannel &channel,
                                 AnimPoseBuffer &dst ) {
    AnimPoseBuffer *pPrevTarget = pAccumulator;
    pAccumulator = &dst;

    for ( const AnimStateBlendEntry &entry : channel.BlendSet ) {
        accumulateClip( entry, channel.Progress );
    }

    pAccumulator = pPrevTarget;
}

void Animation::accumulateClip( const AnimStateBlendEntry &entry,
                                float progress ) {
    // FUN_0067aac0 (inlined)
    if ( entry.pClip == nullptr || entry.Weight < kAnimWeightEpsilon ) {
        return;
    }

    const float duration = entry.pClip->getTotalDurationLooped();
    if ( duration <= 0.0f ) {
        return;
    }

    const float normalised =
        ( entry.Ratio == kAnimRatioFollowCurve ) ? progress : entry.Ratio;
    float time = duration * normalised;
    time = std::fmod( time, duration );
    if ( time < 0.0f ) {
        time += duration;
    }

    if ( !scratch.isValid() ) {
        scratch.resetFromBindPose( pSkeleton );
    }
    scratch.clearWeights();
    sampleClip( entry.pClip, scratch, time );
    accumulatePose( scratch, entry.Weight );
}

void Animation::mergeLayer( int32_t layerMode, float weight ) {
    switch ( layerMode ) {
    case AnimChannel::ALM_BindDelta:
        blendPose( layerBuffer, weight );
        break;
    case AnimChannel::ALM_Mode1:
        mergeLayerReplace();
        break;
    case AnimChannel::ALM_Half:
        mergeLayerLerp( 0.5f );
        break;
    case AnimChannel::ALM_Weighted:
        mergeLayerLerp( weight );
        break;
    default:
        break;
    }
}

void Animation::mergeLayerReplace() {
    // FUN_00675f30
    for ( int32_t node = -1; node < numNodes; node++ ) {
        const float srcWeight = layerBuffer.getWeight( node );
        if ( srcWeight == kAnimNodeUntouched ) {
            continue;
        }
        pose.at( node ) = layerBuffer.at( node );
        pose.setWeight( node, srcWeight );
    }
}

void Animation::mergeLayerLerp( float weight ) {
    // FUN_006767d0
    const float inverse = 1.0f - weight;

    for ( int32_t node = -1; node < numNodes; node++ ) {
        if ( layerBuffer.getWeight( node ) == kAnimNodeUntouched ) {
            continue;
        }

        AnimationTransform &dst = pose.at( node );
        const AnimationTransform &src = layerBuffer.at( node );

        dst.Rotation =
            QuaternionSlerpShortest( dst.Rotation, src.Rotation, weight );
        dst.Translation = dst.Translation * inverse + src.Translation * weight;
        dst.Scale = dst.Scale * inverse + src.Scale * weight;

        pose.setWeight( node, kAnimWeightFull );
    }
}

void Animation::accumulatePose( const AnimPoseBuffer &src, float weight ) {
    for ( int32_t node = -1; node < numNodes; node++ ) {
        if ( src.getWeight( node ) == kAnimNodeUntouched ) {
            continue;
        }

        AnimPoseBuffer &target =
            ( pAccumulator != nullptr ) ? *pAccumulator : pose;
        const AnimationTransform &sampled = src.at( node );
        AnimationTransform &dst = target.at( node );
        const float accumulated = target.getWeight( node );

        if ( accumulated == kAnimNodeUntouched ) {
            dst = sampled;
            target.setWeight( node, weight );
            continue;
        }

        const float total = accumulated + weight;
        const float share = weight / total;
        if ( share <= kAnimBlendShareEpsilon ) {
            continue;
        }

        dst.Rotation =
            QuaternionSlerpShortest( dst.Rotation, sampled.Rotation, share );
        dst.Translation += ( sampled.Translation - dst.Translation ) * share;
        dst.Scale += ( sampled.Scale - dst.Scale ) * share;
        target.setWeight( node, total );
    }
}

void Animation::sampleClip( ClipInstance *pClipToSample,
                            AnimPoseBuffer &dst,
                            float time ) {
    // FUN_006765d0
    if ( pClipToSample == nullptr || !pClipToSample->isValid() ) {
        return;
    }
    const uint8_t *pCh = pClipToSample->getControllers();
    uint32_t numCh = pClipToSample->getNumControllers();
    if ( pCh == nullptr || numCh == 0 ) {
        return;
    }

    const uint16_t rootFlags = *reinterpret_cast<const uint16_t *>( pCh );
    if ( ( rootFlags & 7 ) != 0 && ( rootFlags & 0x10 ) != 0 ) {
        AnimationTransform motion;
        const uint8_t *pSub = pCh + 0xc;
        if ( ( rootFlags & 1 ) && pSub != nullptr ) {
            pSub = ParseRotation( pSub, time, motion.Rotation );
        }
        if ( ( rootFlags & 2 ) && pSub != nullptr ) {
            pSub =
                ParseVec3( pSub, time, motion.Translation, eVec3Combine::Add );
        }
        if ( ( rootFlags & 4 ) && pSub != nullptr ) {
            pSub =
                ParseVec3( pSub, time, motion.Scale, eVec3Combine::Multiply );
        }
        dst.at( -1 ) = motion;
        dst.setWeight( -1, kAnimWeightFull );

        const uint32_t size = *reinterpret_cast<const uint32_t *>( pCh + 4 );
        if ( size == 0 ) {
            return;
        }
        pCh += size;
        numCh--;
    }

    for ( uint32_t c = 0; c < numCh; c++ ) {
        const uint16_t flags = *reinterpret_cast<const uint16_t *>( pCh );
        const uint32_t size = *reinterpret_cast<const uint32_t *>( pCh + 4 );
        const uint32_t boneID = *reinterpret_cast<const uint32_t *>( pCh + 8 );
        const int32_t node = pSkeleton->findNode( boneID );
        if ( node >= 0 && node < numNodes ) {
            AnimationTransform sampled =
                ParseAnimTransform( pSkeleton->getBindPose( node ) );
            const uint8_t *pSub = pCh + 0xc;
            if ( ( flags & 1 ) && pSub != nullptr ) {
                pSub = ParseRotation( pSub, time, sampled.Rotation );
            }
            if ( ( flags & 2 ) && pSub != nullptr ) {
                pSub = ParseVec3( pSub, time, sampled.Translation,
                                  eVec3Combine::Add );
            }
            if ( ( flags & 4 ) && pSub != nullptr ) {
                pSub = ParseVec3( pSub, time, sampled.Scale,
                                  eVec3Combine::Multiply );
            }
            dst.at( node ) = sampled;
            dst.setWeight( node, kAnimWeightFull );
        }
        if ( size == 0 ) {
            break;
        }
        pCh += size;
    }
}

void Animation::blendPose( const AnimPoseBuffer &src, float weight ) {
    // FUN_00676840
    for ( int32_t node = 0; node < numNodes; node++ ) {
        if ( src.getWeight( node ) == kAnimNodeUntouched ) {
            continue;
        }

        const AnimationTransform bind =
            ParseAnimTransform( pSkeleton->getBindPose( node ) );
        const AnimationTransform &sampled = src.at( node );

        Eigen::Quaternionf deltaRot =
            bind.Rotation.conjugate() * sampled.Rotation;
        Eigen::Vector3f deltaTranslation =
            sampled.Translation - bind.Translation;
        Eigen::Vector3f deltaScale = sampled.Scale - bind.Scale;

        if ( weight != kAnimWeightFull ) {
            deltaRot = QuaternionExp( QuaternionLog( deltaRot ) * weight );
            deltaTranslation *= weight;
            deltaScale *= weight;
        }

        AnimationTransform &dst = pose.at( node );
        dst.Rotation = dst.Rotation * deltaRot;
        dst.Translation += deltaTranslation;
        dst.Scale += deltaScale;
        pose.setWeight( node, kAnimWeightFull );
    }
}

static Matrix4x4f_t BuildNodeLocalMatrix( const AnimationTransform &rts ) {
    Matrix4x4f_t local = BuildRotationMatrix( rts.Rotation );
    local.row( 0 ) *= rts.Scale.x();
    local.row( 1 ) *= rts.Scale.y();
    local.row( 2 ) *= rts.Scale.z();
    local.row( 3 ) = Eigen::Vector4f( rts.Translation.x(), rts.Translation.y(),
                                      rts.Translation.z(), 1.0f );
    return local;
}

void Animation::setNodeLocalMatrix( int32_t node,
                                    const Matrix4x4f_t &local,
                                    float damping ) {
    // FUN_00678d30
    if ( node < 0 || node >= numNodes ) {
        return;
    }

    AnimationTransform &dst = pose.at( node );
    const Eigen::Quaternionf rotation( local.block<3, 3>( 0, 0 ) );

    dst.Rotation = QuaternionSlerpShortest( dst.Rotation, rotation, damping );
    dst.Translation =
        Eigen::Vector3f( local( 3, 0 ), local( 3, 1 ), local( 3, 2 ) );
    pose.setWeight( node, kAnimWeightFull );
}

Matrix4x4f_t Animation::getNodeWorldMatrix( int32_t stopNode,
                                            int32_t node ) const {
    // FUN_006793d0
    Matrix4x4f_t accumulated = Matrix4x4f_t::Identity();
    if ( pSkeleton == nullptr ) {
        return accumulated;
    }

    while ( node != stopNode && node >= 0 && node < numNodes ) {
        accumulated = accumulated * BuildNodeLocalMatrix( pose.at( node ) );
        node = pSkeleton->getParent( node );
    }
    return accumulated;
}

void Animation::aimNodeAt( int32_t parentNode,
                           int32_t childNode,
                           const Eigen::Vector3f &targetInParentSpace,
                           float damping ) {
    // FUN_00679540
    const Matrix4x4f_t childLocal = getNodeWorldMatrix( parentNode, childNode );
    const Eigen::Vector3f childPosition( childLocal( 3, 0 ), childLocal( 3, 1 ),
                                         childLocal( 3, 2 ) );

    const Eigen::Quaternionf delta =
        BuildAimRotation( childPosition, targetInParentSpace, damping );

    if ( pose.getWeight( parentNode ) == kAnimNodeUntouched ) {
        pose.setWeight( parentNode, 0.0f );
    }

    AnimationTransform &rts = pose.at( parentNode );
    rts.Rotation = delta * rts.Rotation;
}

void Animation::buildBoneMatrices() {
    // FUN_006770c0
    for ( int32_t i = 0; i < numNodes; i++ ) {
        const AnimationTransform &rts = pose.at( i );

        Matrix4x4f_t local = BuildRotationMatrix( rts.Rotation );
        local.row( 0 ) *= rts.Scale.x();
        local.row( 1 ) *= rts.Scale.y();
        local.row( 2 ) *= rts.Scale.z();
        local.row( 3 ) =
            Eigen::Vector4f( rts.Translation.x(), rts.Translation.y(),
                             rts.Translation.z(), 1.0f );

        const int32_t parent = pSkeleton->getParent( i );
        if ( parent != -1 ) {
            PropagateScale( local, palette[parent] );
            palette[i] = local * palette[parent];
        } else {
            palette[i] = local;
        }
    }

    for ( int32_t i = 0; i < numNodes; i++ ) {
        const Eigen::Map<const Matrix4x4f_t> invBind(
            pSkeleton->getInverseBind( i ) );
        const Matrix4x4f_t world = palette[i];
        palette[i] = invBind * world;
    }
}

bool RenderAnm::deserialize( BinaryReader *pReader ) {
    // FUN_007fa5b0
    ID = *reinterpret_cast<uint32_t *>( pReader->pCursor );
    pReader->pCursor += sizeof( uint32_t );

    if ( gpScenaric->getVersion() > 0xf ) {
        pRefBas = pReader->pCursor;
        pReader->pCursor += 16;
        File = ( *reinterpret_cast<int32_t *>( pRefBas ) != 0 );
        return true;
    }

    File = *reinterpret_cast<uint32_t *>( pReader->pCursor );
    pReader->pCursor += sizeof( uint32_t );
    if ( File != 0 ) {
        pRefBas = pReader->pCursor;
        pReader->pCursor += File * 8;
    }
    return true;
}
