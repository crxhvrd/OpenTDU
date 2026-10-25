#pragma once

#include "entity_avatar_passenger.h"

class EntityAvatarHitchHiker : public EntityAvatarPassenger {
public:
    EntityAvatarHitchHiker();
    ~EntityAvatarHitchHiker();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void resetVoice() override;

    void initializeVoice( const uint32_t voiceType );
    void reactToPenalty( const float pendingRatio );
    inline bool isVoicePlaying() const { return voice.isPlaying(); }

protected:
    PassengerVoiceBank SoundsBank;
    PassengerVoicePlayer voice;
    uint32_t DrivingSoundPlayCounts[kNumPassengerDrivingSounds];
    int32_t CurrentSoundIndex;
    RandomGen Random;
    float CumulatedProba;
    int32_t CurrentFreerideAnim;
    bool bPlaySound;
};
