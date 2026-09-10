#pragma once

#include "base_entity.h"

#include "core/tcb_spline.h"

class EntityBoatTour : public Entity {
public:
    inline uint32_t getTourType() const { return TourType; }
    inline Entity *getBoat() const {
        return bReferencesResolved && pBoatRef != nullptr
                   ? *reinterpret_cast<Entity **>( pBoatRef )
                   : nullptr;
    }
    inline Entity *getCamera() const {
        return bReferencesResolved && pCameraRef != nullptr
                   ? *reinterpret_cast<Entity **>( pCameraRef )
                   : nullptr;
    }
    inline const TCBSplinePosition &getPathSpline() const { return Spline; }

public:
    EntityBoatTour();
    ~EntityBoatTour();

    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void deserialize( BinaryReader *param_2 ) override;

protected:
    uint8_t *pPathRef;
    uint8_t *pBoatRef;
    uint8_t *pCameraRef;
    uint32_t TourType;
    TCBSplinePosition Spline;
    bool bReferencesResolved = false;
};
