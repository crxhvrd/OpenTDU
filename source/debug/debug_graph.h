#pragma once

#include <vector>

class DrawList;

struct DebugGraphPoint {
    float X;
    float Y;
};

struct DebugGraphCurve {
    int32_t Capacity = 0;
    int32_t NumPoints = 0;
    int32_t Head = 0;
    int32_t Tail = 0;
    std::vector<DebugGraphPoint> Points;
    float MinY = 0.0f;
    float MaxY = 0.0f;
    float TimeWindow = 0.0f;
    uint8_t Color[4] = { 0, 0, 0, 0 }; // RGBA
    float LastX = 0.0f;
    bool bAutoRange = false;
    bool bVisible = false;
    float LastRangeUpdate = 0.0f;
    char Name[0x20] = {};

    bool initialize( const int32_t capacity,
                     const float minY,
                     const float maxY,
                     const float timeWindow );
    void addPoint( const float x, const float y );
    void computeRange( float &min, float &max ) const;
    void setName( const char *pName );
    void draw( DrawList *pDrawList,
               const float left,
               const float right,
               const float bottom,
               const float top,
               const bool bScrolling,
               const float time );

    inline void setColor( const uint8_t r,
                          const uint8_t g,
                          const uint8_t b,
                          const uint8_t a ) {
        Color[0] = r;
        Color[1] = g;
        Color[2] = b;
        Color[3] = a;
    }
};

class DebugGraph {
public:
    inline int32_t getNumCurves() const {
        return static_cast<int32_t>( curves.size() );
    }
    inline DebugGraphCurve &getCurve( const int32_t index ) {
        return curves[index];
    }
    inline bool isFrozen() const { return bFrozen; }
    inline void setFrozen( const bool bState ) { bFrozen = bState; }
    inline void setShowNames( const bool bState ) { bShowNames = bState; }
    inline void setShowRange( const bool bState ) { bShowRange = bState; }
    inline void setAutoScale( const bool bState ) { bAutoScale = bState; }
    inline void setScrolling( const bool bState ) { bScrolling = bState; }
    inline void setDrawList( DrawList *pList ) { pDrawList = pList; }
    inline void
    setBounds( const float l, const float t, const float r, const float b ) {
        left = l;
        top = t;
        right = r;
        bottom = b;
    }

public:
    DebugGraph();

    bool create( const int32_t numCurves,
                 DrawList *pList,
                 const float minY,
                 const float maxY,
                 const int32_t capacity,
                 const float timeWindow );
    void release();
    void draw( float time );
    void clear();
    void setAutoRange( const bool bEnabled );

private:
    void autoScale();

private:
    std::vector<DebugGraphCurve> curves;
    DrawList *pDrawList;
    bool bFrozen;
    float left;
    float top;
    float right;
    float bottom;
    bool bShowNames;
    bool bShowRange;
    bool bAutoScale;
    bool bScrolling;
};
