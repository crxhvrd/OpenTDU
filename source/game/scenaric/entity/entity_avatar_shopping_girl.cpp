#include "shared.h"
#include "entity_avatar_shopping_girl.h"

#include "core/maths_helper.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/hiearchy_node.h"
#include "world/gs_world_streaming.h"

static constexpr const char *kShoppingGirlVoiceNames[] = {
    "standard", "trucker", "seductive",
    "stuckup",  "foreign", "hurry" }; // PTR_s_standard_00f97008
static constexpr const char *kShoppingGirlSoundNameFormat =
    "V_F_Shopping_%s_%03d.wav";
static constexpr uint32_t kVoiceRandomSeed = 0x3039;
static constexpr int32_t kCarIKPointPassengerBack = 11; // "Back_HR_R"
static constexpr uint32_t kShowFlagViewCockpit = 0x40000;

EntityAvatarShoppingGirl::EntityAvatarShoppingGirl()
    : EntityAvatarPassenger(),
      ChatterTimer( 0.0f ),
      SoundsBank( "sg",
                  kShoppingGirlSoundNameFormat,
                  kNumPassengerDrivingSounds + kNumChatterSounds ),
      voice(),
      DrivingSoundPlayCounts{},
      bPlayingChatter( false ),
      Random(),
      CumulatedProba( 0.0f ),
      ChatterSoundIndex( -1 ),
      bLastChatterPlayed( true ),
      DrivingSoundIndex( -1 ),
      CurrentFreerideAnim( -1 ) {
    // FUN_007f38f0
    Random.setSeed( kVoiceRandomSeed );
}

EntityAvatarShoppingGirl::~EntityAvatarShoppingGirl() {
    // FUN_00424fc0
    voice.destroy();
    SoundsBank.release();
}

void EntityAvatarShoppingGirl::tick( GameMechanism *pGameMech,
                                     float deltaTime,
                                     float totalTime ) {
    // FUN_007f3af0
    EntityAvatarPassenger::tick( pGameMech, deltaTime, totalTime );

    if ( CurrentBehavior == PB_Passenger ) {
        tickVoice( deltaTime );
    }
}

Eigen::Vector4f EntityAvatarShoppingGirl::computeChatterPosition() const {
    // FUN_007f35b0
    Matrix4x4f_t backWorld = Matrix4x4f_t::Identity();
    AccumulateNodeWorldMatrix( CarIKPoints[kCarIKPointPassengerBack].pNode,
                               backWorld );
    Eigen::Vector4f position = backWorld.row( 3 ).transpose();
    if ( pCarState != nullptr ) {
        position = Mat4x4fTransform( pCarState->getBody()->OffsetedWorldMatrix,
                                     position );
    }
    position.w() = 1.0f;
    return position;
}

void EntityAvatarShoppingGirl::tickVoice( const float deltaTime ) {
    // FUN_007f3650
    static constexpr float kChatterPeriod = 10.0f; // FLOAT_00f4166c

    CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera != nullptr &&
         ( pCamera->getFlags() & kShowFlagViewCockpit ) != 0 ) {
        Eigen::Vector4f position = computeChatterPosition();
        if ( gpWorldStreaming != nullptr ) {
            position.head<3>() += gpWorldStreaming->getPosition().head<3>();
        }

        bool bWaiting = false;
        if ( pCamera->sphereInFrustum( &position, 0.0f ) != 0 ) {
            ChatterTimer += deltaTime;
            bWaiting = ChatterTimer < kChatterPeriod;
            if ( !bWaiting ) {
                if ( AnimPeepin.File != 0 ) {
                    requestStandAnim( &AnimPeepin );
                }
                rollChatter();
            }
        }
        if ( !bWaiting ) {
            ChatterTimer = 0.0f;
        }
    }

    voice.tick();
}

void EntityAvatarShoppingGirl::rollChatter() {
    // FUN_007f32f0
    static constexpr float kFirstChatterThreshold = 0.2f; // DAT_00f41730
    static constexpr float kChatterThreshold = 0.1f;      // FLOAT_00f41668
    static constexpr float kChatterAgainThreshold = 0.4f; // DAT_00f4175c

    if ( voice.hasPendingSound() ) {
        return;
    }

    const float roll = RandomUnit( Random );
    bool bSilent = false;
    if ( ChatterSoundIndex == -1 ) {
        bSilent = roll < kFirstChatterThreshold;
        ChatterSoundIndex = 0;
    } else {
        bSilent =
            CumulatedProba + roll <
            ( bLastChatterPlayed ? kChatterAgainThreshold : kChatterThreshold );
    }
    bLastChatterPlayed = !bSilent;

    if ( !bSilent ) {
        const int32_t soundIndex =
            kNumPassengerDrivingSounds + ChatterSoundIndex;
        if ( SoundsBank.isReady() ) {
            voice.queue( SoundsBank.getSound( soundIndex ),
                         SoundsBank.getSoundName( soundIndex ) );
        }
        bPlayingChatter = true;
        ChatterSoundIndex = 1 - ChatterSoundIndex;
        CumulatedProba = 0.0f;
        return;
    }
    CumulatedProba += roll;
}

