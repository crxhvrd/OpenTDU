#include "shared.h"
#include "debug_graph.h"

#include "render/draw_list.h"
#include "render/text_renderer.h"
#include "system/ed_sys.h"

#include <cstdio>
#include <cstring>

static constexpr float kMinRange = 0.1f;          // FLOAT_00f41668
static constexpr float kRangeUpdatePeriod = 1.0f; // FLOAT_00f4165c
static constexpr float kRangeTextWidth = 5.0f;    // DAT_00f41750
static constexpr int32_t kLineHeight = 12;

static constexpr uint32_t kBorderColor = 0xffffffff;

static int32_t WrapIndex( const int32_t index, const int32_t capacity ) {
    return ( index >= capacity ) ? 0 : index;
}

static float Saturate( const float value ) {
    if ( value < 0.0f ) {
        return 0.0f;
    }
    return ( 1.0f < value ) ? 1.0f : value;
}

bool DebugGraphCurve::initialize( const int32_t capacity,
                                  const float minY,
                                  const float maxY,
                                  const float timeWindow ) {
    // FUN_006dd3e0
    TimeWindow = timeWindow;
    MinY = minY;
    MaxY = maxY;
    Capacity = capacity;
    LastX = 0.0f;
    NumPoints = 0;
    Head = 0;
    Tail = 0;
    bAutoRange = false;
    bVisible = true;
    strcpy( Name, "NONAME" );
    LastRangeUpdate = -1.0f;
    setColor( 0xff, 0x00, 0xff, 0xff );
    Points.resize( Capacity );
    return !Points.empty();
}

void DebugGraphCurve::addPoint( const float x, const float y ) {
    // FUN_006dcef0
    LastX = x;
    if ( Points.empty() || Capacity <= 0 ) {
        return;
    }

    if ( 0 < NumPoints ) {
        int32_t previous = Tail - 1;
        if ( previous < 0 ) {
            previous = Capacity - 1;
        }
        if ( x < Points[previous].X ) {
            return;
        }
    }

    Points[Tail].X = x;
    Points[Tail].Y = y;
    if ( bAutoRange ) {
        if ( y < MinY || NumPoints == 0 ) {
            MinY = y;
        }
        if ( MaxY < y || NumPoints == 0 ) {
            MaxY = y;
        }
        if ( MaxY <= MinY ) {
            MaxY = MinY + kMinRange;
        }
    }

    Tail = WrapIndex( Tail + 1, Capacity );
    if ( Head == Tail ) {
        Head = WrapIndex( Head + 1, Capacity );
    } else {
        NumPoints++;
    }
}

void DebugGraphCurve::computeRange( float &min, float &max ) const {
    // FUN_006dce90
    if ( Head == Tail ) {
        return;
    }

    min = Points[Head].Y;
    max = Points[Head].Y;
    for ( int32_t i = Head; i != Tail; i = WrapIndex( i + 1, Capacity ) ) {
        if ( Points[i].Y < min ) {
            min = Points[i].Y;
        }
        if ( max < Points[i].Y ) {
            max = Points[i].Y;
        }
    }
}

void DebugGraphCurve::setName( const char *pName ) {
    // FUN_006dce60
    strncpy( Name, pName, sizeof( Name ) );
    Name[sizeof( Name ) - 1] = '\0';
}

