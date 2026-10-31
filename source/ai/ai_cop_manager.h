#pragma once

#include "ai/ai_road_iterator.h"
#include "core/popup_callback.h"
#include "flash/entities/flash_entity.h"
#include "game/police_state.h"
#include "game/scenaric/gs_scenaric.h"

class AudioClient;
class CarState;
class edAudioSample;
class edAudioStreamEntry;
class EntityCinematic;
class FlashListBookmark;
class FlashPlayer;
class ScenaricBank;

enum class eCopVoiceEvent : int32_t {
    CVE_InfLimitReached = 0,
    CVE_SearchToChase = 1,
    CVE_AskFor2ndCar = 2,
    CVE_AskFor3rdCar = 3,
    CVE_AskForPoliceDam = 4,
    CVE_AskForAggressive = 5,
    CVE_WitnessedOffence = 6,
    CVE_Arrest = 7,
    CVE_ChaseToSearch = 8,
    CVE_SearchToPatrol = 9,
    CVE_WarnChased = 10,
};

// Police manager (wanted level, chasers, police dams and arrest)
class AiCopMng : public CinematicPlayer, public PopupCallback {
public:
    static constexpr int32_t kNumCopVoices = 94;
    static constexpr float kNoDamLateralRatio = -1024.0f; // DAT_00f417f8

    struct Fine {
        int32_t NumCarsCrashed = 0;
        int32_t CarsTotalValue = 0;
        int32_t NumSettingsCrashed = 0;
        int32_t SettingsTotalValue = 0;
        int32_t LevelFactor = 1;
        int32_t TotalValue = 0;
    };

public:
    inline bool isEnabled() const { return bEnabled; }
    inline void setEnabled( const bool bEnable ) { bEnabled = bEnable; }

    inline bool isPoliceDamRequested() const { return bRequestPoliceDam; }
    inline void setPoliceDamRequested( const bool bRequest ) {
        bRequestPoliceDam = bRequest;
    }
    inline bool isPoliceDamNeeded() const { return bNeedPoliceDam; }
    inline void setPoliceDamNeeded( const bool bNeed ) {
        bNeedPoliceDam = bNeed;
    }
    inline const AiRoadIt &getDamRoadIt() const { return damRoadIt; }
    inline void setDamRoadIt( const AiRoadIt &it ) { damRoadIt = it; }
    inline uint32_t getNumPoliceDams() const { return numPoliceDams; }
    inline void incrementNumPoliceDams() { numPoliceDams++; }
    inline uint32_t getNumWantedPoliceDams() const {
        return numWantedPoliceDams;
    }
    inline void setNumWantedPoliceDams( const uint32_t numDams ) {
        numWantedPoliceDams = numDams;
    }
    inline CarState *getPoliceDamTarget() const { return pPoliceDamTarget; }
    inline void setPoliceDamTarget( CarState *pTarget ) {
        pPoliceDamTarget = pTarget;
    }
    inline float getNextDamLateralRatio() const { return nextDamLateralRatio; }
    inline void setNextDamLateralRatio( const float ratio ) {
        nextDamLateralRatio = ratio;
    }
    inline CarState *getPoliceTarget() const { return pPoliceTarget; }
    inline float getPlayerToCopMinDistance() const {
        return playerToCopMinDistance;
    }
    inline bool canBeAggressive() const { return bCanBeAggressive; }
    inline bool canDoPoliceDams() const { return bCanDoPoliceDams; }
    inline void setLevel( const bool bHighLevel, const bool bMaxLevel ) {
        bCanBeAggressive = bHighLevel;
        bCanDoPoliceDams = bMaxLevel;
    }
    inline bool isSendingPlayerToJail() const { return bSendPlayerToJail; }
    inline void setSendPlayerToJail( const bool bSend ) {
        bSendPlayerToJail = bSend;
    }
    inline float getLastWarnTime() const { return lastWarnTime; }
    inline const Fine &getCurrentFine() const { return currentFine; }
    inline bool isInfringementSaved() const { return bInfringementSaved; }

    // Police dam request (FUN_009cca60 inlined)
    inline void requestPoliceDam( CarState *pTarget ) {
        bRequestPoliceDam = true;
        pPoliceDamTarget = pTarget;
    }

public:
    AiCopMng();
    ~AiCopMng();

