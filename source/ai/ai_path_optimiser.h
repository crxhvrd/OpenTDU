#pragma once

#include <mutex>

// One racing line handed to the optimiser. Everything is 2D (X, Z),
// relative to the world shift origin at build time
struct AiPathOptimiserSlot {
    static constexpr int32_t kMaxPoints = 32;
    static constexpr int32_t kMaxSegments = kMaxPoints - 1;

    // Road cross-section the point is constrained to
    struct CrossSection {
        int32_t ID = 0; // Racing line element ID
        float BaseX = 0.0f;
        float BaseZ = 0.0f;
        float LeftX = 0.0f;
        float LeftZ = 0.0f;
        float RightX = 0.0f;
        float RightZ = 0.0f;
    };

    struct Point {
        float X = 0.0f;
        float Z = 0.0f;
        float ForceX = 0.0f;
        float ForceZ = 0.0f;
        float Angle = 0.0f; // Signed interior angle (+-PI = straight)
    };

    // Segment i always links Points[i] to Points[i + 1]
    struct Segment {
        float ForceMagnitude = 0.0f;
        float AmpFactor = 0.0f;
        float DeltaX = 0.0f;
        float DeltaZ = 0.0f;
        float Length = 0.0f;
        bool bDegenerate = false;
    };

    CrossSection CrossSections[kMaxPoints];
    Point Points[kMaxPoints];
    Segment Segments[kMaxSegments];
    int32_t NumSegments;
    int32_t FirstMovable; // Points before this index are pinned
    int32_t Iteration;
    Eigen::Vector4f Origin;
    float Residual; // Never updated (the convergence test is dead)
    float MeanWidth;

    AiPathOptimiserSlot();

    void clear();
    void copyTo( AiPathOptimiserSlot &destination ) const;
    void updateSegments();
    float computeMeanWidth();
    bool isConverged() const;
    int32_t optimise( int32_t numPasses );

private:
    float computeCurviness( int32_t first, int32_t last ) const;
    float computeAmpFactor( const int32_t index ) const;
    float computeForceMagnitude( const int32_t index ) const;
    void computeForces();
    void integrate();
};

// Racing line relaxation: moves the racing line
// points inside the road cross-sections to straighten the line
class AiPathOptimiser {
public:
    static constexpr int32_t kNumSlots = 12;
    static constexpr int32_t kNumStats = 30;

public:
    inline bool isEnabled() const { return bEnabled; }
    inline void setEnabled( const bool bValue ) { bEnabled = bValue; }
    inline int32_t getNumActiveSlots() const { return numActiveSlots; }

    inline void lock() { mutex.lock(); }
    inline void unlock() { mutex.unlock(); }

public:
    AiPathOptimiser();

    void initialize();
    void release();
    void tick();
    void drawStats();

    int32_t allocSlot();
    void freeSlot( const int32_t slot );
    void resetSlot( const int32_t slot );
    AiPathOptimiserSlot &lockSlotForWrite( const int32_t slot );
    AiPathOptimiserSlot &lockSlotForRead( const int32_t slot );

private:
    std::vector<AiPathOptimiserSlot> buffers[2];
    int32_t currentBuffer;
    bool bSlotUsed[kNumSlots];
    bool bSlotDirty[kNumSlots];
    int32_t numActiveSlots;
    std::mutex mutex;
    bool bEnabled;
    int32_t statsBudget[kNumStats];
    int32_t statsUsed[kNumStats];
    float statsCompletion[kNumStats];
    int32_t numStats;
    int32_t statIndex;
    float occupation;

private:
    inline AiPathOptimiserSlot &getBackSlot( const int32_t slot ) {
        return buffers[( currentBuffer + 1 ) & 1][slot];
    }

    void swapBuffers();
};

// TODO: GSAI+0x2a980 (move into GSAI: initialize from GSAI::initialize,
// tick at the end of GSAI::tick)
extern AiPathOptimiser gAiPathOptimiser;
extern AiPathOptimiser *gpAiPathOptimiser;

extern bool gbPathOptFrameByFrame;      // DAT_010e73f4
extern bool gbPathOptStepOnce;          // DAT_010e73f5
extern bool gbPathOptIgnoreConstraints; // DAT_010e73f6
extern bool gbPathOptFreeNearCar;       // DAT_010e73d1
