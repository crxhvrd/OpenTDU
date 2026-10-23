#pragma once

#include "parameters/game_parameter.h"

class FlashPlayer;

// Settings of a challenge event (loaded from its .IGE file)
class EventParameters {
public:
    // gmth
    enum eGameType : int32_t {
        EGT_TimeAttack = 0,
        EGT_Speed = 1,
        EGT_Driving = 2,
        EGT_Race = 3,
    };

    enum eSource : int32_t {
        ES_None = 0,
        ES_HardcodedMission = 1,
        ES_Buffer = 2,
        ES_IGEFile = 3,
        ES_Online = 4,
        ES_CustomChallenge = 5,
    };

    // gmpm
    enum eSpotType : int32_t {
        EST_Solo = 0xc0c0,
        EST_Multi = 0xc0c1,
        EST_Club = 0xc0c2,
    };

public:
    inline GameParameterList &getParameterList() { return parameterList; }
    inline eSource getSource() const { return source; }
    inline void setSource( const eSource newSource ) { source = newSource; }
    inline const char *getRequestedName() const { return pRequestedName; }
    inline void setRequestedName( const char *pName ) {
        strcpy( pRequestedName, pName );
    }
    inline void setField124( const uint32_t value ) { field124 = value; }
    inline const char *getFileName() const { return pFileName; }
    inline char *getFileNameBuffer() { return pFileName; }
    inline GameParameter &getGameTypeParameter() { return gameType; }
    inline GameParameter &getSpotTypeParameter() { return spotType; }
    inline int32_t getGameType() { return int32_t( gameType.getValue() ); }
    inline int32_t getSpotType() { return int32_t( spotType.getValue() ); }
    inline const char *getName() const { return name.getString(); }
    inline char *getNameBuffer() { return name.getStringBuffer(); }
    inline GameParameterChoice &getLevel() { return level; }
    inline GameParameterNumber &getRewardMoney() { return rewardMoney; }
    inline GameParameterLaps &getNumLaps() { return numLaps; }
    inline GameParameterNumber &getLoopCheckpointIndex() {
        return loopCheckpointIndex;
    }
    inline GameParameterBool &getLoop() { return loop; }
    inline GameParameterNumber &getNumOpponents() { return numOpponents; }
    inline GameParameterDBString &getDatabaseDescription() {
        return databaseDescription;
    }
    inline GameParameterBool &getStartChallenge() { return startChallenge; }
    inline GameParameterBool &getCountdownMode() { return countdownMode; }
    inline GameParameterTime &getCountdown() { return countdown; }
    inline GameParameterBool &getTraffic() { return traffic; }
    inline GameParameterBool &getOffRoad() { return offRoad; }
    inline GameParameterBool &getCockpitView() { return cockpitView; }
    inline GameParameterBool &getGps() { return gps; }
    inline GameParameterBool &getNextChallenge() { return nextChallenge; }
    inline GameParameterTime &getCheckpointBonus() { return checkpointBonus; }
    inline GameParameterDBString &getDatabaseName() { return databaseName; }
    inline GameParameterBool &getCollisions() { return collisions; }
    inline GameParameterBool &getCops() { return cops; }
    inline GameParameterLevel &getTrafficDensity() { return trafficDensity; }
    inline GameParameterLevel &getCopsDensity() { return copsDensity; }
    inline GameParameterNumber &getPenaltyCredits() { return penaltyCredits; }
    inline GameParameterTime &getGoldTime() { return goldTime; }
    inline GameParameterTime &getSilverTime() { return silverTime; }
    inline GameParameterTime &getBronzeTime() { return bronzeTime; }
    inline GameParameterSpeed &getGoldSpeed() { return goldSpeed; }
    inline GameParameterSpeed &getTrapSpeed() { return trapSpeed; }
    inline GameParameterSpeed &getSilverSpeed() { return silverSpeed; }
    inline GameParameterSpeed &getBronzeSpeed() { return bronzeSpeed; }

public:
    EventParameters();

    void reset();
    bool isParameterAvailable( GameParameter *pParameter,
                               const int32_t parameterCategory );
    char *buildVehicleRestrictionText( char *pOutText,
                                       const uint32_t maxLength,
                                       const bool bShowCategories );
    void setGroupRestrictionVars( FlashPlayer *pPlayer );
    void checkRequirements( bool *pOutLevelTooLow,
                            bool *pOutCurrentCarOK,
                            bool *pOutOtherCarOK );
    void setNameVariable( FlashPlayer *pPlayer, const char *pVariableName );

private:
    eSource source;
    char pRequestedName[0x100];
    char pFileName[9];
    uint32_t field124;
    GameParameterList parameterList;

    GameParameterVersion version;
    GameParameter gameType;
    GameParameter spotType;
    GameParameterBool test;
    GameParameterChoice vehicleType;
    GameParameterVehicleRestriction vehicleRestriction;
    GameParameterChoice level;
    GameParameterChoice drivingAid;
    GameParameterBool countdownMode;
    GameParameterBool offRoad;
    GameParameterBool collisions;
    GameParameterBool traffic;
    GameParameterBool cops;
    GameParameterBool startChallenge;
    GameParameterBool gps;
    GameParameterBool noCollision;
    GameParameterVector position;
    GameParameterBool loop;
    GameParameterNumber loopCheckpointIndex;
    GameParameterString name;
    GameParameterString description;
    GameParameterDBString databaseName;
    GameParameterDBString databaseDescription;
    GameParameterTeamCount numTeams;
    GameParameterLaps numLaps;
    GameParameterLevel trafficDensity;
    GameParameterTime goldTime;
    GameParameterTime silverTime;
    GameParameterTime bronzeTime;
    GameParameterTime countdown;
    GameParameterTime checkpointBonus;
    GameParameterLevel copsDensity;
    GameParameterBool cockpitView;
    GameParameterNumber penaltyCredits;
    GameParameterSpeed goldSpeed;
    GameParameterSpeed silverSpeed;
    GameParameterSpeed bronzeSpeed;
    GameParameterSpeed trapSpeed;
    GameParameterNumber fee;
    GameParameterNumber rewardMoney;
    GameParameterBool forc;
    GameParameterNumber numOpponents;
    GameParameterChoice opponentVehicleType;
    GameParameterVehicleRestriction opponentRestriction;
    GameParameterBool nextChallenge;
};
