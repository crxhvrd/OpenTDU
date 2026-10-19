#pragma once

class AiVehicle;
class AiVehNavOpponent;
class GameObject;
class NavPathOpponent;
class OptPath;
struct AiRoadIt;
struct ObjectWorldLocation;
struct OptPathElem;
struct OptPathSituation;

// Lateral occupancy of the racing line span starting at the element of the
// same index (0x54)
struct AiObstacleSlot {
    // Sweep marker of an occupied lateral interval (0x14)
    struct Marker {
        enum eType : int32_t {
            MT_None = 0,
            MT_Start = 1,
            MT_End = 2,
            MT_Invalid = 4,
        };

        float WidthRatio = 0.0f;
        float FractionMin = 0.0f;
        float FractionMax = 0.0f;
        float Time = 0.0f;
        eType Type = MT_Invalid;
    };

    static constexpr int32_t kMaxObjects = 0x18;
    static constexpr float kUnsetWidthRatio = -1024.0f; // DAT_00f417f8

    int32_t RoadID = -1;
    bool bWay = true;
    float FractionStart = -1.0f;
    float FreeEnd = -1.0f;
    float FreeStart = -1.0f;
    float FractionEnd = -1.0f;
    float TimeStart = 0.0f;
    float TimeEnd = 0.0f;
    float TargetWidthRatio = kUnsetWidthRatio;
    std::vector<Marker> Markers; // Sorted by width ratio
    std::vector<GameObject *> Objects;

    void reset();
    bool hasSoftCollisionCar() const;
    void addObject( GameObject *pObject );
    void insertInterval( const Marker &start, Marker end );
};

// Race opponent overtaking: occupancy of the racing line ahead and lateral
// gap search (AiVehNavOpponent+0x7bc)
class AiVehObstacles {
public:
    static constexpr int32_t kNumSlots = 0x20;

public:
    AiVehObstacles();

    void attach( AiVehNavOpponent *pOwnerNav,
                 AiVehicle *pOwnerVehicle,
                 OptPath *pOwnerLine,
                 NavPathOpponent *pOwnerPath,
                 OptPathSituation *pOwnerSituation );
    void detach();
    void resetSlots();

    bool collect();
    bool solve( bool &bOutStop );

private:
    AiVehNavOpponent *pNav;
    AiVehicle *pVehicle;
    OptPathSituation *pSituation;
    OptPath *pLine;
    NavPathOpponent *pPath;
    AiObstacleSlot slots[kNumSlots];

private:
    void collectRoad( const int32_t index );
    void collectCurrentSpan();
    void collectSpan( const int32_t indexA,
                      const int32_t indexB,
                      const int32_t slotIndex );
    bool initSpanSlot( const int32_t slotIndex,
                       const int32_t indexA,
                       const int32_t indexB );
    void collectJunction( const uint32_t junctionID, const int32_t index );
    void collectJunctionObject( GameObject *pObject,
                                const Eigen::Vector2f ( &corners )[4],
                                const AiRoadIt &it,
                                AiObstacleSlot &slot );
    bool isTargetObjectIgnored( GameObject *pObject,
                                AiObstacleSlot *pSlot ) const;
    bool isBlockingPlayer( GameObject *pObject ) const;
    bool updateSlotRange( const int32_t slotIndex,
                          const float fractionA,
                          const float fractionB,
                          const float velocity,
                          const float length );
    void addInterval( const int32_t slotIndex,
                      const AiObstacleSlot::Marker &start,
                      const AiObstacleSlot::Marker &end );
    float computeCatchUpTime( const ObjectWorldLocation &location ) const;
    float computeTimeToElem( const int32_t index ) const;

    bool solveAhead( bool &bOutStop );
    bool solveCurrent( const float lookAhead,
                       float &outDistance,
                       int32_t &outLast,
                       bool &bOutStop );
    bool solveNext( const int32_t first,
                    const float remaining,
                    float &outDistance,
                    int32_t &outLast,
                    bool &bOutStop );
    bool solveSpan( const int32_t first,
                    const float lookAhead,
                    const OptPathElem &start,
                    const bool bSmooth,
                    float &outDistance,
                    int32_t &outLast,
                    bool &bOutStop );
    bool isGapFree( const int32_t slotIndex,
                    const OptPathElem &elemA,
                    const OptPathElem &elemB,
                    bool &bOutFitA,
                    bool &bOutFitB ) const;
    bool findGapTarget( const AiObstacleSlot &slot,
                        const int32_t index,
                        const float desired,
                        float &outTarget ) const;
    bool mergeSlots( const AiObstacleSlot &slotA,
                     const AiObstacleSlot &slotB,
                     AiObstacleSlot *pOut ) const;
};