void DebugGraphCurve::draw( DrawList *pDrawList,
                            const float left,
                            const float right,
                            const float bottom,
                            const float top,
                            const bool bScrolling,
                            const float time ) {
    // FUN_006dd0c0
    if ( bAutoRange && ( LastRangeUpdate < 0.0f || time < LastRangeUpdate ||
                         kRangeUpdatePeriod <= time - LastRangeUpdate ) ) {
        computeRange( MinY, MaxY );
        if ( MaxY <= MinY ) {
            MaxY = MinY + kMinRange;
        }
        LastRangeUpdate = time;
    }

    float startX = LastX - TimeWindow;
    float endX = LastX;
    if ( !bScrolling ) {
        endX = time;
        startX = ( NumPoints < 1 ) ? time - 1.0f : Points[0].X;
    }

    int32_t first = Head;
    for ( int32_t i = Head; i != Tail; i = WrapIndex( i + 1, Capacity ) ) {
        if ( startX <= Points[i].X ) {
            first = i;
            break;
        }
    }

    int32_t last = Head;
    if ( Tail != Head ) {
        int32_t i = Tail;
        do {
            i--;
            if ( i < 0 ) {
                i = Capacity - 1;
            }
            if ( Points[i].X <= endX ) {
                last = i;
                break;
            }
        } while ( i != Head );
    }
    last = WrapIndex( last + 1, Capacity );

    const int32_t numVertices =
        ( last < first ) ? Capacity - first + last : last - first;

    pDrawList->setActiveMaterial( nullptr );
    pDrawList->setIdentityTransform();

    const float rangeY = MaxY - MinY;
    const float zeroY = ( -1.0f / rangeY ) * MinY * ( top - bottom ) + bottom;
    if ( ( bottom <= zeroY && zeroY <= top ) ||
         ( zeroY <= bottom && top <= zeroY ) ) {
        const uint32_t axisColor = ( ( Color[3] >> 1 ) << 24 ) |
                                   ( Color[0] << 16 ) | ( Color[1] << 8 ) |
                                   Color[2];
        pDrawList->setDiffuse( 0, ColorRGBA( axisColor ) );
        if ( pDrawList->beginPrimitive( ePrimitiveType::PT_LineStrip, 2 ) ) {
            pDrawList->pushVertex( left, zeroY, 0.0f );
            pDrawList->pushVertex( right, zeroY, 0.0f );
            pDrawList->commitPrimitive();
        }
    }

    const uint32_t curveColor =
        ( Color[3] << 24 ) | ( Color[0] << 16 ) | ( Color[1] << 8 ) | Color[2];
    pDrawList->setDiffuse( 0, ColorRGBA( curveColor ) );
    if ( !pDrawList->beginPrimitive( ePrimitiveType::PT_LineStrip,
                                     numVertices ) ) {
        return;
    }
    for ( int32_t i = first; i != last; i = WrapIndex( i + 1, Capacity ) ) {
        const float tx =
            Saturate( ( Points[i].X - startX ) / ( endX - startX ) );
        const float ty = Saturate( ( Points[i].Y - MinY ) / rangeY );
        pDrawList->pushVertex( ( right - left ) * tx + left,
                               ( top - bottom ) * ty + bottom, 0.0f );
    }
    pDrawList->commitPrimitive();
}

DebugGraph::DebugGraph()
    : pDrawList( nullptr ),
      bFrozen( false ),
      left( 0.0f ),
      top( 0.0f ),
      right( 1.0f ),
      bottom( 1.0f ),
      bShowNames( true ),
      bShowRange( true ),
      bAutoScale( false ),
      bScrolling( true ) {}

bool DebugGraph::create( const int32_t numCurves,
                         DrawList *pList,
                         const float minY,
                         const float maxY,
                         const int32_t capacity,
                         const float timeWindow ) {
    // FUN_006dd5f0
    curves.resize( numCurves );
    pDrawList = pList;
    for ( DebugGraphCurve &curve : curves ) {
        if ( !curve.initialize( capacity, minY, maxY, timeWindow ) ) {
            release();
            return false;
        }
    }
    return true;
}

void DebugGraph::release() {
    // FUN_006dd550
    curves.clear();
}

void DebugGraph::clear() {
    // FUN_006dcd80
    for ( DebugGraphCurve &curve : curves ) {
        curve.Tail = curve.Head;
        curve.NumPoints = 0;
    }
}

void DebugGraph::setAutoRange( const bool bEnabled ) {
    // FUN_006dd4d0
    for ( DebugGraphCurve &curve : curves ) {
        if ( curve.bAutoRange == bEnabled ) {
            continue;
        }

        curve.bAutoRange = bEnabled;
        if ( !bEnabled ) {
            continue;
        }

        curve.computeRange( curve.MinY, curve.MaxY );
        if ( curve.MaxY <= curve.MinY ) {
            curve.MaxY = curve.MinY + kMinRange;
        }
    }
}

