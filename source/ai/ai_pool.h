#pragma once

#include "ai/ai_road_shape.h"

class AiPool {
public:
    static constexpr int32_t kNumRoadShapes = 32;

public:
    inline void setField0xC( const bool bValue ) { field_0xc = bValue; }
    inline void setField0x7C( const bool bValue ) { field_0x7c = bValue; }
    inline void setField0x85( const bool bValue ) { field_0x85 = bValue; }
    inline int32_t getField0x2448C() const { return field_0x2448c; }

public:
    inline AiRoadShape &getShape( const int32_t index ) {
        return roadShapes[index];
    }

    AiRoadShape *getUnusedShape();

    void removeAllVehicles( const bool bResetPool );
    void removeBots();
    void removeCops();
    void removeParkedVehicles();
    void removeOpponents();
    void removeFlaggedVehicles();

private:
    bool field_0xc = false;
    bool field_0x7c = false;
    bool field_0x85 = false;
    int32_t field_0x2448c = 0;
    AiRoadShape roadShapes[kNumRoadShapes];
};
