#include "shared.h"
#include "ai_path_optimiser.h"

#include "ai/ai_config.h"
#include "debug/ed_debug_text.h"

static constexpr float kStraightAngle = 3.14159274f;    // DAT_00f41940
static constexpr float kInvPi = 0.318309873f;           // DAT_00f41738
static constexpr float kMinCurviness = 1.0e-4f;         // DAT_00f41760
static constexpr float kMinSegmentLength = 1.0e-4f;     // DAT_00f41760
static constexpr float kConvergenceThreshold = 2.0e-4f; // DAT_00f41810
static constexpr float kInitialResidual = 1000.0f;      // DAT_00f41718
static constexpr int32_t kAmpFactorHalfWindow = 3;

AiPathOptimiser gAiPathOptimiser;
AiPathOptimiser *gpAiPathOptimiser = nullptr;

bool gbPathOptFrameByFrame = false;      // DAT_010e73f4
bool gbPathOptStepOnce = false;          // DAT_010e73f5
bool gbPathOptIgnoreConstraints = false; // DAT_010e73f6
bool gbPathOptFreeNearCar = false;       // DAT_010e73d1

AiPathOptimiserSlot::AiPathOptimiserSlot()
    : NumSegments( 0 ),
      FirstMovable( 0 ),
      Iteration( 0 ),
      Origin( Eigen::Vector4f::Zero() ),
      Residual( kInitialResidual ),
      MeanWidth( 0.0f ) {
    // FUN_00942720
    clear();
}

void AiPathOptimiserSlot::clear() {
    // FUN_00942660
    for ( Point &point : Points ) {
        point.ForceZ = 0.0f;
        point.ForceX = 0.0f;
        point.Z = 0.0f;
        point.X = 0.0f;
        point.Angle = kStraightAngle;
    }
    for ( CrossSection &crossSection : CrossSections ) {
        crossSection.BaseZ = 0.0f;
        crossSection.BaseX = 0.0f;
    }
    for ( Segment &segment : Segments ) {
        segment.DeltaZ = 0.0f;
        segment.DeltaX = 0.0f;
        segment.Length = 0.0f;
        segment.bDegenerate = false;
    }
    NumSegments = 0;
    Iteration = 0;
    FirstMovable = 0;
    Residual = kInitialResidual;
    MeanWidth = 0.0f;
}

void AiPathOptimiserSlot::copyTo( AiPathOptimiserSlot &destination ) const {
    // FUN_00941de0
    destination = *this;
}

static void UpdateSegment( AiPathOptimiserSlot::Segment &segment,
                           const AiPathOptimiserSlot::Point &start,
                           const AiPathOptimiserSlot::Point &end ) {
    // FUN_00942750
    segment.DeltaX = end.X - start.X;
    segment.DeltaZ = end.Z - start.Z;
    segment.Length = sqrtf( segment.DeltaZ * segment.DeltaZ +
                            segment.DeltaX * segment.DeltaX );
    segment.bDegenerate = kMinSegmentLength > segment.Length;
}

void AiPathOptimiserSlot::updateSegments() {
    for ( int32_t i = 0; i < NumSegments; i++ ) {
        UpdateSegment( Segments[i], Points[i], Points[i + 1] );
    }
}

float AiPathOptimiserSlot::computeMeanWidth() {
    // FUN_00942090
    float sum = 0.0f;
    for ( int32_t i = 0; i <= NumSegments; i++ ) {
        const CrossSection &crossSection = CrossSections[i];
        const float deltaX = crossSection.RightX - crossSection.LeftX;
        const float deltaZ = crossSection.RightZ - crossSection.LeftZ;
        sum += sqrtf( deltaX * deltaX + deltaZ * deltaZ );
    }
    MeanWidth = sum / static_cast<float>( NumSegments );
    return MeanWidth;
}

bool AiPathOptimiserSlot::isConverged() const {
    // FUN_00941bf0
    return Iteration > 0 && kConvergenceThreshold >= Residual;
}

float AiPathOptimiserSlot::computeCurviness( int32_t first,
                                             int32_t last ) const {
    // FUN_00941c50
    const int32_t maxIndex = NumSegments - 1;
    if ( first < 0 ) {
        first = 0;
    } else if ( first > maxIndex ) {
        first = maxIndex;
    }
    if ( last < 0 ) {
        last = 0;
    } else if ( last > maxIndex ) {
        last = maxIndex;
    }

    const int32_t count = last - first + 1;
    float sum = 0.0f;
    for ( int32_t i = first; i <= last; i++ ) {
        sum += fabsf( Points[i + 1].Angle );
    }
    if ( count <= 0 ) {
        return 0.0f;
    }
    return kStraightAngle - sum / static_cast<float>( count );
}

