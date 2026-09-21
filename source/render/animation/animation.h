#pragma once

#include <vector>

#include "shared.h"
#include "anim_state.h"

struct BinaryReader;
class SkeletonInstance;
class ClipInstance;
class AnimExpressionList;

struct AnimationTransform {
    Eigen::Quaternionf Rotation = Eigen::Quaternionf::Identity();
    Eigen::Vector3f Translation = Eigen::Vector3f::Zero();
    Eigen::Vector3f Scale = Eigen::Vector3f::Ones();
};

static constexpr float kAnimNodeUntouched = -1.0f;     // FLOAT_00f41664
static constexpr float kAnimWeightFull = 1.0f;         // FLOAT_00f4165c
static constexpr float kAnimWeightEpsilon = 1e-4f;     // DAT_00f41760
static constexpr float kAnimBlendShareEpsilon = 0.01f; // DAT_00f416d4
static constexpr float kAnimRatioFollowCurve = 2.0f;   // FLOAT_00f416b0
static constexpr int32_t kAnimChannelPoolSize = 10;

class AnimPoseBuffer {
public:
    void resetFromBindPose( const SkeletonInstance *pSkeleton );
    void clearWeights();
    void seedFromParent( const AnimPoseBuffer &parent, int32_t numNodes );
    void clear();

    inline bool isValid() const { return !transforms.empty(); }

    inline AnimationTransform &at( int32_t node ) {
        return transforms[node + 1];
    }
    inline const AnimationTransform &at( int32_t node ) const {
        return transforms[node + 1];
    }
    inline float getWeight( int32_t node ) const { return weights[node + 1]; }
    inline void setWeight( int32_t node, float weight ) {
        weights[node + 1] = weight;
    }

private:
    std::vector<AnimationTransform> transforms;
    std::vector<float> weights;
};

struct AnimChannel {
    enum eState : int32_t {
        ACS_Free = 0,
        ACS_Playing = 1,
        ACS_Stopping = 2,
    };
    enum eLayerMode : int32_t {
        ALM_Direct = 0,
        ALM_Mode1 = 1,
        ALM_Half = 2,
        ALM_Weighted = 3,
        ALM_BindDelta = 4,
    };

    int32_t MatchID = 0;
    int32_t State = ACS_Free;
    int32_t LayerMode = ALM_Direct;
    float LayerWeight = kAnimWeightFull;
    uint32_t ClipID = 0;
    std::vector<AnimStateBlendEntry> BlendSet;
    float Progress = 0.0f;
    float Duration = 0.0f;
};

class Animation {
public:
    inline bool isValid() const { return pSkeleton != nullptr; }
    inline Matrix4x4f_t *getPalette() {
        return palette.empty() ? nullptr : palette.data();
    }
    inline int32_t getNumNodes() const { return numNodes; }
    inline int32_t getNumChannels() const {
        return static_cast<int32_t>( channels.size() );
    }
    inline int32_t getNumBlendClips() const {
        return channels.empty()
                   ? 0
                   : static_cast<int32_t>( channels[0].BlendSet.size() );
    }
    inline const AnimStateBlendEntry &getBlendEntry( int32_t index ) const {
        return channels[0].BlendSet[index];
    }
    inline ClipInstance *getClip() const {
        return ( channels.empty() || channels[0].BlendSet.empty() )
                   ? nullptr
                   : channels[0].BlendSet[0].pClip;
    }
    inline SkeletonInstance *getSkeleton() const { return pSkeleton; }

    inline void setExpressionList( AnimExpressionList *pList ) {
        pExpressionList = pList;
    }

public:
    Animation();

    void initialize( SkeletonInstance *pSkel, ClipInstance *pClip );
    void terminate();
    void update( float deltaTime );
    void setClip( ClipInstance *pNewClip );
    void setBlendSet( const AnimStateBlendEntry *pEntries, int32_t count );
    void setBlendRatios( const AnimStateBlendEntry *pEntries, int32_t count );
    void setNumChannels( int32_t count );
    void playChannel( int32_t matchID,
                      uint32_t clipID,
                      const AnimStateBlendEntry *pEntries,
                      int32_t count );
    void stopChannel( int32_t matchID );
    void setChannelLayer( int32_t matchID, int32_t layerMode, float weight );
    void setChannelBlendRatios( int32_t matchID,
                                const AnimStateBlendEntry *pEntries,
                                int32_t count );
    AnimChannel *getChannel( int32_t matchID );

    Matrix4x4f_t getNodeWorldMatrix( int32_t stopNode, int32_t node ) const;

    void setNodeLocalMatrix( int32_t node,
                             const Matrix4x4f_t &local,
                             float damping );

    void aimNodeAt( int32_t parentNode,
                    int32_t childNode,
                    const Eigen::Vector3f &targetInParentSpace,
                    float damping );

private:
    SkeletonInstance *pSkeleton = nullptr;
    std::vector<AnimChannel> channels;
    int32_t numNodes = 0;
    AnimPoseBuffer pose;
    AnimPoseBuffer scratch;
    AnimPoseBuffer layerBuffer;
    AnimPoseBuffer *pAccumulator = nullptr;
    AnimExpressionList *pExpressionList = nullptr;
    std::vector<Matrix4x4f_t> palette;

private:
    void buildBoneMatrices();
    void evaluate( float deltaTime );
    void refreshBlendDuration( AnimChannel &channel );
    void evaluateChannel( const AnimChannel &channel, AnimPoseBuffer &dst );
    void
    sampleClip( ClipInstance *pClipToSample, AnimPoseBuffer &dst, float time );
    void accumulateClip( const AnimStateBlendEntry &entry, float progress );
    void accumulatePose( const AnimPoseBuffer &src, float weight );
    void mergeLayer( int32_t layerMode, float weight );
    void mergeLayerReplace();
    void mergeLayerLerp( float weight );
    void blendPose( const AnimPoseBuffer &src, float weight );
};

// *.anm file instance (incomplete)
struct RenderAnm {
    uint32_t ID = 0;
    uint32_t File = 0;
    void *pRefBas = nullptr; // the {id,hash} clip-ref block (in bytecode)
    // Resolved runtime clip (orig: SCActor::loadResources/FUN_007da8a0 rewrites
    // the ID slot to the edANM_HDR*; the port stores it here).
    ClipInstance *pClip = nullptr;
    AnimStateInstance *pAnimState = nullptr;

    bool deserialize( BinaryReader *pReader );
};

// *.bas file instance (incomplete)
struct RenderBas {
    uint32_t File = 0;         // count of {id,hash} skeleton-ref entries
    void *pInstance = nullptr; // the {id,hash}[File] ref block (in bytecode)
    SkeletonInstance *pSkeleton = nullptr;
};