    void initialize();
    void terminate();
    void reset();
    void tick();

    bool needsMoreChasers() const;
    bool canSpawnPoliceDam() const;
    bool getDamRoadIt( AiRoadIt &outIt ) const;
    void startPoliceDam( const AiRoadIt &it );
    int32_t countChasers( const CarState *pTarget ) const;
    int32_t computeNumWantedChasers( CarState *pTarget ) const;
    bool canPlayWarnVoice() const;
    void onVoiceEvent( const eCopVoiceEvent event );
    void saveInfringement();
    void restoreInfringement();

    bool startArrestFade();
    void computeFine( CarState *pCar );
    void applyFine( CarState *pCar,
                    const bool bShowInstantFine,
                    const bool bChargeFine,
                    const bool bResetInfringement,
                    const bool bShowFine );
    bool executeAction( int32_t action, void *pSource, void *pData ) override;
    void onClose( ePopupAnswer answer, void *pData, int32_t userData ) override;

    bool interceptBookmarkAction( const FlashMessage message,
                                  FlashListBookmark *pBookmarkList );
    bool interceptFlashAction( const FlashMessage message,
                               FlashPlayer *pPlayer );
    bool interceptFreerideAction( const FlashMessage message );

    inline EntityCinematic *getArrestCinematic() const {
        return pArrestCinematic;
    }

    static void OnArrestFadeOut( void *pData, void *pSource );

private:
    bool bEnabled;
    EntityCinematic *pArrestCinematic;
    ScenaricBank *pArrestCinematicBank;
    bool bRequestPoliceDam;
    bool bNeedPoliceDam;
    AiRoadIt damRoadIt;
    uint32_t numPoliceDams;
    uint32_t numWantedPoliceDams;
    CarState *pPoliceDamTarget;
    float nextDamLateralRatio;
    CarState *pPoliceTarget;
    float playerToCopMinDistance;
    bool bCanBeAggressive;
    bool bCanDoPoliceDams;
    float lastGameCounterValue;

    struct RadioChannel {
        edAudioStreamEntry *pInstance = nullptr;
        void *pBuffer = nullptr;
        uint32_t BufferSize = 0u;
        std::unique_ptr<edAudioSample> pSample;
        int32_t LastVoice = -1;
        uint32_t LoadGeneration = 0u;
        bool bLoading = false;
    };

    AudioClient *pAudioClient;
    RadioChannel radio;
    RadioChannel highPriorityRadio;
    float lastWarnTime;
    std::array<float, kNumCopVoices> copVoicesTime;

    FlashPlayer *pInstantFinePlayer;
    FlashMessage interceptedMessage;
    FlashPlayer *pInterceptedPlayer;
    FlashListBookmark *pInterceptedBookmark;
    Fine currentFine;
    bool bSendPlayerToJail;

    int32_t previousNumWantedChasers;
    int32_t previousNumChasers;
    bool bWasChased;
    bool bWasSearched;
    PoliceState savedInfringement;
    bool bInfringementSaved;

private:
    void rebaseTimers();
    float computeClosestCopDistance( CarState *pCar ) const;
    void updateChaseState();
    void updateVoiceEvents();
    uint32_t updateNumWantedPoliceDams();
    bool findArrestCinematic( const char *pName );
    bool startArrestCinematic();
    bool startArrestLoopCinematic();
    void computeArrestCinematicFrame( Matrix4x4f_t &outFrame ) const;
    void stopVoices();
    float computeVoiceElapsedTime( int32_t voice );
    void playVoice( int32_t voice, bool bHighPriority );
    void playInfLimitVoice( CarState *pPlayerCar );
    bool loadVoice( RadioChannel &channel, int32_t voice );
    void onVoiceLoaded( RadioChannel &channel, uint32_t generation );
    void startVoice( RadioChannel &channel );
    void releaseVoice( RadioChannel &channel, bool bStop );
    void onVoiceFinished();
    void chargeCurrentFine();
    int32_t requestInstantFine( CarState *pCar, const bool bShowPrompt );
    void showInstantFine( const int32_t fine, const bool bCanPay );
    void resolveInterceptedAction( const bool bChargeFine,
                                   const bool bResetInfringement );
    void cancelInterceptedAction();
    void clearInterceptedAction();
};

extern AiCopMng gAiCopMng;