float AiPathOptimiserSlot::computeAmpFactor( const int32_t index ) const {
    // FUN_00941d50
    float curviness = computeCurviness( index - kAmpFactorHalfWindow,
                                        index + kAmpFactorHalfWindow );
    if ( kMinCurviness > curviness ) {
        curviness = 0.0f;
    }
    return 1.0f / ( 1.0f / gAIConfig.PathOptMaxAmpFactor + curviness ) +
           gAIConfig.PathOptMinAmpFactor;
}

float AiPathOptimiserSlot::computeForceMagnitude( const int32_t index ) const {
    // FUN_00941da0
    const Segment &segment = Segments[index];
    const Segment &next = Segments[index + 1];
    const float bend =
        ( kStraightAngle - fabsf( Points[index + 1].Angle ) ) * kInvPi;
    return bend * bend / ( next.Length + segment.Length ) *
           ( 1.0f / segment.Length ) *
           ( gAIConfig.PathOptTrackDistance + gAIConfig.PathOptTrackDistance ) *
           segment.AmpFactor;
}

static Eigen::Vector2f ComputeSegmentNormal( const float deltaX,
                                             const float deltaZ ) {
    Eigen::Vector2f normal( deltaZ, -deltaX );
    const float length =
        sqrtf( normal.y() * normal.y() + normal.x() * normal.x() );
    if ( length > 0.0f ) {
        normal *= 1.0f / length;
    }
    return normal;
}

static inline float GetAngleSign( const float angle ) {
    return ( 0.0f > angle ) ? -1.0f : 1.0f;
}

void AiPathOptimiserSlot::computeForces() {
    // FUN_00942100
    Points[0].ForceZ = 0.0f;
    Points[0].ForceX = 0.0f;
    Points[0].Angle = kStraightAngle;
    Points[NumSegments].Angle = kStraightAngle;

    for ( int32_t i = 1; i < NumSegments; i++ ) {
        const Segment &segmentA = Segments[i - 1];
        const Segment &segmentB = Segments[i];
        Points[i].Angle = -atan2f( segmentA.DeltaZ * segmentB.DeltaX -
                                       segmentA.DeltaX * segmentB.DeltaZ,
                                   -segmentA.DeltaZ * segmentB.DeltaZ -
                                       segmentA.DeltaX * segmentB.DeltaX );
    }

    for ( int32_t i = 0; i < NumSegments - 1; i++ ) {
        Segment &segment = Segments[i];
        if ( segment.bDegenerate || Segments[i + 1].bDegenerate ) {
            segment.AmpFactor = 0.0f;
            segment.ForceMagnitude = 0.0f;
            continue;
        }
        segment.AmpFactor = computeAmpFactor( i );
        segment.ForceMagnitude = computeForceMagnitude( i );
    }
    Segments[NumSegments - 1].AmpFactor = 1.0f;
    Segments[NumSegments - 1].ForceMagnitude = 0.0f;

    for ( int32_t i = 0; i < NumSegments; i++ ) {
        const Segment &segment = Segments[i];
        const Segment &previous = ( i > 0 ) ? Segments[i - 1] : segment;
        const bool bHasNext = i + 1 < NumSegments;
        const Segment &next = bHasNext ? Segments[i + 1] : segment;
        if ( segment.bDegenerate || previous.bDegenerate ) {
            continue;
        }

        const Eigen::Vector2f normal =
            ComputeSegmentNormal( segment.DeltaX, segment.DeltaZ );
        const Eigen::Vector2f nextNormal =
            ComputeSegmentNormal( next.DeltaX, next.DeltaZ );
        const float signStart = GetAngleSign( Points[i].Angle );
        const float signEnd = GetAngleSign( Points[i + 1].Angle );
        const float signNextEnd =
            GetAngleSign( Points[bHasNext ? i + 2 : i + 1].Angle );

        const float previousForce = ( i > 0 ) ? previous.ForceMagnitude : 0.0f;
        const float nextForce = bHasNext ? next.ForceMagnitude : 0.0f;
        const float selfForce = 0.0f - segment.ForceMagnitude;
        const float selfForceNext = bHasNext ? selfForce : 0.0f;

        const Eigen::Vector2f forceStart = normal * signStart * previousForce;
        const Eigen::Vector2f forceEnd = normal * signEnd * selfForce;
        const Eigen::Vector2f forceEndNext =
            nextNormal * signEnd * selfForceNext;
        const Eigen::Vector2f forceNext = nextNormal * signNextEnd * nextForce;

        Point &point = Points[i + 1];
        point.ForceX = ( forceNext.x() + forceStart.x() + forceEnd.x() +
                         forceEndNext.x() ) *
                       segment.AmpFactor;
        point.ForceZ = ( forceNext.y() + forceStart.y() + forceEnd.y() +
                         forceEndNext.y() ) *
                       segment.AmpFactor;
    }
}

