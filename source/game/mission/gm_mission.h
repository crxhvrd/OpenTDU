#pragma once

#include "game/gm_world.h"

#include "ai/ai_race.h"
#include "game/mission/mission_extra_challenge.h"
#include "game/mng_ingame_menu.h"
#include "online/mng_chat.h"
#include "render/mng_font_players.h"

class GameSession;
class AudioClient;
class edAudioStreamEntry;

class GMMission;
class ScenaricBank;

// Mission data the offline game session reads
struct MissionInfo {
    static constexpr int32_t kMaxNumParticipants = 8;

    struct Participant {
        int32_t Unknown0;
        int32_t CarDBID;
        int32_t PaintIndex;
        int32_t BotID;
    };

    GMMission *pOwner;
    Participant Participants[kMaxNumParticipants];
    int32_t NumParticipants;
};

class GMMission : public GameModeWorld, public AiRaceDescriptor {
public:
    inline const Matrix4x4f_t &getSpawnFrame() const { return spawnFrame; }
    inline MissionInfo &getMissionInfo() { return missionInfo; }
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
    MngInGameMenu mngInGameMenu;
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
    bool bQuitRequested;
    bool bLastPlayerPromptShown;
    bool bRankingShown;
    bool field_0x107098;
    bool bRelockChallengeRoads;
    MissionExtraChallenge extraChallenge;
};
