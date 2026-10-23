#pragma once

#include "entity_actor.h"

// Spot Interior
class EntityInterior : public EntityActor {
public:
    EntityInterior();
    ~EntityInterior();

    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
};
