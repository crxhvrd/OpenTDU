#pragma once

#include "game/cinematic/cin_res_collection.h"
#include "game/gm_world.h"
#include "game/scenaric/scenaric_bank.h"
#include "movie/mng_movie.h"

class CinematicMusicEmitter;
class EntityAvatar;
class EntityCamera;
class EntityCinematic;
class FlashPlayer;
struct FlashMessage;

// Game Intro
class GMIntro : public GameModeWorld {
public:
    static constexpr int32_t kNumIntroCarSlots = 4;
    static constexpr int32_t kMaxIntroCarParts = 96;
    static constexpr int32_t kMaxIntroCarSlotParts = 8;

    // Cut intro car slot.
    struct IntroCarSlot {
        const char *pName = nullptr;
        int32_t RefCount = 0;
        int32_t CinematicRefCount = 0;
        bool bPosePending = false;
        CinematicActor *pActor = nullptr;
        CinActorState PendingPose;
        Eigen::Vector3f PendingScale = Eigen::Vector3f::Ones();
        bool bPendingTransformTrack = false;
        // Discs FL/FR/RL/RR then forks FL/FR/RL/RR
        Matrix4x4f_t PartMatrices[kMaxIntroCarSlotParts];
        int32_t NumParts = 0;
        int32_t NumPartsUpdated = 0;
        CarState *pCarState = nullptr;
        MngShowCaseCarControl *pShowcaseControl = nullptr;
    };

    struct IntroCarPart {
        const char *pName = nullptr;
        IntroCarSlot *pSlot = nullptr;
        int32_t PartIndex = -1;
        bool bUsed = false;
    };

public:
    inline bool isCockpitZFarActive() const { return bCockpitZFar; }
    inline bool isCockpitZFarNoPostCockpit() const {
        return bCockpitZFarNoPostCockpit;
    }

public:
    GMIntro();
    ~GMIntro();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;

    EntityAvatar *addIntroAvatarReference( const int32_t index );

    IntroCarSlot *acquireIntroCar( const char *pName, bool &bOutBindActor );
    void releaseIntroCar( IntroCarSlot *pSlot );
    IntroCarPart *allocateIntroCarPart( const char *pName );
    static IntroCarSlot *FindIntroCarSlot( const char *pName );

private:
    static constexpr int32_t kNumStates = 8;
    static constexpr int32_t kNumBankSlots = 4;
    static constexpr int32_t kNumChoiceAvatars = 8;
    static constexpr int32_t kNumIntroAvatars = 3;
    static constexpr int32_t kMaxBankNPCAvatars = 3;
    static constexpr int32_t kNumChoiceCinematics = 4;

    enum eBankSlotState : int32_t {
        BSS_None = 0,
        BSS_Loading = 1,
        BSS_Loaded = 2,
        BSS_Installed = 3,
    };

    enum eAvatarSelection : int32_t {
        AS_ReleaseAll = 0,
        AS_LoadAll = 1,
        AS_AllButSelected = 2,
        AS_Remaining = 3,
        AS_SelectedOnly = 4,
    };

    struct IntroBankSlot {
        void *pMemory = nullptr;
        uint32_t Size = 0;
        int32_t State = BSS_None;
        ScenaricBank Bank;
    };

private:
    void setState( const bool bSkipped, int32_t newState );
    void leaveState();
    void enterState( const int32_t state );
    void enterAvatarChoice();
    void stopAvatarChoiceMusic();

    void swapBanks( const int32_t state );
    void loadBank( const int32_t slot, const int32_t heapBuffer );
    void unloadBank( const int32_t slot );
    void unloadBanks( const uint32_t mask );
    bool isBanksReady( const int32_t state );
    void installBank( const int32_t slot );

    void launchCinematic( const Matrix4x4f_t *pFrame,
                          EntityCinematic *pCinematic,
                          const bool bDeferred );
    void selectAvatars( const int32_t mode, const int32_t selectedIndex );
    void releaseSelectedAvatarReference( EntityCinematic *pSender );

    void startSaturationFade( const float target, const float duration );
    void tickSaturationFade( const float deltaTime );

    void startSpeedBlur( const float target );
    void startConstantBlur( const float target, const float rate );
    void stopRadialBlur();
    void tickRadialBlur( const float deltaTime );
    void setZFarMode( const bool bEnabled,
                      EntityCinematic *pSender,
                      const bool bNoPostCockpit );