void AiPathOptimiserSlot::integrate() {
    // FUN_00942880
    if ( gbPathOptIgnoreConstraints ) {
        for ( int32_t i = 0; i <= NumSegments; i++ ) {
            Points[i].X += Points[i].ForceX;
            Points[i].Z += Points[i].ForceZ;
        }
    } else {
        for ( int32_t i = FirstMovable; i <= NumSegments; i++ ) {
            Point &point = Points[i];
            const CrossSection &crossSection = CrossSections[i];
            const float deltaX = point.ForceX + point.X - crossSection.LeftX;
            const float deltaZ = point.ForceZ + point.Z - crossSection.LeftZ;
            const float axisX = crossSection.RightX - crossSection.LeftX;
            const float axisZ = crossSection.RightZ - crossSection.LeftZ;
            float t = ( deltaZ * axisZ + deltaX * axisX ) /
                      ( axisZ * axisZ + axisX * axisX );
            if ( 0.0f > t ) {
                t = 0.0f;
            } else if ( t > 1.0f ) {
                t = 1.0f;
            }
            point.X =
                crossSection.LeftX * ( 1.0f - t ) + crossSection.RightX * t;
            point.Z =
                crossSection.LeftZ * ( 1.0f - t ) + crossSection.RightZ * t;
        }
    }
    updateSegments();
}

int32_t AiPathOptimiserSlot::optimise( int32_t numPasses ) {
    // FUN_00942af0
    if ( NumSegments < 2 ) {
        return 0;
    }
    if ( gbPathOptFrameByFrame ) {
        numPasses = gbPathOptStepOnce ? 1 : 0;
        gbPathOptStepOnce = false;
    }

    int32_t pass = 0;
    for ( ; pass < numPasses; pass++ ) {
        if ( isConverged() ) {
            break;
        }
        computeForces();
        integrate();
    }
    Iteration++;
    return pass;
}

AiPathOptimiser::AiPathOptimiser()
    : buffers(),
      currentBuffer( 0 ),
      bSlotUsed{},
      bSlotDirty{},
      numActiveSlots( 0 ),
      mutex(),
      bEnabled( false ),
      statsBudget{},
      statsUsed{},
      statsCompletion{},
      numStats( 0 ),
      statIndex( 0 ),
      occupation( 0.0f ) {}

void AiPathOptimiser::initialize() {
    // FUN_009419f0
    buffers[0].assign( kNumSlots, AiPathOptimiserSlot() );
    buffers[1].assign( kNumSlots, AiPathOptimiserSlot() );
    currentBuffer = 0;
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        bSlotUsed[i] = false;
        bSlotDirty[i] = false;
    }
    numStats = 0;
    statIndex = 0;
    bEnabled = true;
    gpAiPathOptimiser = this;
}

void AiPathOptimiser::release() {
    bEnabled = false;
    buffers[0].clear();
    buffers[1].clear();
    numActiveSlots = 0;
    if ( gpAiPathOptimiser == this ) {
        gpAiPathOptimiser = nullptr;
    }
}

void AiPathOptimiser::swapBuffers() {
    // FUN_00941870
    std::vector<AiPathOptimiserSlot> &current = buffers[currentBuffer];
    std::vector<AiPathOptimiserSlot> &back = buffers[( currentBuffer + 1 ) & 1];
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        if ( !bSlotUsed[i] ) {
            continue;
        }
        if ( bSlotDirty[i] ) {
            bSlotDirty[i] = false;
            back[i].copyTo( current[i] );
        } else {
            current[i].copyTo( back[i] );
        }
    }
    currentBuffer = ( currentBuffer + 1 ) & 1;
}

