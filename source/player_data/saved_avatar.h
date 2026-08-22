#pragma once

#include "game/avatar/avatar_configuration.h"

class edStore;

class SavedAvatar {
public:
    static constexpr uint32_t kAvatarDataSize = 0x1002u;

public:
    SavedAvatar();
    ~SavedAvatar();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream );

public:
    uint16_t CRC = 0u;                        
    AvatarConfiguration Config;               
    uint8_t AvatarData[kAvatarDataSize] = {}; 
    uint32_t AvatarDataSize = 0u;
};
