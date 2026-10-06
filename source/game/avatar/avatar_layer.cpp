#include "shared.h"
#include "avatar_layer.h"

#include "avatar_clothes.h"
#include "avatar_part.h"
#include "gs_avatar.h"

#include "flash/implementation/ed_flash.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/animation/anim_state.h"
#include "render/animation/animation.h"
#include "render/animation/skeleton.h"

static constexpr int32_t kNoPose = AvatarPoseLayer::AP_Count;
static constexpr uint32_t kHelmetBoneID = 0xd93741e5;

static inline int32_t GetAvatarGender( const EntityAvatar *pAvatar ) {
    // FUN_007f73d0
    return pAvatar->getPooledEntry()->Body.getAvatarPart()->Gender;
}

static inline void SetLayerChannel( Animation *pAnimation,
                                    const int32_t channel,
                                    const float weight ) {
    pAnimation->setChannelLayer( channel, AnimChannel::ALM_BindDelta, weight );
}

static void PlayAnimState( Animation *pAnimation,
                           RenderAnm *pAnim,
                           const int32_t matchID,
                           const uint32_t clipID,
                           const float *pExpressionList ) {
    if ( pAnim->pAnimState == nullptr && pAnim->pRefBas != nullptr ) {
        pAnim->pAnimState =
            gpScenaric->resolveAnimState( nullptr, pAnim->pRefBas );
    }

    const AnimStateInstance *pState = pAnim->pAnimState;
    if ( pState == nullptr ) {
        return;
    }

    std::vector<AnimStateBlendEntry> blendSet;
    pState->getBlendSet( blendSet );
    if ( pExpressionList != nullptr ) {
        for ( size_t i = 0; i < blendSet.size(); i++ ) {
            blendSet[i].Weight = pExpressionList[i * 2];
            blendSet[i].Ratio = pExpressionList[i * 2 + 1];
        }
    }

    blendSet.erase( std::remove_if( blendSet.begin(), blendSet.end(),
                                    []( const AnimStateBlendEntry &entry ) {
                                        return entry.pClip == nullptr;
                                    } ),
                    blendSet.end() );
    if ( blendSet.empty() ) {
        return;
    }

    const int32_t channelMatchID =
        ( matchID < 0 ) ? static_cast<int32_t>( pState->getMatchID() )
                        : matchID;
    pAnimation->playChannel( channelMatchID, clipID, blendSet.data(),
                             static_cast<int32_t>( blendSet.size() ) );
}

AvatarLayer::AvatarLayer()
    : pAvatar( nullptr ), pNext( nullptr ), bEnabled( false ) {}

Animation *AvatarLayer::getAnimation() const {
    return static_cast<Animation *>( pAvatar->getAnimation() );
}

void AvatarLayer::attach( EntityAvatar *pTargetAvatar ) {
    // FUN_008eb920
    pAvatar = pTargetAvatar;
}

void AvatarLayer::detach() {
    // FUN_008eb910
    pAvatar = nullptr;
}

void AvatarLayer::enable() {
    // FUN_008eb8e0
    bEnabled = true;
}

void AvatarLayer::disable() {
    // FUN_008eb8d0
    bEnabled = false;
}

AvatarPoseLayer::AvatarPoseLayer()
    : AvatarLayer(), mode( APM_Body ), pPoseAnims{}, currentPose( kNoPose ) {}

void AvatarPoseLayer::attach( EntityAvatar *pTargetAvatar ) {
    // FUN_008eb840
    pAvatar = pTargetAvatar;
    refresh();
}

void AvatarPoseLayer::detach() {
    // FUN_008eb5c0
    getAnimation()->resetChannel( 0 );
    currentPose = kNoPose;
    for ( RenderAnm *&pAnim : pPoseAnims ) {
        pAnim = nullptr;
    }
    mode = APM_Body;
    pAvatar = nullptr;
}

void AvatarPoseLayer::onResourceChange() {
    // FUN_008eb810
    refresh();
}

void AvatarPoseLayer::onSkeletonChange() {
    // FUN_008eb6c0
    if ( currentPose == kNoPose || pPoseAnims[currentPose] == nullptr ) {
        return;
    }

    Animation *pAnimation = getAnimation();
    SetLayerChannel( pAnimation, 0, kAnimWeightFull );
    PlayAnimState( pAnimation, pPoseAnims[currentPose], -1,
                   static_cast<uint32_t>( currentPose ), nullptr );
}

