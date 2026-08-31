#pragma once

class Entity;
class GameModeWorld;
struct SpotMap;

// Player game mechanic.
class PlayerGameMech {
public:
    enum eMechEvent : int32_t {
        ME_Reset = 3,
        ME_SpotEntered = 4,
        ME_ExtraChallengeEntered = 6,
        ME_SpotLeft = 7,
        ME_SpotUnloaded = 9,
    };

    // Flash Movie /:WhatSpotType index
    enum eSpotDisplayType : int32_t {
        SDT_Solo = 0,
        SDT_Multi = 1,
        SDT_Club = 2,
        SDT_Error = 3,

        SDT_Count
    };

    // Flash Movie /:WhatSpotType index
    enum eSpotDisplayName : int32_t {
        SDN_TimeAttack = 0,
        SDN_Speed = 1,
        SDN_Driving = 2,
        SDN_Race = 3,
        SDN_BikeDealer = 4,
        SDN_CarDealer = 5,
        SDN_CarRental = 6,
        SDN_CarWash = 7,
        SDN_Estates = 8,
        SDN_Goodies = 9,
        SDN_Tuning = 10,
        SDN_Home = 11,
        SDN_ForSale = 12,
        SDN_Club = 13,
        SDN_DriveIn = 14,
        SDN_CarPaint = 15,
        SDN_ShoppingGirl = 16,
        SDN_Hiker = 17,
        SDN_Transporter = 18,
        SDN_Convoy = 19,
        SDN_CarPaintDeluxe = 20,

        SDN_Count
    };

public:
    inline int32_t getMechType() const { return mechType; }
    inline uint32_t getMechID() const { return mechID; }
    inline void setOwner( GameModeWorld *pGameMode ) { pOwner = pGameMode; }
    inline GameModeWorld *getOwner() const { return pOwner; }
    inline bool hasCurrentActivity() const {
        return spotDisplayNameIndex != -1;
    }
    inline SpotMap *getCurrentSpot() const { return pCurrentSpot; }
    inline Entity *getCurrentSpotEntity() const { return pCurrentSpotEntity; }
    inline Entity *getCurrentExtraChallenge() const {
        return pCurrentExtraChallenge;
    }

public:
    PlayerGameMech( int32_t type, uint32_t id );
    ~PlayerGameMech();

    bool onMessage( Entity *pSender, int32_t eventCode, void *pParam );

    const char *getDisplayTypeName() const;
    const char *getDisplayName() const;

private:
    void clearCurrentActivity();

private:
    int32_t mechType;
    uint32_t mechID;
    GameModeWorld *pOwner;

    uint64_t currentActivityHashcode;
    SpotMap *pCurrentSpot;
    Entity *pCurrentSpotEntity;
    int32_t spotDisplayTypeIndex;
    int32_t spotDisplayNameIndex;
    Entity *pCurrentExtraChallenge;
};

void GetSpotDisplayIndices( const SpotMap *pSpot,
                            int32_t *pOutTypeIndex,
                            int32_t *pOutNameIndex );
void GetExtraChallengeDisplayIndices( const Entity *pEntity,
                                      int32_t *pOutTypeIndex,
                                      int32_t *pOutNameIndex );
int32_t GetSpotChallengeKind( const SpotMap *pSpot );
int32_t GetSpotGameModeID( const SpotMap *pSpot );