void DebugGraph::autoScale() {
    // FUN_006dd770
    bool bFirst = true;
    float min = 0.0f;
    float max = 1.0f;
    float curveMin = 0.0f;
    float curveMax = 1.0f;
    for ( const DebugGraphCurve &curve : curves ) {
        if ( !curve.bVisible ) {
            continue;
        }
        curve.computeRange( curveMin, curveMax );
        if ( bFirst ) {
            bFirst = false;
            min = curveMin;
            max = curveMax;
        } else {
            if ( curveMin <= min ) {
                min = curveMin;
            }
            if ( max <= curveMax ) {
                max = curveMax;
            }
        }
    }

    for ( DebugGraphCurve &curve : curves ) {
        curve.bAutoRange = false;
        curve.MinY = min;
        curve.MaxY = max;
        if ( max <= min ) {
            curve.MaxY = min + kMinRange;
        }
    }
}

static void PrintGraphText( const int32_t x,
                            const int32_t y,
                            const uint8_t *pColor,
                            const char *pText ) {
    // FUN_00457160
    gTextRenderer.setColor(
        ColorRGBA( pColor[0], pColor[1], pColor[2], pColor[3] ) );
    DrawDebugText( x, y, pText );
}

void DebugGraph::draw( float time ) {
    // FUN_006dd8a0
    if ( pDrawList == nullptr ) {
        return;
    }
    if ( bAutoScale ) {
        autoScale();
    }

    const float width = static_cast<float>( gWindowWidth );
    const float height = static_cast<float>( gWindowHeight );

    float graphLeft = left;
    float graphBottom = bottom;
    if ( bShowRange ) {
        const float rangeLeft =
            ( kLineHeight / width ) * kRangeTextWidth + left;
        if ( rangeLeft < right ) {
            graphLeft = rangeLeft;
        }
    }

    if ( bShowNames ) {
        const float namesBottom = bottom - kLineHeight / height;
        if ( top < namesBottom ) {
            graphBottom = namesBottom;
        }

        int32_t textX = static_cast<int32_t>( width * graphLeft );
        const int32_t textY = static_cast<int32_t>( height * graphBottom );
        for ( const DebugGraphCurve &curve : curves ) {
            const int32_t nameLength =
                static_cast<int32_t>( strlen( curve.Name ) );
            if ( !curve.bVisible || nameLength <= 0 ) {
                continue;
            }
            PrintGraphText( textX, textY, curve.Color, curve.Name );
            textX += ( nameLength + 1 ) * kLineHeight;
        }
    }

    if ( bShowRange ) {
        const int32_t textX = static_cast<int32_t>( width * left );
        int32_t minTextY =
            static_cast<int32_t>( height * graphBottom ) - kLineHeight;
        int32_t maxTextY = static_cast<int32_t>( height * top );
        for ( const DebugGraphCurve &curve : curves ) {
            if ( !curve.bVisible ) {
                continue;
            }
            char minText[16];
            char maxText[16];
            snprintf( minText, sizeof( minText ), "%.2f", curve.MinY );
            snprintf( maxText, sizeof( maxText ), "%.2f", curve.MaxY );
            PrintGraphText( textX, minTextY, curve.Color, minText );
            PrintGraphText( textX, maxTextY, curve.Color, maxText );
            minTextY -= kLineHeight;
            maxTextY += kLineHeight;
        }
    }

    pDrawList->setIdentityTransform();
    pDrawList->setActiveMaterial( nullptr );
    pDrawList->setDiffuse( 0, ColorRGBA( kBorderColor ) );
    if ( pDrawList->beginPrimitive( ePrimitiveType::PT_LineStrip, 4 ) ) {
        pDrawList->pushVertex( graphLeft, top, 0.0f );
        pDrawList->pushVertex( right, top, 0.0f );
        pDrawList->pushVertex( right, graphBottom, 0.0f );
        pDrawList->pushVertex( graphLeft, graphBottom, 0.0f );
        pDrawList->commitPrimitive();
    }

    for ( DebugGraphCurve &curve : curves ) {
        if ( !curve.bVisible ) {
            continue;
        }
        if ( !bScrolling ) {
            time = curve.TimeWindow;
        }
        curve.draw( pDrawList, graphLeft, right, graphBottom, top, bScrolling,
                    time );
    }
}
