#include "shared.h"
#include "entity_micro.h"

#include "game/cinematic/cin_res_collection.h"
#include "game/game_mechanism.h"
#include "filesystem/binary.h"

EntityMicro::EntityMicro() : Entity(), Flags( 0 ) {
    // FUN_009da130
}

EntityMicro::~EntityMicro() {
    // FUN_00424120
}

void EntityMicro::initialize() {
    // FUN_007e05d0
    setTransformFromDesc();

    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }
}

void EntityMicro::loadResources( ScenaricBank *param_2 ) {
    // FUN_007e0580
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();
}

void EntityMicro::unloadResources() {
    // FUN_007e0270
}

void EntityMicro::tick( PlayerGameMech *pGameMech,
                        float deltaTime,
                        float totalTime ) {
    // FUN_007e05c0
    Entity::tick( pGameMech, deltaTime, totalTime );
}

bool EntityMicro::draw( RenderScene * ) {
    // FUN_007e0280
    return false;
}

void EntityMicro::deserialize( BinaryReader *param_2 ) {
    // FUN_007e0550
    Entity::deserialize( param_2 );
    Flags = 0;
}

void EntityMicro::initializeForCinematicKeyframed( void *param_2,
                                                   void *param_3 ) {
    // FUN_007e0290
    const CinActorState *pState = static_cast<const CinActorState *>( param_2 );
    const CinResCollection *pInterface =
        static_cast<const CinResCollection *>( param_3 );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: CINEMATIC

    Matrix4x4f_t transform = pInterface->buildActorMatrix( *pState );
    setTransform( transform, true );
}

void EntityMicro::setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) {
    // FUN_007e0470
    bShifted = !param_1;
    modelMatrix = transformMatrix;
}

void EntityMicro::setPosition( Eigen::Vector4f &position, bool param_1 ) {
    // FUN_007e0360
    bShifted = !param_1;
    modelMatrix.row( 3 ) = position;
}

void EntityMicro::reset() {
    // FUN_007e0570
    if ( pMechanism != nullptr ) {
        pMechanism->reInit();
    }
}

void EntityMicro::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007e0540
    Entity::instanciate( pMngActor, param_1 );
}
