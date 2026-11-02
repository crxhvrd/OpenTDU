#pragma once

#include "ai/ai_road_iterator.h"
#include "core/random/bcpl.h"
#include "render/camera/camera_base.h"

struct AIZone;
struct PhysicsRoadCurve;
class AiVehicle;
class CameraGame;

// Traffic simulation bubble
class AiSimBubble {
public:
    static constexpr int32_t kNumZoneWeights = 6;
    static constexpr int32_t kMaxNumCandidates = 0x80;

    struct Candidate {
        AiRoadIt It;
        float Weight;
        float AltWeight; // Weight when in view, 0 otherwise
        bool bNegT;
        Eigen::Vector4f WorldPos;
        Eigen::Vector4f Direction;
        int32_t ZoneIndex;
    };

    // One sample per second; averages over the first 60 / 300 samples
    struct StatsHistory {
        static constexpr int32_t kCapacity = 300;
        static constexpr int32_t kShortWindow = 60;

        float LastSampleTime = -1.0f;
        float Instant = 0.0f;
        float ShortAverage = 0.0f;
        float LongAverage = 0.0f;
        float Samples[kCapacity] = {};
        int32_t NumSamples = 0;

        void push( const float value, const float time );
    };

public:
    inline const AIZone *getCurrentZone() const { return pCurrentZone; }
    inline int32_t getCurrentZoneIndex() const { return currentZoneIndex; }
    inline const Eigen::Vector4f &getPosition() const { return position; }
    inline float getRadius() const { return radius; }
    inline int32_t getNumCandidates() const {
        return static_cast<int32_t>( candidates.size() );
    }
    inline const std::vector<Candidate> &getCandidates() const {
        return candidates;
    }
    inline float getMaxWeight() const { return maxWeight; }
    inline float getMinWeight() const { return minWeight; }
    inline float getAltWeightSum() const { return altWeightSum; }
    inline float getRoadLengthInside() const { return roadLengthInside; }
    inline float getZoneWeight( const int32_t index ) const {
        return zoneWeights[index];
    }
    inline int32_t getNumTraffic() const { return numTraffic; }
    inline int32_t getNumVisibleHits() const { return numVisibleHits; }
    inline int32_t getTargetTraffic() const { return targetTraffic; }
    inline int32_t getTargetVisible() const { return targetVisible; }
    inline float getLaneLengthInside() const { return laneLengthInside; }
    inline bool isCullingFromCamera() const { return bCullFromCamera; }
    inline bool isOcclusionRaycastEnabled() const { return bOcclusionRaycasts; }
    inline const StatsHistory &getEstimatedVisibility() const {
        return estimatedVisibility;
    }
    inline const StatsHistory &getRealVisibility() const {
        return realVisibility;
    }
    inline void setCullFromCamera( const bool bEnabled ) {
        bCullFromCamera = bEnabled;
    }
    inline void setPosition( const Eigen::Vector4f &newPosition ) {
        position = newPosition;
        velocity = Eigen::Vector4f::Zero();
    }

public:
    AiSimBubble();

    void initialize();
    void tick( const float deltaTime, const float totalTime );
    void selectZone();
    bool pickCandidate( AiRoadIt &outIt, const bool bUseAltWeight );
    bool
    pickBotCandidate( AiRoadIt &outIt, const bool bBehind, const bool bAhead );

private:
    struct CircleHit {
        float T[8];
        int32_t MaxHits;
        float Inside;
        Eigen::Vector2f Centre;
        float Radius;
        float RadiusSq;
        Eigen::Vector2f ControlPoints[4];
    };

private:
    RandomGen random;
    Eigen::Vector4f position;
    float radius;
    Eigen::Vector4f velocity;
    bool bInitialized;
    AiRoadIt bubbleIt;
    CameraGame *pCamera;
    Camera *pRearCamera;
    Camera rearCamera; // +0x60
    std::vector<Candidate> candidates;
    float altWeightSum;
    float roadLengthInside;
    float laneLengthInside;
    float zoneWeights[kNumZoneWeights];
    float maxWeight;
    float minWeight;
    int32_t numTraffic;
    int32_t targetTraffic;
    int32_t numVisibleHits;
    int32_t targetVisible;
    int32_t onlineSpawnCredits;
    float spawnConfig[3];
    float spawnInterval;
    float lastSpawnTick;
    float frontSeenMemory;
    float rearSeenMemory;
    float lastOnlineUpdateTime;
    float lastVisibleCullTime;
    bool bCullFromCamera;
    bool bOcclusionRaycasts;
    StatsHistory estimatedVisibility;
    StatsHistory realVisibility;
    int32_t currentZoneIndex;
    const AIZone *pCurrentZone;

private:
    void updateCameras();
    float computeVisibility( AiVehicle *pVehicle, const float totalTime );
    bool isVisibleFromCamera( AiVehicle *pVehicle ) const;
    void updateTrafficVisibility( const float deltaTime,
                                  const float totalTime );
    void updateOtherVisibility( const float totalTime );
    void buildCandidates();
    float addCandidate( const AiRoadIt &source,
                        const PhysicsRoadCurve &curve,
                        const float t,
                        const bool bWay );
    void spawnCops( const float totalTime );
    void spawnTraffic( const float totalTime );
    void spawnBots( const float deltaTime );
    AiVehicle *pickLeastVisible() const;
    void cullLeastVisible( const float totalTime, const char *pReason );
    bool computeEnabledZones( bool *pOutMask ) const;
};

extern AiSimBubble gAiSimBubble;