void AvatarPoseLayer::enable() {
    // FUN_008eb830
    bEnabled = true;
    refresh();
}

void AvatarPoseLayer::disable() {
    // FUN_008eb590
    getAnimation()->resetChannel( 0 );
    currentPose = kNoPose;
    bEnabled = false;
}

void AvatarPoseLayer::refresh() {
    // FUN_008eb780
    if ( pAvatar == nullptr ) {
        return;
    }

    if ( mode == APM_Body && pPoseAnims[AP_Male] != nullptr ) {
        if ( pAvatar->getPooledEntry()->ActiveClotheSet == 1 ) {
            play( AP_Bike );
            return;
        }
        play( ( GetAvatarGender( pAvatar ) == 1 ) ? AP_Female : AP_Male );
        return;
    }

    if ( mode == APM_Head && pPoseAnims[AP_HeadMale] != nullptr ) {
        play( ( GetAvatarGender( pAvatar ) == 1 ) ? AP_HeadFemale
                                                  : AP_HeadMale );
    }
}

void AvatarPoseLayer::play( const int32_t pose ) {
    // FUN_008eb600
    if ( currentPose == pose ) {
        return;
    }

    Animation *pAnimation = getAnimation();
    SetLayerChannel( pAnimation, 0, kAnimWeightFull );
    PlayAnimState( pAnimation, pPoseAnims[pose], -1,
                   static_cast<uint32_t>( pose ), nullptr );
    currentPose = pose;
}

AvatarLookAtLayer::AvatarLookAtLayer()
    : AvatarLayer(),
      blendRatios{},
      pHorizontalAnim( nullptr ),
      pVerticalAnim( nullptr ) {}

void AvatarLookAtLayer::attach( EntityAvatar *pTargetAvatar ) {
    // FUN_008ebf20
    pAvatar = pTargetAvatar;
    if ( bEnabled ) {
        apply();
    }
}

void AvatarLookAtLayer::detach() {
    // FUN_008ebb90
    getAnimation()->resetChannel( 2 );
    getAnimation()->resetChannel( 3 );
    blendRatios[1] = 0.0f;
    blendRatios[3] = 0.0f;
    blendRatios[0] = 0.0f;
    blendRatios[4] = 0.0f;
    blendRatios[5] = 0.0f;
    pHorizontalAnim = nullptr;
    pVerticalAnim = nullptr;
    pAvatar = nullptr;
}

void AvatarLookAtLayer::onResourceChange() {
    // FUN_008eb8a0
    if ( bEnabled ) {
        tick( 0.0f, 0.0f );
    }
}

void AvatarLookAtLayer::enable() {
    // FUN_008ebf10
    bEnabled = true;
    apply();
}

void AvatarLookAtLayer::disable() {
    // FUN_008ebb40
    if ( pAvatar != nullptr ) {
        getAnimation()->resetChannel( 2 );
        getAnimation()->resetChannel( 3 );
    }
    bEnabled = false;
}

void AvatarLookAtLayer::tick( float deltaTime, float totalTime ) {
    // FUN_008ebdc0
    static constexpr float kAreaLeft = 0.29f;   // DAT_00f41a48
    static constexpr float kAreaRight = 0.99f;  // DAT_00f41a44
    static constexpr float kAreaBottom = 0.21f; // DAT_00f41a40
    static constexpr float kAreaTop = 0.71f;    // DAT_00f41a3c
    static constexpr float kAreaScaleX =
        2.0f / ( kAreaRight - kAreaLeft ); // DAT_00f41a38
    static constexpr float kAreaScaleY =
        2.0f / ( kAreaTop - kAreaBottom );      // DAT_00f418d4
    static constexpr float kSmoothing = 5.0f;   // DAT_00f41750
    static constexpr float kMaxRatio = 0.9995f; // DAT_00f41e50

    static float gLookTargetX = 0.0f; // DAT_016a2c5c
    static float gLookTargetY = 0.0f; // DAT_016a2c58

    const float mouseX = gEdFlash.Input.MouseCoordinates[0];
    const float mouseY = 1.0f - gEdFlash.Input.MouseCoordinates[1];
    if ( kAreaLeft <= mouseX && mouseX <= kAreaRight && kAreaBottom <= mouseY &&
         mouseY <= kAreaTop && gEdFlash.Input.bMouseButton0Down ) {
        gLookTargetX = ( mouseX - kAreaLeft ) * kAreaScaleX - 1.0f;
        gLookTargetY = ( mouseY - kAreaBottom ) * kAreaScaleY - 1.0f;
    }

    float &lookX = blendRatios[4];
    float &lookY = blendRatios[5];
    lookX += ( gLookTargetX - lookX ) * deltaTime * kSmoothing;
    lookY += ( gLookTargetY - lookY ) * deltaTime * kSmoothing;

    blendRatios[0] = 1.0f;
    blendRatios[1] = std::min( ( lookX + 1.0f ) * 0.5f, kMaxRatio );
    blendRatios[2] = 1.0f;
    blendRatios[3] = std::min( ( lookY + 1.0f ) * 0.5f, kMaxRatio );

    apply();
}

