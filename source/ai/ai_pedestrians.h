#pragma once

class Entity;
class MngActor;
class Sector;
class CameraGame;

// Walkable area of a sector, as read by the pedestrian simulation
struct AiPedestrianNavQuad {
    float Points[4][4];
};
OTDU_SIZE_MUST_MATCH( AiPedestrianNavQuad, 0x40 );

struct AiPedestrianNavCircle {
    float Center[4];
    float Radius;
    uint8_t __PADDING__[0xc];
};
OTDU_SIZE_MUST_MATCH( AiPedestrianNavCircle, 0x20 );

struct AiPedestrianNavCell {
    uint32_t NumQuads;
    uint32_t NumCircles;
    uint32_t QuadIndexesOffset;
    uint32_t CircleIndexesOffset;
};
OTDU_SIZE_MUST_MATCH( AiPedestrianNavCell, 0x10 );

struct AiPedestrianNavSlot {
    static constexpr float kMaxLocalCoordinate = 767.5f; // DAT_00f41ab0

    Sector *pOwner;
    uint32_t NumQuads;
    const AiPedestrianNavQuad *pQuads;
    uint32_t NumCircles;
    const AiPedestrianNavCircle *pCircles;
    int32_t GridSize;
    int32_t CellSize;
    const AiPedestrianNavCell *pCells;
    AiPedestrianNavCell EmptyCell;
    const uint8_t *pBlob;

    AiPedestrianNavSlot();

    void reset();
    void bind( const uint8_t *pNavBlob, Sector *pSector );

    const AiPedestrianNavCell &getCell( const Eigen::Vector4f &position ) const;
    const AiPedestrianNavQuad *
    findQuad( const AiPedestrianNavCell &cell,
              const Eigen::Vector4f &position ) const;
    const AiPedestrianNavQuad *
    findQuad( const Eigen::Vector4f &position ) const;
};

struct AiPedestrian {
    enum eFlags : uint32_t {
        PF_InView = 0x1,
        PF_Displayed = 0x10,
        PF_OverBudget = 0x20,
        PF_Stopped = 0x1000,
    };

    AiPedestrianNavSlot *pSlot;
    Matrix4x4f_t Transform;
    Eigen::Vector4f SectorOrigin;
    AiPedestrianNavSlot *pNextSlot;
    Matrix4x4f_t NextTransform;
    Eigen::Vector4f NextSectorOrigin;
    Eigen::Vector4f Direction;
    float Heading;
    float Speed;
    Eigen::Vector4f Avoidance;
    float DeltaTime;
    uint32_t Flags;
    float CameraDistance;
    int32_t bWasMoving;
    bool bDirty;
    int32_t DisplaySlot;

    inline Eigen::Vector4f getPosition() const {
        return Transform.row( 3 ).transpose();
    }
    inline Eigen::Vector4f getNextPosition() const {
        return NextTransform.row( 3 ).transpose();
    }
};

struct AiPedestrianPool {
    static constexpr int32_t kCapacity = 600;

    AiPedestrian *pActive[kCapacity];
    AiPedestrian *pFree[kCapacity];
    int32_t NumActive;
    bool bUsed[kCapacity];
    AiPedestrian Pedestrians[kCapacity];

    void initialize();
    AiPedestrian *allocate();
    void release( AiPedestrian *pPedestrian );
};

struct AiPedestrianListNode {
    AiPedestrian *pData;
    AiPedestrianListNode *pPrevious;
    AiPedestrianListNode *pNext;
};

struct AiPedestrianList {
    AiPedestrianListNode *pHead;
    AiPedestrianListNode *pTail;
    AiPedestrianListNode *pCursor;
    int32_t Count;

    AiPedestrianListNode *pNodes;
    AiPedestrianListNode *pFreeNodes;
    int32_t Capacity;
    int32_t NumUsedNodes;

    void reset();
    void allocate( const int32_t capacity );
    void release();
    AiPedestrianListNode *pushBack();
    void removeCursor();
    void clear();
};

class AiPedestrianManager {
public:
    static constexpr int32_t kNumNavSlots = 7;
    static constexpr int32_t kMaxNumDisplayed = 40;
    static constexpr uint32_t kMaxNumSpawnPerArea = 0x4b;

public:
    inline bool isEnabled() const { return bEnabled; }
    inline AiPedestrianNavSlot &getNavSlot( const int32_t index ) {
        return navSlots[index];
    }
    inline bool isAreaRemovalPending() const { return bAreaRemovalPending; }
    inline void clearAreaRemovalPending() { bAreaRemovalPending = false; }
    inline int32_t getNumActive() const { return pool.NumActive; }
    inline int32_t getNumVisible() const { return numVisible; }
    inline int32_t getNumDisplayed() const { return numDisplayed; }
    inline Entity *getDisplayEntity( const int32_t index ) const {
        return pDisplayEntities[index];
    }
    inline const AiPedestrian *getSlotPedestrian( const int32_t index ) const {
        return pSlotPedestrians[index];
    }

public:
    AiPedestrianManager();

    void initialize();
    void terminate();
    void registerArea( const uint8_t *pNavBlob, Sector *pSector );
    void removeArea( const AiPedestrianNavSlot *pSlot );
    void removePendingAreas();
    void update( const float deltaTime );
    void updateDisplay();
    void instanciateEntities( MngActor *pMngActor );
    void uninstanciateEntities();
    void enable();
    void disable();

private:
    AiPedestrianNavSlot *findNavSlot( const Sector *pSector );
    uint32_t nextRandom();
    void spawnArea( AiPedestrianNavSlot &slot );
    void updateSpeed( AiPedestrian &pedestrian, const float deltaTime );
    void updateAvoidance( AiPedestrian &pedestrian, const float deltaTime );
    void updateNavigation( AiPedestrian &pedestrian );
    void integrate( AiPedestrian &pedestrian, const bool bLookAhead );
    void refresh( AiPedestrian &pedestrian );
    void commit( AiPedestrian &pedestrian );
    bool snapToQuad( AiPedestrian &pedestrian,
                     const AiPedestrianNavQuad &quad );

    static void InitializePedestrian( AiPedestrian &pedestrian,
                                      AiPedestrianNavSlot *pSlot,
                                      const Matrix4x4f_t &transform );
    static void SetDirection( AiPedestrian &pedestrian,
                              const Eigen::Vector4f &direction,
                              const float heading );
    static bool TestVisibility( AiPedestrian &pedestrian, CameraGame *pCamera );

    void assignDisplaySlots( AiPedestrian **ppDisplayed,
                             const uint32_t numToDisplay,
                             const uint32_t maxDisplayed );

private:
    MngActor *pActorManager;
    bool bEnabled;
    AiPedestrianNavSlot navSlots[kNumNavSlots];
    Entity *pDisplayEntities[kMaxNumDisplayed];
    AiPedestrian *pSlotPedestrians[kMaxNumDisplayed];
    AiPedestrianPool pool;
    AiPedestrianList list;
    uint32_t randomSeed;
    bool bAreaRemovalPending;
    AiPedestrianNavSlot *pPendingRemovals[kNumNavSlots];
    int32_t numVisible;
    int32_t numDisplayed;
};
