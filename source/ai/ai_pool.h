#pragma once

#include "ai/ai_config.h"
#include "ai/ai_pool_traffic.h"
#include "ai/ai_road_shape.h"
#include "ai/ai_spawn_request.h"
#include "ai/ai_vehicle.h"
#include "core/random/bcpl.h"

class EntityCarPlayer;

class CarState;

// Fixed size list of AI vehicles
struct AiVehicleList {
    static constexpr int32_t kCapacity = 16;

    std::array<AiVehicle *, kCapacity> Items = {};
    int32_t Count = 0;

    bool contains( const AiVehicle *pVehicle ) const;
    void add( AiVehicle *pVehicle );
    void remove( AiVehicle *pVehicle );
    void clear();

    inline AiVehicle **begin() { return Items.data(); }
    inline AiVehicle **end() { return Items.data() + Count; }
};

// Distance sorted vehicles used for the physics LOD and culling
struct AiLODSorter {
    struct Node {
        AiVehicle *pVehicle;
        float DistanceSq;
        Node *pNext;
    };

    static constexpr int32_t kCapacity = 16;

    float PhysicsRadius = 0.0f;
    float CullRadius = 0.0f;
    float FovDistance = 0.0f;
    float FovMagnitude = 0.0f; // Radians
    float PhysicsRadiusSq = 0.0f;
    float CullRadiusSq = 0.0f;
    float FovDistanceSq = 0.0f;
    Matrix4x4f_t Reference = Matrix4x4f_t::Identity();
    int32_t Count = 0;
    std::array<Node, kCapacity> Nodes = {};
    Node *pHead = nullptr;
    float LastLODTime = -1.0f;
    bool bNoOpponentPhysics = false;

    void clear();
    void insert( AiVehicle *pVehicle );
};

extern AiLODSorter gAiLODSorter;

class AiPool {
public:
    static constexpr int32_t kNumRoadShapes = 32;
    static constexpr int32_t kMaxNumVehicles = 16;
    static constexpr int32_t kNumVehicleLists = 35;
    static constexpr int32_t kMaxNumRemovals = 16;
    static constexpr uint32_t kFirstCarStateSlot = 0x10;

    enum eVehicleList : int32_t {
        AVL_All = 0,
        AVL_FirstCategory = 1,
        AVL_FirstBehaviour = 10,
        AVL_FirstController = 15,
        AVL_FirstNavigation = 24,
        AVL_FirstLocomotion = 28,
        AVL_WithTemplate = 31,
        AVL_WithoutTemplate = 32,
        AVL_LocalTraffic = 33,
        AVL_DistantTraffic = 34,
    };

public:
    inline AiPoolTraffic &getTraffic() { return traffic; }
    inline RandomGen &getRandom() { return random; }
    inline AiVehicleList &getVehicles() { return lists[AVL_All]; }
    inline AiVehicleList &getList( const int32_t index ) {
        return lists[index];
    }
    inline AiSpawnRequest &getSpawnRequest() { return spawnRequest; }
    inline int32_t getNumBots() const {
        return lists[AVL_FirstCategory +
                     static_cast<int32_t>( eAiVehicleCategory::AVC_Bot )]
            .Count;
    }

    inline void setField0x7C( const bool bValue ) { field_0x7c = bValue; }
    inline void setField0x85( const bool bValue ) { field_0x85 = bValue; }
    inline void setPoliceScale( const float value ) { policeScale = value; }
    inline void setTrafficScale( const float value ) { trafficScale = value; }
    inline void setIgnoreZoneDensity( const bool bValue ) {
        bIgnoreZoneDensity = bValue;
    }
    inline void setForceDensity( const bool bValue ) { bForceDensity = bValue; }
    inline bool isZoneDensityIgnored() const { return bIgnoreZoneDensity; }
    inline bool isForceDensity() const;
    inline int32_t getNextOpponentIndex() const {
        // FUN_00409440
        return ( opponentPickFrame != 0 ) ? nextOpponentIndex : -1;
    }
    inline int32_t getOpponentPickFrame() const { return opponentPickFrame; }
    inline int32_t getNextController1Index() const {
        return nextController1Index;
    }
    inline void setTrafficPaused( const bool bValue ) {
        traffic.setPaused( bValue );
    }

public:
    AiPool();