void EntityAvatarShoppingGirl::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f3aa0
    EntityAvatarPassenger::loadResources( param_2 );

    if ( AnimHi.File != 0 ) {
        AnimHi.pClip = gpScenaric->resolveClip( param_2, AnimHi.pRefBas );
    }

    if ( AnimPeepin.File != 0 ) {
        AnimPeepin.pClip =
            gpScenaric->resolveClip( param_2, AnimPeepin.pRefBas );
    }
}

bool EntityAvatarShoppingGirl::draw( RenderScene *pScene ) {
    // FUN_007f39c0
    CameraGame *pActiveCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;

    if ( pActiveCamera != nullptr ) {
        const uint32_t showFlags = pActiveCamera->getFlags();
        const bool bIsSeated = ( CurrentBehavior == 2 || CurrentBehavior == 1 );

        if ( !bIsSeated || ( showFlags & 0x10000 ) == 0 ||
             ( showFlags & 0x40000 ) != 0 ) {
            return EntityAvatar::draw( pScene );
        }
    }

    return true;
}

void EntityAvatarShoppingGirl::deserialize( BinaryReader *param_2 ) {
    // FUN_007f3a50
    deserializeAnims( param_2, 0x19 );
}

void EntityAvatarShoppingGirl::instanciate( MngActor *pMngActor,
                                            Entity *param_1 ) {
    // FUN_007f3a00
    EntityAvatarPassenger::instanciate( pMngActor, param_1 );

    EntityAvatarShoppingGirl *pTarget =
        static_cast<EntityAvatarShoppingGirl *>( param_1 );

    pTarget->AnimHi.ID = AnimHi.ID;
    pTarget->AnimHi.File = AnimHi.File;
    pTarget->AnimHi.pRefBas = AnimHi.pRefBas;

    pTarget->AnimPeepin.ID = AnimPeepin.ID;
    pTarget->AnimPeepin.File = AnimPeepin.File;
    pTarget->AnimPeepin.pRefBas = AnimPeepin.pRefBas;
}

void EntityAvatarShoppingGirl::resetVoice() {
    // FUN_007f34a0
    voice.requestStop();
    voice.clear();
    ChatterSoundIndex = -1;
    DrivingSoundIndex = -1;
    bPlayingChatter = false;
    bLastChatterPlayed = true;
    for ( uint32_t &playCount : DrivingSoundPlayCounts ) {
        playCount = 0u;
    }
}

void EntityAvatarShoppingGirl::initializeVoice( const uint32_t voiceType ) {
    // FUN_007f3c00
    const uint32_t numVoices = sizeof( kShoppingGirlVoiceNames ) /
                               sizeof( kShoppingGirlVoiceNames[0] );
    SoundsBank.load(
        kShoppingGirlVoiceNames[voiceType < numVoices ? voiceType : 0] );
    voice.create( "SCAvatarShoppingGirl" );
    ChatterSoundIndex = -1;
    DrivingSoundIndex = -1;
    bPlayingChatter = false;
    bLastChatterPlayed = true;
}

void EntityAvatarShoppingGirl::reactToPenalty( const float pendingRatio ) {
    // FUN_007f33f0
    static constexpr float kAlwaysReactRatio = 1.0f; // FLOAT_00f4165c
    if ( voice.hasPendingSound() ) {
        return;
    }

    const float proba = RandomUnit( Random ) + CumulatedProba;
    if ( kAlwaysReactRatio - pendingRatio < proba ) {
        const int32_t soundIndex =
            PickLeastPlayedDrivingSound( DrivingSoundPlayCounts, Random );
        if ( SoundsBank.isReady() ) {
            voice.queue( SoundsBank.getSound( soundIndex ),
                         SoundsBank.getSoundName( soundIndex ) );
        }
        bPlayingChatter = false;
        DrivingSoundIndex = soundIndex;
        CumulatedProba = 0.0f;
        return;
    }

    CumulatedProba += proba;
}
