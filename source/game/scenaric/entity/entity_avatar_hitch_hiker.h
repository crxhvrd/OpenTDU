#pragma once

#include "entity_avatar_passenger.h"

class EntityAvatarHitchHiker : public EntityAvatarPassenger {
public:
    EntityAvatarHitchHiker();
    ~EntityAvatarHitchHiker();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void resetVoice() override;

    void initializeVoice( const uint32_t voiceType );
    void reactToPenalty( const float pendingRatio );
    inline bool isVoicePlaying() const { return bVoicePlaying; }

protected:
    bool bVoicePlaying;
    uint32_t RandomSeed;
    float CumulatedProba;
    int32_t CurrentFreerideAnim;
    bool bPlaySound;
};