    void computeAvatarCameraOffsets();
    void getAvatarCameraView( const int32_t index,
                              const bool bCloseUp,
                              Eigen::Vector4f &outPosition,
                              Eigen::Vector4f &outTarget ) const;
    void startCameraTransition( const Eigen::Vector4f &endPosition,
                                const Eigen::Vector4f &endTarget,
                                const float duration );
    void tickCameraTransition( const float deltaTime );
    inline bool isCameraTransitionRunning() const {
        // FUN_0042cec0
        return camTransitionDuration > camTransitionElapsed;
    }
    void switchToFarView();
    void switchToCloseUpView();
    void selectNextAvatar();
    void selectPreviousAvatar();
    void confirmAvatar();
    void tickAvatarChoice( const float deltaTime );

    CarState *installIntroCar( const int32_t carID, IntroCarSlot &slot );

    static void SpawnDriverAvatar( const bool bKeepReference,
                                   const bool bAsync );
    static void ReleaseDriverFromCar();
    static void EnableFakeDOF();
    static void DisableFakeDOF();

private:
    MngMovie mngMovie;
    MngShowCaseCarControl introShowcaseCarControl;

    IntroCarPart introCarParts[kMaxIntroCarParts];
    IntroCarSlot introCarSlots[kNumIntroCarSlots];

    FlashPlayer *pFlashPlayer;
    bool bMovieLoaded;
    int32_t state;
    int32_t pendingState;

    void *pBankHeapCursor[2];
    void *pBankHeapBase[2];
    uint32_t bankHeapHalfSize;
    int32_t bankHeapBuffer;
    IntroBankSlot bankSlots[kNumBankSlots];
    uint32_t currentBankMask;
    uint32_t previousBankMask;

    EntityCinematic *pIntroSc01;
    EntityCinematic *pIntroSc02;
    EntityCinematic *pIntroSc03;
    EntityCamera *pCamSelectionClose;
    EntityCamera *pCamSelectionFar;
    EntityCinematic *pChoiceCinematics[kNumChoiceCinematics];
    EntityCinematic *pInplaneSc02;
    EntityCinematic *pInplaneSc04;
    EntityCinematic *pFinishCine;
    Entity *pAvatarChoiceSoundLeft;
    Entity *pAvatarChoiceSoundRight;
    std::unique_ptr<CinematicMusicEmitter> pAvatarChoiceMusic;
    EntityCinematic *pAvatarChoiceCine;
    bool bAvatarChosen;

    Eigen::Vector4f camFarOffset;
    Eigen::Vector4f camCloseOffset;
    Eigen::Vector4f camFarTargetOffset;
    Eigen::Vector4f camCloseTargetOffset;
    float camTransitionElapsed;
    float camTransitionDuration;
    float carouselDirection;
    Eigen::Vector4f camTransitionStartPosition;
    Eigen::Vector4f camTransitionEndPosition;
    Eigen::Vector4f camTransitionStartTarget;
    Eigen::Vector4f camTransitionEndTarget;
    float carouselTime;
    int32_t selectedAvatar;
    bool bCloseUpView;

    EntityAvatar *pChoiceAvatars[kNumChoiceAvatars];
    EntityAvatar *pIntroAvatars[kNumIntroAvatars];
    EntityAvatar *pBankNPCAvatars[kMaxBankNPCAvatars];
    uint32_t numBankNPCAvatars;
    bool bIntroAvatarsSpawned;

    float saturationFadeFrom;
    float saturationFadeTo;
    float saturationFadeCurrent;
    float saturationFadeElapsed;
    float saturationFadeDuration;

    EntityCinematic *pPendingCinematic;
    int32_t choiceCinematicIndex;
    bool bCinematicStarted;

    float blurTarget;
    float blurRate;
    bool bBlurConstant;
    float blurCurrent;
    Eigen::Vector4f blurPreviousCameraPosition;
    float blurSpeed;

    bool bCockpitZFar;
    bool bCockpitZFarNoPostCockpit;

    bool bWeatherPending;
    char pendingWeatherName[64];

    int32_t delayedStateCountdown;
    int32_t delayedState;
    uint32_t numDrawSuspensions;
    bool bForceStreamCheck;
    bool bSavedSunEnabled;
};

extern GMIntro *gpGMIntro;
extern int32_t gIntroStartState;
extern int32_t gIntroCmdAvatarIndex;
