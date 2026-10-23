#include "shared.h"
#include "entity_avatar_shopping_girl.h"

#include "game/scenaric/gs_scenaric.h"
#include "render/camera/gs_camera.h"

EntityAvatarShoppingGirl::EntityAvatarShoppingGirl()
    : EntityAvatarPassenger(), bVoicePlaying( false ), RandomSeed( 0x3039 ) {
    // FUN_007f38f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO
}

EntityAvatarShoppingGirl::~EntityAvatarShoppingGirl() {
    // FUN_00424fc0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO
}

void EntityAvatarShoppingGirl::tick( GameMechanism *pGameMech,
                                     float deltaTime,
                                     float totalTime ) {
    // FUN_007f3af0
    EntityAvatarPassenger::tick( pGameMech, deltaTime, totalTime );

    if ( CurrentBehavior == PB_Passenger ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO
    }
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
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (stop the voice instance, reset the
                             // pending voice line)
    bVoicePlaying = false;
}

void EntityAvatarShoppingGirl::initializeVoice( const uint32_t /*voiceType*/ ) {
    // FUN_007f3c00
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (voice bank for the voice type,
                             // audio client and stream entry)
    bVoicePlaying = false;
}

void EntityAvatarShoppingGirl::reactToPenalty( const float /*pendingRatio*/ ) {
    // FUN_007f33f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (random penalty voice line)
}
