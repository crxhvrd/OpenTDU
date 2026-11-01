#pragma once

#include "entity_avatar_passenger.h"

class EntityAvatarShoppingGirl : public EntityAvatarPassenger {
public:
    EntityAvatarShoppingGirl();
    ~EntityAvatarShoppingGirl();

    void loadResources( ScenaricBank *param_2 ) override;
    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void resetVoice() override;
    void resetFreerideAnim() override {
        // FUN_007f2fe0
        CurrentFreerideAnim = -1;
    }

    void initializeVoice( const uint32_t voiceType );
    void reactToPenalty( const float pendingRatio );
    inline bool isVoicePlaying() const { return voice.isPlaying(); }

protected:
    static constexpr int32_t kNumChatterSounds = 2;

protected:
    void tickVoice( const float deltaTime );
    void rollChatter();
    Eigen::Vector4f computeChatterPosition() const;

protected:
    float ChatterTimer;
    PassengerVoiceBank SoundsBank;
    PassengerVoicePlayer voice;
    uint32_t DrivingSoundPlayCounts[kNumPassengerDrivingSounds];
    bool bPlayingChatter;
    RandomGen Random;
    float CumulatedProba;
    int32_t ChatterSoundIndex;
    bool bLastChatterPlayed;
    int32_t DrivingSoundIndex;
    int32_t CurrentFreerideAnim;
};
