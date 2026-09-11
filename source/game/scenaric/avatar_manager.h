#pragma once

#include "game/scenaric/scenaric_bank.h"

class EntityAvatar;
struct AvatarConfiguration;

class AvatarManager {
public:
    inline ScenaricBank &getBank() { return avatarBank; }

public:
    AvatarManager();
    ~AvatarManager();

    void initialize();
    void tick( const float deltaTime, const float totalTime );
    void draw();
    void releaseAllAvatars();

    EntityAvatar *allocateAvatar( const uint64_t hashcode, const int32_t type );
    EntityAvatar *createAvatar( const AvatarConfiguration *pConfig,
                                const int32_t clotheSet,
                                const bool bAsync,
                                const uint64_t hashcode,
                                const bool bCritical,
                                const int32_t type );
    void releaseAvatar( EntityAvatar *pAvatar );

    EntityAvatar *getDriverAvatar();

    inline bool getUnknownBoolean() const { return bUnknownBoolean; }

private:
    static constexpr int32_t kMaxAvatars = 9;

private:
    struct PlayerAvatar {
        EntityAvatar *pAvatar = nullptr;
        uint64_t Hashcode = 0;
        uint32_t ReferenceCount = 0;
    };

private:
    ScenaricBank avatarBank;
    PlayerAvatar playerAvatars[kMaxAvatars];
    std::array<uint32_t, kMaxAvatars> animFrameIndex;
    std::array<uint32_t, kMaxAvatars> animFrameStart;

    uint32_t currentFrameIndex;
    bool bIsFullUpdate;
    bool bUnknownBoolean;

private:
    EntityAvatar *createAvatarInstance( int32_t type );
    void destroyAvatarInstance( EntityAvatar *pAvatar );
    void releaseDriverAvatar();
};
