#pragma once

#include "base_entity.h"
#include "entity_bytecode.h"

class edAudioSample;
class EntityCinematic;

class EntityAircraftPath : public Entity {
public:
    static constexpr uint32_t kMaxPathPoints = 25;

public:
    enum ePathPointType : uint32_t {
        APPT_Waypoint = 0,  // fly through the volume origin
        APPT_Scatter = 1,   // pick a random point inside the volume
        APPT_CineStart = 2, // landing cinematic hand-over marker
        APPT_CineStop = 3,  // landing cinematic release marker
    };

public:
    inline uint32_t getPathPointCount() const { return NumPathPoints; }
    inline uint32_t getPathPointType( uint32_t index ) const {
        return pPathPoints[index].Value;
    }
    Entity *getPathPointVolume( uint32_t index ) const;

    inline int32_t getAircraftType() const { return Type; }
    inline float getMinDistanceBetweenAircrafts() const { return Floats[0]; }
    inline float getMaxDistanceBetweenAircrafts() const { return Floats[1]; }
    inline float getSpeedOnGround() const { return Floats[2]; }
    inline float getSpeedOnAir() const { return Floats[3]; }

    inline const edAudioSample *getSoundOnGroundSample() const {
        return pSoundOnGroundSample;
    }
    inline const edAudioSample *getSoundInAirSample() const {
        return pSoundInAirSample;
    }
    EntityCinematic *getCinematic() const;

public:
    EntityAircraftPath();
    ~EntityAircraftPath();

    void loadResources( ScenaricBank *param_2 ) override;
    void deserialize( BinaryReader *param_2 ) override;

protected:
    BytecodePair pPathPoints[kMaxPathPoints];
    uint32_t NumPathPoints;
    int32_t Type;
    float Floats[4];
    uint8_t *pSoundOnGround;
    uint8_t *pSoundInAir;
    uint8_t *pCinematic;
    const edAudioSample *pSoundOnGroundSample;
    const edAudioSample *pSoundInAirSample;
    std::unique_ptr<edAudioSample> soundOnGroundStorage;
    std::unique_ptr<edAudioSample> soundInAirStorage;
};