    void initialize();
    void terminate();
    void tick( const float deltaTime, const float totalTime );

    inline AiRoadShape &getShape( const int32_t index ) {
        return roadShapes[index];
    }
    AiRoadShape *getUnusedShape();

    int32_t getMaxTraffic() const;
    int32_t getMaxPolice() const;
    bool spawnTrafficOnRoad( const AiRoadIt &candidate, const bool bPolice );
    AiVehicle *addOpponent( CarState *pCar,
                            const Matrix4x4f_t &startFrame,
                            const AiRoadIt &startHint,
                            const int32_t missionType );
    bool requestBot( const AiRoadIt &candidate );
    bool spawnBot( const int32_t botIndex,
                   const Matrix4x4f_t &frame,
                   const AiRoadIt &roadIt,
                   const int32_t lane );
    AiVehicle *requestParked( const Matrix4x4f_t &frame,
                              const int32_t colorIndex,
                              EntityCarPlayer *pOwner );
    bool isVehicleSlotUsed( const AiVehicle *pVehicle ) const;
    bool requestTraffic( const Matrix4x4f_t &frame,
                         const AiRoadIt &roadIt,
                         const int32_t lane,
                         const bool bPolice );
    CarState *installVehicle( AiPoolTraffic::Template *pTemplate,
                              AiVehicle *pVehicle,
                              const bool bLinkAiToCarState,
                              const Matrix4x4f_t &physicsSpawnMatrix );
    void releaseTemplateCar( AiVehicle *pVehicle );

    void addVehicle( AiVehicle *pVehicle );
    void releaseVehicleSlot( AiVehicle *pVehicle );
    void queueRemoval( AiVehicle *pVehicle, const char *pReason );
    bool togglePlayerAutopilot( const bool bOpponent );
    void removeVehicle( AiVehicle *pVehicle,
                        const char *pReason,
                        const bool bReleaseCar );
    void removeVehiclesWithCar( const int32_t carID );

    void removeAllVehicles( const bool bResetPool );
    void removeBots();
    void removeCops();
    void removeParkedVehicles();
    void removeOpponents();
    void removeTraffic();

private:
    AiPoolTraffic traffic;
    float trafficScale;
    bool field_0x7c;
    bool bIgnoreZoneDensity;
    float policeScale;
    bool bForceDensity;
    bool field_0x85;
    RandomGen random;

    std::array<AiVehicle *, kMaxNumRemovals> removalQueue;
    int32_t numRemovals;
    char removalReasons[kMaxNumRemovals][AiVehicle::kRemovalReasonLength];

    std::array<AiVehicle *, kMaxNumVehicles> pUsedVehicles;
    std::array<AiVehicle *, kMaxNumVehicles> pFreeVehicles;
    int32_t numUsedVehicles;
    std::array<bool, kMaxNumVehicles> bVehicleUsed;
    std::array<AiVehicle, kMaxNumVehicles> vehicles;

    std::array<AiVehicleList, kNumVehicleLists> lists;
    AiRoadShape roadShapes[kNumRoadShapes];
    AiSpawnRequest spawnRequest;

    int32_t opponentPickFrame;
    int32_t nextOpponentIndex;
    int32_t nextController1Index;

private:
    AiVehicle *allocVehicleSlot();
    void addToSubLists( AiVehicle *pVehicle );
    void removeFromLists( AiVehicle *pVehicle );
    void removeCategory( const eAiVehicleCategory category,
                         const char *pReason );
    void updateLOD( AiLODSorter &sorter );
    void updateOpponentRoundRobin();
    void updateController1RoundRobin();
    bool wantsPhysics( AiVehicle *pVehicle,
                       const AiLODSorter &sorter,
                       const int32_t maxPhysics,
                       const float distanceSq,
                       int32_t &numPhysics ) const;
};

inline bool AiPool::isForceDensity() const {
    // FUN_0091dab0
    return gAIConfig.bPoliceForceDensity || bForceDensity;
}
