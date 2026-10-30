#pragma once

#include "game/gm_world.h"

#include "ai/ai_race.h"
#include "core/random/bcpl.h"
#include "game/mission/mission_extra_challenge.h"
#include "game/mission/mng_blck_rds.h"
#include "game/mission/mng_nav_path_display.h"
#include "game/mng_ingame_menu.h"
#include "online/mng_chat.h"
#include "render/mng_font_players.h"

class GameSession;
class AudioClient;
class edAudioStreamEntry;

class GMMission;
class GameParameterChoice;
class GameParameterVehicleRestriction;
class ScenaricBank;

// Mission data the offline game session reads
struct MissionInfo {
    static constexpr int32_t kMaxNumParticipants = 8;
    static constexpr int32_t kMaxNumCandidates = 0x200;
    static constexpr int32_t kMaxNumBikeBots = 32;

    struct Participant {
        int32_t CarDBID;
        int32_t PaintIndex;
        int32_t BotID;
        float Rating;
    };

    GMMission *pOwner;
    RandomGen Random;
    Participant Participants[kMaxNumParticipants];
    int32_t NumEntries;
    int32_t NumSlots;
    GameParameterVehicleRestriction *pRestriction;
    GameParameterChoice *pVehicleType;
    int32_t PlayerCarID;
    int32_t PlayerCarRawID;
    int32_t NumCandidateCars;
    int32_t CandidateCars[kMaxNumCandidates];
    int32_t NumCandidateBots;
    int32_t CandidateBots[kMaxNumCandidates];
    int32_t NumBikeBots;
    int32_t BikeBots[kMaxNumBikeBots];
    float RefAcceleration;
    float RefBrakingDistance;
    float RefTopSpeed;
    float RefTurningRadius;

    explicit MissionInfo( GMMission *pOwnerGM );

    void
    selectOpponents( GameParameterChoice *pOpponentVehicleType,
                     GameParameterVehicleRestriction *pOpponentRestriction );

private:
    void buildCandidateCars( const int32_t numOpponents );
    void buildCandidateCarsNone();
    void buildCandidateCarsBrand();
    void buildCandidateCarsCategory();
    void buildCandidateCarsRange( const int32_t numOpponents );
    void pickOpponentCars( const int32_t numOpponents );
    void computeFieldReference();
    float computeCarRating( const int32_t carID ) const;
    void computeRatingsAndTier();
    bool hasBikeOpponent() const;
    void buildBikeBotList();
    void buildBotListRange( const int32_t minTier, const int32_t maxTier );
    void buildBotListExact( const int32_t tier );
    int32_t pickBikeBot( const int32_t carID );
    void assignBots();
    void assignPaints();
    void addCandidateCar( const int32_t carID );
    bool isInstantChallengeBotSlot( const int32_t index ) const;
};

class GMMission : public GameModeWorld, public AiRaceDescriptor {
public:
    inline const Matrix4x4f_t &getSpawnFrame() const { return spawnFrame; }
    inline bool getSavedFreeRideBool3() const { return bSavedFreeRideBool3; }
    inline MissionInfo &getMissionInfo() { return missionInfo; }
    inline GameSession *getGameSession() const { return pGameSession; }
    inline void setRankingShown( const bool bShown ) { bRankingShown = bShown; }

public:
    GMMission();
    ~GMMission();

    int32_t getNumCheckpoints() const override;
    bool getCheckpointRoadLocation( const int32_t index,
                                    AiRoadIt &outIt ) override;
    void setCheckpointRoadLocation( const int32_t index,
                                    const AiRoadIt &it ) override;

    Matrix4x4f_t computeGridMatrix( const int32_t slotIndex,
                                    const int32_t numSlots );
    inline MissionExtraChallenge &getExtraChallenge() { return extraChallenge; }
    inline MngBlckRds &getBlockedRoads() { return mngBlckRds; }
    void endExtraChallenge( const bool bWon );
    bool isExtraChallengeOutroPending() const;
    bool isExtraChallengeSucceeded() const;

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;
    bool restart() override;

    void onPauseMenuRequested( bool bOpened ) override;
    void onMenuPauseExit( bool bRestoreState ) override;

private:
    bool isSoloEvent();
    void quitMission();
    void exitMission();
    bool attachOffroadWarningSample();
    void startRestartFade();

    static void OnRestartFadedOut( void *pData, void *pSource );

private:
    MngBlckRds mngBlckRds;
    MngInGameMenu mngInGameMenu;
    MngNavPathDisplay mngNavPathDisplay;
    MngChat mngChat;
    MngFontPlayers mngFontPlayers;

    ScenaricBank *pCheckpointBank;
    bool bFromLobby;
    Matrix4x4f_t spawnFrame;
    GameSession *pGameSession;
    MissionInfo missionInfo;
    bool bOnlineSession;
    AudioClient *pOffroadWarningClient;
    edAudioStreamEntry *pOffroadWarningEntry;
    bool bRestartRequested;
    bool bSavedFreeRideBool3;
    bool bQuitRequested;
    bool bLastPlayerPromptShown;
    bool bRankingShown;
    bool field_0x107098;
    bool bRelockChallengeRoads;
    MissionExtraChallenge extraChallenge;
};

extern MissionInfo *gpMissionInfo;
