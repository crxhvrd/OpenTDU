#pragma once

#include "game/mng_base.h"

class Entity;

class MngZone : public Manager {
public:
    const char *getName() const override { return "ZONE"; /* FUN_0042e900 */ }

public:
    MngZone();
    ~MngZone();

    void terminate() override;

    void registerZone( Entity *pZone );
    void unregisterZone( Entity *pZone );

    void registerVolume( Entity *pVolume );
    void unregisterVolume( Entity *pVolume );

    inline int32_t getNumVolumes() const { return numVolume; }
    inline Entity *getVolume( const int32_t index ) const {
        return pVolumes[index];
    }

protected:
    static constexpr int32_t kMaxNumEntity = 256;

protected:
    int32_t numVolume;
    int32_t numZone;
    std::array<Entity *, kMaxNumEntity> pVolumes;
    std::array<Entity *, kMaxNumEntity> pZones;
    int32_t field_0x828;
    int32_t field_0x82c;
};

extern MngZone *gpMngZone;
