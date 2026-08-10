#pragma once

#include "base_entity.h"

#include <vector>

class EntityBoatPath : public Entity {
public:
    inline int32_t getWaypointCount() const {
        return static_cast<int32_t>( waypointVolumes.size() );
    }
    inline int32_t getBoatGroupIndex() const {
        int32_t groupIndex = 0;
        memcpy( &groupIndex, &Floats[0], sizeof( groupIndex ) );
        return groupIndex;
    }
    inline float getMinBoatSpacing() const { return Floats[1]; }
    inline float getMaxBoatSpacing() const { return Floats[2]; }
    inline float getBoatSpeed() const { return Floats[3]; }
    inline bool isLooping() const { return bFlag; }

public:
    EntityBoatPath();
    ~EntityBoatPath();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    bool getWaypointPosition( int32_t index, Eigen::Vector4f &outPos ) const;
    const Matrix4x4f_t *getWaypointMatrix( int32_t index ) const;
    uint32_t getWaypointMode( int32_t index ) const;
    float getWaypointParameter( int32_t index ) const;

protected:
    // [count:u32][ guid:u64, mode:u32, parameter:f32 ] * count
    static constexpr uint32_t kRecordStride = 0x10;

protected:
    const uint8_t *getRecord( int32_t index ) const;

protected:
    uint8_t *pRecordArray;
    float Floats[4];
    bool bFlag;
    std::vector<Entity *> waypointVolumes;
};