void AvatarLookAtLayer::apply() {
    // FUN_008ebc00
    if ( pAvatar == nullptr ) {
        return;
    }

    Animation *pAnimation = getAnimation();
    if ( pHorizontalAnim != nullptr ) {
        SetLayerChannel( pAnimation, 2, blendRatios[0] );
        PlayAnimState( pAnimation, pHorizontalAnim, 2, 0, &blendRatios[0] );
    }

    if ( pVerticalAnim != nullptr ) {
        SetLayerChannel( pAnimation, 3, blendRatios[2] );
        PlayAnimState( pAnimation, pVerticalAnim, 3, 1, &blendRatios[2] );
    }
}

AvatarHeelLayer::AvatarHeelLayer() : AvatarLayer(), pHeelAnim( nullptr ) {}

void AvatarHeelLayer::attach( EntityAvatar *pTargetAvatar ) {
    // FUN_008ec080
    pAvatar = pTargetAvatar;
    refresh();
}

void AvatarHeelLayer::detach() {
    // FUN_008ebb10
    getAnimation()->resetChannel( 1 );
    pAvatar = nullptr;
}

void AvatarHeelLayer::onResourceChange() {
    // FUN_008ec0a0
    refresh();
}

void AvatarHeelLayer::enable() {
    // FUN_008ec070
    bEnabled = true;
    refresh();
}

void AvatarHeelLayer::disable() {
    // FUN_008ebae0
    getAnimation()->resetChannel( 1 );
    bEnabled = false;
}

void AvatarHeelLayer::refresh() {
    // FUN_008ebf40
    static constexpr uint32_t kHeelClipID = 0x9a;

    if ( !bEnabled || pAvatar == nullptr ) {
        return;
    }

    PooledAvatarEntry *pEntry = pAvatar->getPooledEntry();
    const AvatarClothePart *pShoes = gpAvatar->findClotheByID(
        pEntry->pClothesHC[pEntry->ActiveClotheSet][AC_Shoes] );
    if ( pShoes != nullptr && pShoes->Size == ACS_Large &&
         pHeelAnim != nullptr ) {
        Animation *pAnimation = getAnimation();
        SetLayerChannel( pAnimation, 1, kAnimWeightFull );
        PlayAnimState( pAnimation, pHeelAnim, 1, kHeelClipID, nullptr );
        return;
    }

    getAnimation()->resetChannel( 1 );
}

void AvatarHelmetLayer::detach() {
    // FUN_008eb870
    pAvatar->setForceShowHelmet( false );
}

void AvatarHelmetLayer::onPoseComputed() {
    // FUN_008ec0b0
    Animation *pAnimation = getAnimation();
    if ( pAnimation == nullptr || !pAnimation->isValid() ) {
        return;
    }

    const int32_t helmetNode =
        pAnimation->getSkeleton()->findNode( kHelmetBoneID );
    const Matrix4x4f_t helmetMatrix =
        pAnimation->getNodeWorldMatrix( -1, helmetNode ) *
        pAvatar->getModelMatrix();
    pAvatar->getPooledEntry()->Slots[AC_Helmet].setMatrix( helmetMatrix );
}

void AvatarHelmetLayer::tick( float deltaTime, float totalTime ) {
    // FUN_008eb960
    if ( pAvatar != nullptr ) {
        pAvatar->setForceShowHelmet(
            pAvatar->getPooledEntry()->ActiveClotheSet == 1 );
    }
}