void AiPathOptimiser::tick() {
    // FUN_00941a90
    if ( !bEnabled || numActiveSlots <= 0 ) {
        return;
    }

    lock();
    swapBuffers();
    unlock();

    const int32_t numPasses = gAIConfig.PathOptNbPasses;
    const int32_t quotient = numPasses / numActiveSlots;
    const int32_t numPassesPerSlot = ( quotient >= 1 ) ? quotient : 1;
    int32_t numSparePasses = numPasses % numActiveSlots;
    statsBudget[statIndex] = numPasses;

    std::vector<AiPathOptimiserSlot> &current = buffers[currentBuffer];
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        if ( !bSlotUsed[i] ) {
            continue;
        }
        const int32_t numUsed = current[i].optimise( numPassesPerSlot );
        if ( numUsed < numPassesPerSlot ) {
            numSparePasses += numPassesPerSlot - numUsed;
        }
    }

    statsUsed[statIndex] = numPasses - numSparePasses;
    statIndex++;
    if ( statIndex >= kNumStats ) {
        statIndex = 0;
    }
    if ( numStats < kNumStats ) {
        numStats++;
    }
}

int32_t AiPathOptimiser::allocSlot() {
    // FUN_00941980
    if ( numActiveSlots >= kNumSlots ) {
        return -1;
    }
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        if ( bSlotUsed[i] ) {
            continue;
        }
        lock();
        getBackSlot( i ).clear();
        bSlotDirty[i] = true;
        unlock();
        bSlotUsed[i] = true;
        numActiveSlots++;
        return i;
    }
    return -1;
}

void AiPathOptimiser::freeSlot( const int32_t slot ) {
    // FUN_00941930
    bSlotUsed[slot] = false;
    lock();
    getBackSlot( slot ).clear();
    bSlotDirty[slot] = true;
    unlock();
    numActiveSlots--;
}

void AiPathOptimiser::resetSlot( const int32_t slot ) {
    // FUN_009418f0
    lock();
    getBackSlot( slot ).clear();
    bSlotDirty[slot] = true;
    unlock();
}

AiPathOptimiserSlot &AiPathOptimiser::lockSlotForWrite( const int32_t slot ) {
    // FUN_009417e0
    lock();
    bSlotDirty[slot] = true;
    return getBackSlot( slot );
}

AiPathOptimiserSlot &AiPathOptimiser::lockSlotForRead( const int32_t slot ) {
    // FUN_00941810
    lock();
    if ( bSlotDirty[slot] ) {
        return getBackSlot( slot );
    }
    return buffers[currentBuffer][slot];
}

void AiPathOptimiser::drawStats() {
    // FUN_009415e0
    static constexpr float kPercent = 100.0f;     // DAT_00f41708
    static constexpr float kMetersToMm = 1000.0f; // DAT_00f41718

    if ( numStats <= 0 ) {
        return;
    }

    float maxMove = 0.0f;
    float sumCompletion = 0.0f;
    int32_t numUsedSlots = 0;
    const std::vector<AiPathOptimiserSlot> &slots = buffers[currentBuffer & 1];
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        if ( !bSlotUsed[i] ) {
            continue;
        }
        if ( maxMove <= slots[i].Residual ) {
            maxMove = slots[i].Residual;
        }
        numUsedSlots++;
    }
    float meanCompletion = 0.0f;
    if ( numUsedSlots > 0 ) {
        meanCompletion = sumCompletion / static_cast<float>( numUsedSlots );
    }
    statsCompletion[statIndex] = meanCompletion;

    int32_t totalBudget = 0;
    int32_t totalUsed = 0;
    float totalCompletion = 0.0f;
    for ( int32_t i = 0; i < numStats; i++ ) {
        totalCompletion += statsCompletion[i];
        totalBudget += statsBudget[i];
        totalUsed += statsUsed[i];
    }
    occupation =
        static_cast<float>( totalUsed ) / static_cast<float>( totalBudget );
    totalCompletion /= static_cast<float>( numStats );

    gEdDebugText.drawText( "Nb Passes Permitted: %d", totalBudget );
    gEdDebugText.drawText( "Nb Passes Done: %d", totalUsed );
    gEdDebugText.drawText( "Occupation: %d",
                           static_cast<int32_t>( occupation * kPercent ) );
    gEdDebugText.drawText( "Max Move: %.2f mm",
                           static_cast<double>( maxMove * kMetersToMm ) );
    gEdDebugText.drawText( "Mean Completion: %d",
                           static_cast<int32_t>( totalCompletion * kPercent ) );
}
