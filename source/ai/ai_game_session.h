#pragma once

#include "ai/ai_race.h"

class CarState;
class GameMechanism;
class GSAI;
struct MissionInfo;
struct VehicleBaseResources;

// Race message posted through GameSession::onRaceMessage (type 2 = checkpoint
// split).
struct RaceSessionMessage : public AiRaceMessage {
    int32_t Step = 0;
    float Time = 0.0f;
    int32_t Lap = 0;
};

// CGameOpponent: a race participant.
class GameParticipant {
public:
    // SCGameSessionParams
    struct SessionParams {
        int32_t LastCheckpoint = 0;
        uint32_t LastCheckpointTime = 0u;
        int32_t LastPosition = 0;
        int32_t CurrentLap = 0;
        GameParticipant *pRankNextPlayer = nullptr;
        uint32_t FinalScore = 0u;
        float Penalty = 0.0f;
        uint32_t Level = 0u;
        int32_t State = 0;
    };

    enum eState : int32_t {
        PS_Installed = 2,
        PS_Finished = 5,
        PS_Racing = 7,
    };

public:
    inline SessionParams &getSessionParams() { return params; }
    inline void resetRaceData() {
        // Inlined in FUN_007317e0
        const int32_t state = params.State;
        params = SessionParams();
        params.State = state;
    }
    inline int32_t getFinishPosition() const { return finishPosition; }
    inline void setFinishPosition( const int32_t position ) {
        finishPosition = position;
    }

public:
    virtual ~GameParticipant() {}

    virtual bool isActive() const = 0;
    virtual bool isMaster() const = 0;
    virtual bool isRemote() const { return false; }
    virtual void addToWorld() = 0;
    virtual void removeFromWorld() = 0;
    virtual int32_t getState() const { return params.State; }
    virtual void setState( const int32_t state ) { params.State = state; }
    virtual char *getName() = 0;
    virtual CarState *getCarState() const = 0;

protected:
    SessionParams params;
    int32_t finishPosition = -1;
};

// CGameOpponentOffline (0x100 bytes, vtable 0x00f2a118)
class GameParticipantOffline : public GameParticipant {
public:
    static constexpr int32_t kNameLength = 32;

public:
    GameParticipantOffline();

    bool isActive() const override { return bActive; }
    bool isMaster() const override { return bMaster; }
    void addToWorld() override;
    void removeFromWorld() override;
    char *getName() override { return pName; }
    CarState *getCarState() const override { return pCarState; }

    bool instantiate( const int32_t missionType );
    void installDriver();
    bool release();

public:
    bool bIsLocalPlayer;
    Matrix4x4f_t StartPositionMat;
    int32_t CarDBID;
    int32_t CarPaintIndex;
    int32_t BotID;
    char pPNJName[64];
    int32_t OpponentIndex;
    int32_t AILevel;
    bool bActive;
    bool bMaster;
    bool bAddedToWorld;
    VehicleBaseResources *pVehicleRsc;
    CarState *pCarState;
    bool bPhysicsInstanciated;
    char pName[kNameLength];
};

// CGameSession: race participants as seen by the game mechanism.
class GameSession {
public:
    enum eEvent : int32_t {
        E_Unknown99 = 0x99,
        E_Unknown9A = 0x9a,
        E_Install = 0x9b,
        E_PollInstall = 0x9c,
        E_Unknown9D = 0x9d,
        E_Release = 0x9e,
    };

public:
    virtual ~GameSession() {}

    virtual void setListener( GameMechanism *pListener ) = 0;
    virtual bool unknown1( int32_t, int32_t ) { return false; }
    virtual bool sendRaceMessage( const AiRaceMessage &message ) = 0;
    virtual bool trySetLocalParticipantState( const int32_t state ) = 0;
    virtual void setLocalParticipantState( const int32_t state ) = 0;
    virtual int32_t getCapacity() const = 0;
    virtual int32_t getNumParticipants() const = 0;
    virtual int32_t countParticipantsInState( const int32_t state ) = 0;
    virtual GameParticipant *getParticipant( const int32_t index ) = 0;
    virtual GameParticipant *findByCarID( const uint32_t carID ) = 0;
    virtual GameParticipant *getLocalParticipant() = 0;
    virtual int32_t getParticipantIndex( GameParticipant * /*pParticipant*/ ) {
        // FUN_00423430
        return -1;
    }
    virtual int32_t getLocalParticipantIndex() const {
        // FUN_008074c0
        return 0;
    }
    virtual bool unknownD( int32_t ) { return true; }
    virtual int32_t getMaxPlayers() const = 0;
    virtual int32_t onEvent( const int32_t eventID ) = 0;
    virtual bool isOnline() const = 0;
    virtual void onRaceStarted() {}
    virtual void onRaceStopped() {}
};

// GameSessionOffline (GSAI+0xc0, 0x1020 bytes, vtable 0x00f2a140)
class GameSessionOffline : public GameSession {
public:
    static constexpr int32_t kMaxNumParticipants = 16;
    static constexpr int32_t kMaxNumPlayers = 8;

public:
    inline void setMissionInfo( MissionInfo *pInfo ) { pMissionInfo = pInfo; }

public:
    explicit GameSessionOffline( GSAI *pOwnerAI );

    void setListener( GameMechanism *pNewListener ) override;
    bool sendRaceMessage( const AiRaceMessage &message ) override;
    bool trySetLocalParticipantState( const int32_t state ) override;
    void setLocalParticipantState( const int32_t state ) override;
    int32_t getCapacity() const override { return capacity; }
    int32_t getNumParticipants() const override { return numParticipants; }
    int32_t countParticipantsInState( const int32_t state ) override;
    GameParticipant *getParticipant( const int32_t index ) override;
    GameParticipant *findByCarID( const uint32_t carID ) override;
    GameParticipant *getLocalParticipant() override { return &participants[0]; }
    int32_t getMaxPlayers() const override { return kMaxNumPlayers; }
    int32_t onEvent( const int32_t eventID ) override;
    bool isOnline() const override { return false; }

private:
    void setup();
    void computeStartGrid();
    bool instantiateAll();
    bool releaseAll();

private:
    MissionInfo *pMissionInfo;
    GSAI *pAI;
    GameMechanism *pListener;
    int32_t capacity;
    int32_t numParticipants;
    int32_t numPlayers;
    int32_t numOpponents;
    GameParticipantOffline participants[kMaxNumParticipants];
};

int32_t GetNumOpponents( MissionInfo *pMissionInfo );
